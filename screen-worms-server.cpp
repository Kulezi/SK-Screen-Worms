#include "screen-worms-server.h"

// Parses shell options, sets params accordingly or terminates the server when they are incorrect.
void getOptions(int argc, char **argv, ServerParameters &params) {
    int opt;
    int cnt = 0;
    while ((opt = getopt(argc, argv, "p:s:t:v:w:h:")) != -1) {
        cnt += 2;
        switch (opt) {
        case 'p':
            params.portNum = getValFromOptarg(0, MAX_PORT, "Invalid port");
            break;
        case 's':
            params.rng = getValFromOptarg(0, MAX_SEED, "Invalid seed");
            break;
        case 't':
            params.turningSpeed = getValFromOptarg(MIN_TURNING_SPEED, MAX_TURNING_SPEED, "Invalid turning speed");
            break;
        case 'v':
            params.rps = getValFromOptarg(MIN_RPS, MAX_RPS, "Invalid rps");
            break;
        case 'w':
            params.width = getValFromOptarg(MIN_WIDTH, MAX_WIDTH, "Invalid width");
            break;
        case 'h':
            params.height = getValFromOptarg(MIN_HEIGHT, MAX_HEIGHT, "Invalid height");
            break;
        default:
            std::cerr << "Option is not supported\n";
            exit(1);
        }
    }

    if (cnt != argc - 1) {
        std::cerr << "Invalid arguments\n";
        exit(1);
    }
}

// Resets the game timer to default settings.
void resetGameTimer(ServerParameters &params, ServerNetworkData &socks) {
    uint64_t trash;
    read(socks.client[GAME_TIMER_ID].fd, &trash, sizeof(trash));

    itimerspec ts;
    ts.it_interval.tv_sec = (params.rps == 1 ? 1 : 0);
    ts.it_interval.tv_nsec = (params.rps == 1 ? 0 : (1000000000 / params.rps));
    ts.it_value.tv_sec = (params.rps == 1 ? 1 : 0);
    ts.it_value.tv_nsec = (params.rps == 1 ? 0 : (1000000000 / params.rps));

    if (timerfd_settime(socks.client[GAME_TIMER_ID].fd, 0, &ts, NULL) < 0) {
        syserr("timerfd_settime()");
    }
}

// Returns ServerNetworkData with sockets that are ready for connections with clients.
ServerNetworkData setupSockets(ServerParameters &params) {
    ServerNetworkData result{};
    result.client[0].fd = -1;
    result.client[0].events = POLLIN;
    result.client[0].revents = 0;

    result.client[0].fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (result.client[0].fd == -1)
        syserr("Opening input socket");

    timeval tv {};
    tv.tv_sec = 0;
    tv.tv_usec = 10;
    if (setsockopt(result.client[0].fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) < 0) {
        syserr("setsockopt");
    }
    int no = 0;
    setsockopt(result.client[0].fd, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&no, sizeof(no));

    result.server.sin6_family = AF_INET6;
    result.server.sin6_addr = in6addr_any;
    result.server.sin6_port = htons(params.portNum);
    if (bind(result.client[0].fd, (sockaddr *)&(result.server), (socklen_t)sizeof(result.server)) < 0) {
        syserr("Binding central socket");
    }

    size_t length = sizeof(result.server);
    if (getsockname (result.client[0].fd, (sockaddr*)&(result.server), (socklen_t*)&length) == -1) {
        syserr("Getting socket name");
    }

    for (int i = 1; i <= MAX_PLAYERS; i++) {
        result.freeTimerIds.insert(i);
        if ((result.client[i].fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1) {
            syserr("timerfd_create()");
        }

        result.client[i].events = POLLIN;
        result.client[i].revents = 0;
    }

    if ((result.client[GAME_TIMER_ID].fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1) {
        syserr("timerfd_create()");
    }

    itimerspec ts;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;
    ts.it_value.tv_nsec = 0;
    ts.it_value.tv_sec = 0;

    if (timerfd_settime(result.client[GAME_TIMER_ID].fd, 0, &ts, NULL) < 0) {
        syserr("timerfd_settime()");
    }

    result.client[GAME_TIMER_ID].events = POLLIN;
    result.client[GAME_TIMER_ID].revents = 0;
    return result;
}

// Gets next values from the random number generator according to task specification.
uint64_t getNextRand(uint64_t &rng) {
    uint64_t ret = rng;
    rng = (uint64_t)rng * 279410273 % 4294967291;
    return ret;
}

// Parses raw data from buf to fill msg structure with according values.
int parseClientMsg(char *buf, ssize_t len, ClientMsg &msg) {
    if (len < MIN_CLIENT_MSG_SIZE || len > MAX_CLIENT_MSG_SIZE) {
        return 1;
    }

    msg.sessionId = 0;
    for (int i = 0; i < 8; i++) {
        msg.sessionId = (msg.sessionId << 8) + (uint8_t)buf[i];
    }

    msg.turnDirection = +(uint8_t)buf[8];

    if (msg.turnDirection > 2) {
        return 1;
    }

    msg.nextExpectedEventNo = 0;
    for (int i = 9; i < 13; i++) {
        msg.nextExpectedEventNo = (msg.nextExpectedEventNo << 8) + (uint8_t)buf[i];
    }

    for (int i = 13; i < len; i++) {
        if (buf[i] < 33 || buf[i] > 126) {
            return 1;
        }

        msg.playerName += buf[i];
    }

    return 0;
}

// Returns ClientAddr struct with data from clientAddress minding if it uses IPv4 or IPv6.
ClientAddr getClientAddr(sockaddr_storage &clientAddress) {
    ClientAddr ret{};
    char clientIp[INET6_ADDRSTRLEN + 5];
    switch (clientAddress.ss_family) {
        case AF_INET: {
            sockaddr_in clientAddressIPv4 = *reinterpret_cast<sockaddr_in*>(&clientAddress);
            inet_ntop(AF_INET, &(clientAddressIPv4.sin_addr), clientIp, sizeof(clientIp));
            ret.port = ntohs(clientAddressIPv4.sin_port);
            ret.ip = std::string(clientIp);
            ret.family = AF_INET;
            break;
        }

        case AF_INET6: {
            sockaddr_in6 clientAddressIPv6 = *reinterpret_cast<sockaddr_in6*>(&clientAddress);
            inet_ntop(AF_INET6, &(clientAddressIPv6.sin6_addr), clientIp, sizeof(clientIp));
            ret.port = ntohs(clientAddressIPv6.sin6_port);
            ret.ip = std::string(clientIp);
            ret.family = AF_INET6;
            break;
        }

        default:
            ret.family = -1;
            break;
    }

    return ret;
}

// Kicks players that are idling for too long.
void handleTimeouts(ServerNetworkData &socks, GameState &game) {
    for (int i = 1; i <= MAX_PLAYERS; i++) {
        if (socks.client[i].fd != -1 && (socks.client[i].revents & POLLIN)) {
            for (auto &elt : socks.clientId) {
                if (elt.second.timerId == i) {
                    socks.usedNames.erase(elt.second.playerName);
                    game.readyPlayers.erase(elt.second);
                    if (!game.active) {
                        game.playerPos.erase(elt.second);
                    }

                    socks.clientId.erase(elt.first);
                    break;
                }
            }

            socks.freeTimerIds.insert(i);
        }
    }
}

// Prepares client timer and accepts a new player to join the game.
void acceptPlayer(ServerNetworkData &socks, ClientAddr &addr, ClientInfo &msg) {
    int timerId = *socks.freeTimerIds.begin();
    socks.freeTimerIds.erase(timerId);
    msg.timerId = timerId;
    socks.clientId[addr] = msg;

    if (msg.playerName != "") {
        socks.usedNames.insert(msg.playerName);
    }

    itimerspec ts;
    ts.it_interval.tv_sec = ts.it_interval.tv_nsec = ts.it_value.tv_nsec = 0;
    ts.it_value.tv_sec = CLIENT_TIMEOUT;

    if (timerfd_settime(socks.client[timerId].fd, 0, &ts, NULL) < 0) {
        syserr("timerfd_settime()");
    }
}

// Return the floor of given floating point value.
int getFloor(double x) {
    return static_cast<int>(std::floor(x));
}

// Prefixes the string with its big endian encoded length, and joins it with its crc32 checksum.
std::string wrapEvent(std::string event) {
    event = tonStr(event.size(), 4) + event;
    event += tonStr(crc32(event.c_str(), event.size()), 4);
    return event;
}

// Creates a new game event that can be read by clients.
void createNewGameEvent(ServerParameters &params, GameState &game) {
    std::string res;
    uint32_t eventNo = game.events.size();
    res += tonStr(eventNo, 4);
    res += tonStr(NEW_GAME_EVENT, 1);
    res += tonStr(params.width, 4);
    res += tonStr(params.height, 4);
    for (auto &i : game.readyPlayers) {
        res += i.playerName;
        res.append(1, '\0');
    }

    game.events.push_back(wrapEvent(res));
}


// Creates a game over event that can be read by clients.
void createGameOverEvent(GameState &game) {
    std::string res;
    uint32_t eventNo = game.events.size();
    res += tonStr(eventNo, 4);
    res += tonStr(GAME_OVER_EVENT, 1);

    game.events.push_back(wrapEvent(res));
}


// Creates a player eliminated event that can be read by clients.
void createPlayerEliminatedEvent(int order, GameState &game) {
    std::string res;
    uint32_t eventNo = game.events.size();
    res += tonStr(eventNo, 4);
    res += tonStr(PLAYER_ELIMINATED_EVENT, 1);
    res += tonStr(order, 1);

    game.events.push_back(wrapEvent(res));
}


// Creates a new pixel event that can be read by clients.
void createPixelEvent(int order, int x, int y, GameState &game) {

    std::string res;
    uint32_t eventNo = game.events.size();
    res += tonStr(eventNo, 4);
    res += tonStr(PIXEL_EVENT, 1);
    res += tonStr(order, 1);
    res += tonStr(x, 4);
    res += tonStr(y, 4);


    game.events.push_back(wrapEvent(res));
}


// Renews players idle timer, so the server doesn't kick him for idling.
void renewPlayer(ServerNetworkData &socks, ClientAddr &addr) {
    int timerId = socks.clientId[addr].timerId;

    itimerspec ts{};
    ts.it_interval.tv_sec = ts.it_interval.tv_nsec = ts.it_value.tv_nsec = 0;
    ts.it_value.tv_sec = CLIENT_TIMEOUT;

    if (timerfd_settime(socks.client[timerId].fd, 0, &ts, NULL) < 0) {
        syserr("timerfd_settime()");
    }

    uint64_t res;
    read(socks.client[timerId].fd, &res, sizeof(res));
}

// Updates turn directions of players according to msg.
void updatePlayerState(GameState &game, ClientMsg &msg, ClientInfo &info) {
    if (msg.playerName.empty())
        return;

    if (!game.active && msg.turnDirection) {
        game.readyPlayers.insert(info);
    }
    
    game.playerPos[info].turnDirection = msg.turnDirection;
}

// Sends events with ID's not less than @from to given client.
void sendEvents(ServerNetworkData &socks, ClientAddr addr, GameState &game, uint32_t from) {
    sockaddr_storage sAddr{};
    if (addr.family == AF_INET) {
        sockaddr_in sAddrIPv4 = *reinterpret_cast<sockaddr_in*>(&sAddr);
        sAddrIPv4.sin_family = AF_INET;
        inet_pton(AF_INET, addr.ip.c_str(), &sAddrIPv4.sin_addr);
        sAddrIPv4.sin_port = htons(addr.port);
        sAddr = *reinterpret_cast<sockaddr_storage*>(&sAddrIPv4);
    } else {
        assert(addr.family == AF_INET6);
        sockaddr_in6 sAddrIPv6 = *reinterpret_cast<sockaddr_in6*>(&sAddr);
        sAddrIPv6.sin6_family = AF_INET6;
        inet_pton(AF_INET6, addr.ip.c_str(), &sAddrIPv6.sin6_addr);
        sAddrIPv6.sin6_port = htons(addr.port);
        sAddr = *reinterpret_cast<sockaddr_storage*>(&sAddrIPv6);
    }

    std::string dgram = tonStr(game.gameId, 4);
    for (; from < game.events.size(); ++from) {
        dgram += game.events[from];
        if (from + 1 == game.events.size() || dgram.size() + game.events[from].size() > 550) {
            assert(dgram.size() <= 550);

            if (sendto(socks.client[0].fd, dgram.c_str(), dgram.size(), 0, (sockaddr *) &sAddr, sizeof(sAddr)) == -1) {
                return;
            }
            
            dgram = tonStr(game.gameId, 4);
        }
    }
}

// Handles a single UDP packet received from some client.
void handleConnection(ServerNetworkData &socks, GameState &game, GameState &oldGame) {
    sockaddr_storage clientAddress{};
    socklen_t rcvaLen = sizeof(clientAddress);
    ssize_t len = recvfrom(socks.client[0].fd, socks.buf, MAX_EVENT_SIZE, 0, (sockaddr *)&clientAddress, &rcvaLen);
    if (len > 0) {
        ClientAddr addr = getClientAddr(clientAddress);
        if (addr.family != AF_INET && addr.family != AF_INET6)
            return;

        ClientMsg msg{};
        if (parseClientMsg(socks.buf, len, msg)) {
            return;
        }

        ClientInfo info = {-1, msg.sessionId, msg.playerName};
        if (!socks.clientId.count(addr) && socks.clientId.size() < MAX_PLAYERS) {
            if (!msg.playerName.empty() && socks.usedNames.count(msg.playerName)) {
                return;
            }

            acceptPlayer(socks, addr, info);
        } else if (socks.clientId.count(addr)) {
            if (msg.sessionId < socks.clientId[addr].sessionId) {
                return;
            }

            if (msg.sessionId > socks.clientId[addr].sessionId) {
                info.timerId = socks.clientId[addr].timerId;
                socks.clientId[addr] = info;
            }

            renewPlayer(socks, addr);
        } else {  
            return;
        }

        info = socks.clientId[addr];
        updatePlayerState(game, msg, info);
        if (game.active) {
            sendEvents(socks, addr, game, msg.nextExpectedEventNo);
        } else {
            sendEvents(socks, addr, oldGame, msg.nextExpectedEventNo);
        }
    }
}

// Simulates a single frame of the game's logic.
void updateGame(ServerParameters &params, GameState &game) {
    std::vector<ClientInfo> toErase;
    for (auto &i: game.playerPos) {
        if (i.second.turnDirection == 1) {
            i.second.direction += params.turningSpeed;
            i.second.direction %= 360;
        } 

        if (i.second.turnDirection == 2) {
            i.second.direction -= params.turningSpeed;
            i.second.direction %= 360;
            if (i.second.direction < 360) i.second.direction += 360;
        }

        int oldX = getFloor(i.second.x), oldY = getFloor(i.second.y);    
        i.second.x += cos(i.second.direction / 180.0 * M_PI);
        i.second.y += sin(i.second.direction / 180.0 * M_PI);

        int x = getFloor(i.second.x), y = getFloor(i.second.y);
        if (oldX == x && oldY == y) {
            continue;
        } else if (game.eatenFields.count({x, y}) || x < 0 || x >= params.width || y < 0 || y >= params.height) {
            createPlayerEliminatedEvent(i.second.order, game);
            toErase.push_back(i.first);
            if (game.playerPos.size() - toErase.size() == 1) {
                createGameOverEvent(game);
                break;
            }
        } else {
            createPixelEvent(i.second.order, x, y, game);
            game.eatenFields.insert({x, y});
        }
    }

    for (auto i : toErase) {
        game.playerPos.erase(i);
    }
}

// Sends new events to all players.
void broadcastEvents(ServerNetworkData &socks, GameState &game, uint32_t from) {
    for (auto &i : socks.clientId) {
        sendEvents(socks, i.first, game, from);
    }
}

// Generates a single game frame and broadcasts it to all connected clients.
void handleGameFrame(ServerParameters &params, ServerNetworkData &socks, GameState &game) {
    uint64_t ret;
    size_t lastEventNo = game.events.size();

    int rbytes = read(socks.client[GAME_TIMER_ID].fd, &ret, sizeof(ret));
    if (rbytes > 0) {
        if (!game.active) {
            return;
        }

        for (uint64_t rep = 0; rep < ret; rep++) {
            updateGame(params, game);
            broadcastEvents(socks, game, lastEventNo);
            lastEventNo = game.events.size();
        }
    }
}

// Dispatches server operations according to active timers.
void handlePollEvent(ServerParameters &params, ServerNetworkData &socks,
                     GameState &game, int timeout, GameState &oldGame) {
    int ret = poll(socks.client, MAX_PLAYERS + 2, timeout);
    if (ret == -1) {
        if (errno == EINTR) {
            fprintf(stderr, "Interrupted syscall\n");
        } else {
            syserr("poll");
        }
    } else if (ret > 0) {
        handleTimeouts(socks, game);
        if (socks.client[0].revents & POLLIN) {
            handleConnection(socks, game, oldGame);
            socks.client[0].revents = 0;
        }

        handleGameFrame(params, socks, game);
        if (game.playerPos.size() < 2) {
            game.active = false;
        }

        for (int i = 0; i <= MAX_PLAYERS + 1; i++) {
            socks.client[i].revents = 0;
        }
    }
}

// Generates the first frame of a new game.
void startGame(ServerParameters &params, GameState &game) {
    game.gameId = getNextRand(params.rng);
    game.active = true;
    createNewGameEvent(params, game);

    std::vector<ClientInfo> toErase;
    int order = 0;
    for (auto &i : game.playerPos) {
        i.second.order = order++;
        i.second.x = getNextRand(params.rng) % params.width + 0.5;
        i.second.y = getNextRand(params.rng) % params.height + 0.5;
        i.second.direction = getNextRand(params.rng) % 360;

        int x = getFloor(i.second.x);
        int y = getFloor(i.second.y);
        if (game.eatenFields.count({x, y})) {
            createPlayerEliminatedEvent(i.second.order, game);
            if (game.playerPos.size() - toErase.size() == 1) {
                createGameOverEvent(game);
                toErase.push_back(i.first);
                break;
            }
        } else {
            createPixelEvent(i.second.order, x, y, game);
            game.eatenFields.insert({x, y});
        }
    }

    for (auto i : toErase) {
        game.playerPos.erase(i);
    }
}

int main(int argc, char **argv) {
    // Set server params to default values.
    ServerParameters params = {(uint64_t)time(NULL) & UINT32_MAX, DEFAULT_TURNING_SPEED,
                               DEFAULT_RPS, DEFAULT_SERVER_PORT, DEFAULT_WIDTH, DEFAULT_HEIGHT};

    // Update params with shell options that user has provided.
    getOptions(argc, argv, params);

    // Prepare sockets for UDP communication.
    ServerNetworkData socks{};
    socks = setupSockets(params);

    GameState oldGame{};

    // Server is meant to run indefinitely, thus we start new games in an endless loop.
    while (true) {
        GameState game{};
        game.active = false;
        while (socks.usedNames.size() < 2 || game.readyPlayers.size() < socks.usedNames.size()) {
            handlePollEvent(params, socks, game, -1, oldGame);
        }

        startGame(params, game);
        resetGameTimer(params, socks);
        broadcastEvents(socks, game, 0);
        if (game.playerPos.size() < 2) {
            game.active = false;
        }

        oldGame.events = {};
        while (game.active) {
            handlePollEvent(params, socks, game, -1, oldGame);
        }

        oldGame.gameId = game.gameId;
        oldGame.events = game.events;
    }
}