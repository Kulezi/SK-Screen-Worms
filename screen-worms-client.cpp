#include "screen-worms-client.h"

// Gets socket address in sockaddr_in6 form from given ip, port and socket type (TCP/UDP).
sockaddr_in6 getSockaddr(char *ip, char *port, int type) {
    sockaddr_in6 ret{};
    addrinfo addr_hints;
    addrinfo *addr_result;
    memset(&addr_hints, 0, sizeof(addrinfo));

    addr_hints.ai_family = AF_UNSPEC;
    addr_hints.ai_socktype = type;
    addr_hints.ai_protocol = 0;
    int err = getaddrinfo(ip, port, &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) { // system error
        syserr("getaddrinfo: %s", gai_strerror(err));
    } else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s", gai_strerror(err));
    }
    
    sockaddr *addr = addr_result->ai_addr;
    if (addr->sa_family == AF_INET) {
        for (int i = 0; i < 16; i++) {
            ret.sin6_addr.s6_addr[i] = 0;
        }

        ret.sin6_addr.s6_addr[10] = ret.sin6_addr.s6_addr[11] = UINT8_MAX;
        *((in_addr_t*)(ret.sin6_addr.s6_addr + 12)) = (reinterpret_cast<sockaddr_in*>(addr))->sin_addr.s_addr;
        ret.sin6_port = (reinterpret_cast<sockaddr_in*>(addr))->sin_port;
    } else if (addr->sa_family == AF_INET6) {
        ret.sin6_addr = (reinterpret_cast<sockaddr_in6*>(addr))->sin6_addr;
        ret.sin6_port = (reinterpret_cast<sockaddr_in6*>(addr))->sin6_port;
    } else {
        syserr("wrong protocol");
    }

    ret.sin6_family = AF_INET6;
    freeaddrinfo(addr_result);
    return ret;
}

// Makes the sockets nonblocking and forcing IPV6 addressing.
void setSockOpts(int sock) {
    timeval tv {};
    tv.tv_sec = 0;
    tv.tv_usec = 10;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) < 0) {
        syserr("setsockopt");
    }

    int no = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&no, sizeof(no)) < 0) {
        syserr("setsockopt");
    }

}

// Prepares a new UDP socket for connection with the server.
int getServerSock() {
    int serverSock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (serverSock == -1) 
        syserr("Opening server socket");

    setSockOpts(serverSock);
    return serverSock;
}

// Prepares a new TCP socket for connection with the GUI.
int getGuiSock() {
    int guiSock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (guiSock == -1) 
        syserr("Opening gui socket");

    int optval;
    if (setsockopt(guiSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
        syserr("setsockopt");

    if (fcntl(guiSock, F_SETFL, O_NONBLOCK) < 0)
        syserr("fcntl");

    int no = 0;
    if (setsockopt(guiSock, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&no, sizeof(no)) == -1) {
        syserr("setsockopt");
    }
    
    int yes = 1;
    if (setsockopt(guiSock, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0)
        syserr("setsockopt");

    return guiSock;
}

// Parses shell options, sets params accordingly or terminates the client when they are incorrect.
void getOptions(ClientParameters &params, int argc, char **argv) {
    int opt;
    int cnt = 0;
    while ((opt = getopt(argc, argv, "n:p:i:r:")) != -1) {
        cnt += 2;
        switch (opt) {
        case 'p':
            params.serverPort = getValFromOptarg(0, MAX_PORT, "Invalid server port");
            break;

        case 'r':
            params.guiPort = getValFromOptarg(0, MAX_PORT, "Invalid gui port");
            break;

        case 'n':
            params.playerName = optarg;
            for (auto i : params.playerName) {
                if (i < 33 || i > 126) {
                    std::cerr <<  "Invalid player name\n";
                    exit(1);
                }
            }

            if (params.playerName.size() > 20) {
                std::cerr <<  "Invalid player name\n";
                exit(1);
            }

            break;

        case 'i': 
            params.guiName = optarg;
            break;

        default:
            std::cerr << "Option is not supported\n";
            exit(1);
        }
    }

    if (cnt != argc - 2) {
        std::cerr << "Invalid arguments\n";
        exit(1);
    }
}

// Sets default values for the timer and makes it nonblocking.
void setupTimer(pollfd &timerPollfd) {
    timerPollfd.fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    timerPollfd.events = POLLIN;
    timerPollfd.revents = 0;

    itimerspec ts;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = MSG_FREQUENCY * 1000 * 1000; 
    ts.it_value.tv_nsec = MSG_FREQUENCY * 1000 * 1000;
    ts.it_value.tv_sec = 0;

    if (timerfd_settime(timerPollfd.fd, 0, &ts, NULL) < 0) {
        syserr("timerfd_settime()");
    }

}

// Gets current time from epoch, used for setting the session ID.
uint64_t curTime() {
    timeval tv{};
    if (gettimeofday(&tv, NULL) == -1) {
        syserr("gettimeofday");
    }

    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

// Creates a move message, in form that can be read by the server (big endian).
std::string createMoveMsg(ClientParameters &params) {
    std::string res = tonStr(params.sessionId, 8);
    res += tonStr(params.turnDirection, 1);
    res += tonStr(params.nextExpectedEventNo, 4);
    res += params.playerName;
    return res;
}

// Creates a NEW_GAME event in form that can be read by the GUI.
std::string createNewGameEvent(int maxx, int maxy, std::vector<std::string> &players) {
    if (maxx < MIN_WIDTH || maxx > MAX_WIDTH || maxy < MIN_HEIGHT || maxy > MAX_HEIGHT) {
        syserr("wrong create new game event");
    }

    std::string result = "NEW_GAME " + std::to_string(maxx) + " " + std::to_string(maxy);
    for (auto &i : players)
        result += " " + i;

    result += "\n";

    return result;
}

// Creates a PIXEL event in form that can be read by the GUI.
std::string createPixelEvent(ClientParameters &params, uint8_t playerNumber, uint32_t x, uint32_t y) {
    if (playerNumber >= params.playerNames.size() || x >= params.width || y >= params.height) {
        syserr("wrong create pixel event");
    }

    std::string result = "PIXEL " + std::to_string(x) + " " + std::to_string(y);
    result += " " + params.playerNames[playerNumber] + "\n";

    return result;
}

// Creates a PLAYER_ELIMINATED event in form that can be read by the GUI.
std::string createPlayerEliminatedEvent(ClientParameters &params, uint8_t playerNumber) {
    if (playerNumber >= params.playerNames.size())
        syserr("wrong create player event");

    return "PLAYER_ELIMINATED " + params.playerNames[playerNumber] + "\n";
}


// Parses a single event from raw form, updates params.events with it.
int parseEvent(ClientParameters &params, char *buf, int64_t bufLen) {
    if (bufLen < MIN_EVENT_SIZE) {
        return -1;
    }

    uint32_t len = strTon(std::string(buf, 4));

    if (4 + len + 4 > bufLen) {
        return -1;
    }

    std::string eventWithoutCRC = std::string(buf, len + 4);
    uint32_t ServerCRC = strTon(std::string(buf + 4 + len, 4));
    uint32_t ClientCRC = crc32(eventWithoutCRC.c_str(), eventWithoutCRC.size());

    if (ServerCRC != ClientCRC) {
        return -1;
    }
    
    // How many letters left to read from event*
    int64_t left = len;

    // We cut the buffer by len
    buf += 4;
    uint32_t eventNo = strTon(std::string(buf, 4));

    // We cut the buffer by event_no
    buf += 4, left -= 4;
    
    uint8_t eventType = strTon(std::string(buf, 1));

    // We cut the buffer by event_type
    buf += 1, left -= 1;

    switch (eventType) {
        case NEW_GAME_EVENT: {
            if (bufLen < 9) {
                syserr("valid crc but event is invalid1");
            }

            uint32_t maxx = strTon(std::string(buf, 4));
            buf += 4, left -= 4;

            uint32_t maxy = strTon(std::string(buf, 4));
            buf += 4, left -= 4;

            std::vector<std::string> players;
            std::string curPlayer;
            while (left > 0) {
                if (buf[0] == '\0' && curPlayer.empty()) {
                    syserr("valid crc but event is invalid2");
                } else if (buf[0] == '\0') {
                    players.push_back(curPlayer);
                    curPlayer.clear();
                } else if (buf[0] < 33 || buf[0] > 126) {
                    syserr("valid crc but event is invalid3");
                } else {
                    curPlayer += buf[0];
                }

                left--;
                if (left == 0 && buf[0] != '\0') {
                    syserr("valid crc but event is invalid4");
                }

                buf++;
            }

            if (players.size() < 2 || players.size() > MAX_PLAYERS || !is_sorted(players.begin(), players.end())) {
                syserr("valid crc but event is invalid5");
            }

            auto it = std::unique(players.begin(), players.end());
            if (it != players.end()) {
                syserr("valid crc but event is invalid6");
            }

            if (eventNo != 0)
                syserr("new game wrong eventno");

            params.events.push_back(createNewGameEvent(maxx, maxy, players));
            params.nextExpectedEventNo++;
            params.width = maxx;
            params.height = maxy;
            params.playerNames = players;
            break;
        }
        
        case PIXEL_EVENT: {
            uint8_t playerNumber = strTon(std::string(buf, 1));
            buf++;
            uint32_t x = strTon(std::string(buf, 4));
            buf += 4;
            uint32_t y = strTon(std::string(buf, 4));

            std::string event = createPixelEvent(params, playerNumber, x, y);
            if (eventNo != params.nextExpectedEventNo || params.finished) {
                break;
            }

            params.events.push_back(event);
            params.nextExpectedEventNo++;
            break;
        }
        
        case PLAYER_ELIMINATED_EVENT: {
            uint8_t playerNumber = strTon(std::string(buf, 1));
            std::string event = createPlayerEliminatedEvent(params, playerNumber);
            if (eventNo != params.nextExpectedEventNo || params.finished) {
                break;
            }

            params.events.push_back(event);
            params.nextExpectedEventNo++;
            break;
        }

        case GAME_OVER_EVENT: {
            if (eventNo != params.nextExpectedEventNo || params.finished) {
                break;
            }

            params.finished = 1;
        }
    
        default:
            break;
    }

    return len + 8;
}

// Parses events from raw message format, updates params with them.
void parseEvents(ClientParameters &params, char *buf, size_t len) {
    size_t parsed = 0;
    while (parsed < len) {
        int ret = parseEvent(params, buf + parsed, len - parsed);
        if (ret <= 0)
            return;

        parsed += ret;
    }
}

// Updates the server on current turn direction of player's worm.
void trySendMove(ClientParameters &params, NetInfo &net) {
    if (!net.timer[CYCLIC].revents)
        return;

    if (net.timer[CYCLIC].revents != POLLIN)
        syserr("timer fail");

    if (!(net.timer[SERVER_SOCK].revents & POLLOUT))
        syserr("server socket fail");

    net.timer[SERVER_SOCK].revents = 0;
    uint64_t reps = 0;
    int ret;

    if ((ret = read(net.timer[SERVER_SOCK].fd, &reps, sizeof(reps))) < 0) {
        syserr("timerfd broke");
    }

    if (!ret || !reps) {
        return;
    }

    std::string msg = createMoveMsg(params);
    if (sendto(net.serverSock, msg.c_str(), msg.size(), 0,
               (sockaddr *)&net.serverAddr, sizeof(net.serverAddr)) == -1) {
        syserr("sendto");
    }
}

//  Updates params with current direction of this client player's worm from the GUI.
void tryGetMove(ClientParameters &params, NetInfo &net) {
    if (!net.timer[GUI_SOCK].revents)
        return;

    if (!(net.timer[GUI_SOCK].revents & POLLOUT) || (net.timer[GUI_SOCK].revents & (POLLHUP | POLLERR | POLLNVAL)))
        syserr("gui sock fail");
    
    if (!(net.timer[GUI_SOCK].revents & POLLIN)) {
        net.timer[GUI_SOCK].revents = 0;
        return;
    }

    char buf[BUF_SIZE];
    std::string msg;
    int ret;

    // Zero value on this variable indicates there was no connection with the GUI yet.
    int was = 0;
    while ((ret = read(net.guiSock, buf, BUF_SIZE)) != 0) {
        was = 1;
        if (ret == -1 && errno != EAGAIN) {
            syserr("read from gui");
        } else if (ret == -1)
            break;
        
        for (int i = 0; i < ret; ++i) {
            if (buf[i] == '\n') {
                if (msg == "LEFT_KEY_DOWN") {
                    params.turnDirection = TURN_LEFT;
                } else if (msg == "RIGHT_KEY_DOWN") {
                    params.turnDirection = TURN_RIGHT;
                } else if (msg == "RIGHT_KEY_UP" && params.turnDirection == TURN_RIGHT) {
                    params.turnDirection = 0;
                } else if (msg == "LEFT_KEY_UP" && params.turnDirection == TURN_LEFT) {
                    params.turnDirection = 0;
                }

                msg.clear();
            } else msg.append(1, buf[i]);
        }
    }

    // We lost connection with the GUI, so our client needs to terminate.
    if (was == 0) {
        syserr("gui connection");
    }

    // Update revents to ensure poll doesn't lead us here until the next timer cycle.
    net.timer[GUI_SOCK].revents = 0;
}

// Sends a single event to GUI through a TCP socket.
void sendEventToGui(std::string &event, NetInfo &net) {
    size_t sent = 0;
    const char *buf = event.c_str();
    size_t count = event.size();

    int ret;
    while (sent < count) {
        ret = write(net.guiSock, buf + sent, count - sent);
        if (ret == -1) {
            syserr("write");
        }

        sent += ret;
    }
}


// Tries to get new events from the server, and updates the GUI on any changes on the board.
void tryGetEvents(ClientParameters &params, NetInfo &net) {
    if (!(net.timer[SERVER_SOCK].revents & POLLIN))
        return;
        
    net.timer[SERVER_SOCK].revents = 0;

    char buf[MAX_EVENT_SIZE];
    uint32_t nextEventNo = params.nextExpectedEventNo;
    ssize_t len;
    while ((len = recvfrom(net.serverSock, buf, MAX_EVENT_SIZE, 0, NULL, NULL)) != 0) {
        if (len < 0 && errno != EINTR) {
            break;
        }

        if (len < 8)
            continue;

        uint32_t gameId = strTon(std::string(buf, 4));

        // When game ID is different we need to update params to handle a new game session.
        if (params.gameId != gameId) {
            nextEventNo = 0;
            params.gameId = gameId;
            params.width = 0;
            params.height = 0;
            params.playerNames.clear();
            params.events.clear();
            params.nextExpectedEventNo = 0;
            params.turnDirection = 0;
            params.finished = false;
        }

        parseEvents(params, buf + 4, len - 4);
    }

    for (size_t i = nextEventNo; i < params.events.size(); i++) {
        sendEventToGui(params.events[i], net);
    }
}


int main(int argc, char **argv) {
    if (argc < 2 || argv[1][0] == '\0' || argv[1][0] == '-') {
        std::cerr << "usage ./screen-worms-client game_server [-n player_name] [-p n] [-i gui_server] [-r n]\n";
        exit(1);
    }

    std::string localhost = "localhost";

    // Initialize client parameters with default values.
    ClientParameters params{};
    params.serverName   = argv[1];
    params.serverPort   = DEFAULT_SERVER_PORT;
    params.guiName      = &localhost[0];
    params.guiPort      = DEFAULT_GUI_PORT;
    params.playerName   = "";
    params.sessionId    = curTime();

    // Update client parameters according to shell options
    getOptions(params, argc, argv);

    std::string serverPortStr = std::to_string(params.serverPort);
    std::string guiPortStr    = std::to_string(params.guiPort);
    NetInfo net;


    // Prepare sockets for connection
    net.serverAddr = getSockaddr(params.serverName, &serverPortStr[0], SOCK_DGRAM);
    net.guiAddr    = getSockaddr(params.guiName, &guiPortStr[0], SOCK_STREAM);

    net.serverSock = getServerSock();
    net.guiSock    = getGuiSock();

    // Connect to GUI, so it starts displaying itself.
    if (connect(net.guiSock, (sockaddr *) &net.guiAddr, sizeof(net.guiAddr)) == -1 && errno != EINPROGRESS) {
        syserr("connect");
    }

    // Prepare timers to ensure regular intervals between packets.
    setupTimer(net.timer[CYCLIC]);
    net.timer[SERVER_SOCK].fd       = net.serverSock;
    net.timer[GUI_SOCK].fd          = net.guiSock;
    net.timer[SERVER_SOCK].events   = net.timer[GUI_SOCK].events  = POLLIN | POLLOUT;
    net.timer[SERVER_SOCK].revents  = net.timer[GUI_SOCK].revents = 0;

    // Intended endless loop, client is closed on losing connection with GUI.
    while (true) {
        if (poll(net.timer, 3, -1) == -1 && errno != EINTR) {
            syserr("poll");
        }

        tryGetEvents(params, net);
        tryGetMove(params, net);
        trySendMove(params, net);
    }
} 