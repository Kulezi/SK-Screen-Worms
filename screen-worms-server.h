#ifndef SCREEN_WORMS_SERVER_H
#define SCREEN_WORMS_SERVER_H

#include "common.h"

#define MAX_SEED UINT32_MAX

#define MIN_TURNING_SPEED 1
#define DEFAULT_TURNING_SPEED 6
#define MAX_TURNING_SPEED 90

#define MIN_RPS 1
#define DEFAULT_RPS 50
#define MAX_RPS 250

#define GAME_TIMER_ID MAX_PLAYERS + 1

#define MIN_CLIENT_MSG_SIZE 13
#define MAX_CLIENT_MSG_SIZE 33

#define CLIENT_TIMEOUT 2

struct ServerParameters {
    uint64_t rng;
    int64_t turningSpeed, rps, portNum, width, height;
};

struct PlayerPos {
    double x, y;
    int direction;
    int turnDirection;
    int order;
};

struct ClientInfo {
    int timerId;
    uint64_t sessionId;
    std::string playerName;
};

struct cmpInfo {
    bool operator()(const ClientInfo &a, const ClientInfo &b) const {
        return a.playerName < b.playerName;
    }
};

struct GameState {
    bool active;
    uint32_t gameId;
    EventVector events;
    std::set<std::pair<int, int>> eatenFields;
    std::map<ClientInfo, PlayerPos, cmpInfo> playerPos;
    std::set<ClientInfo, cmpInfo> readyPlayers;
};

struct ClientMsg {
    uint64_t sessionId;
    uint8_t turnDirection;
    uint32_t nextExpectedEventNo;
    std::string playerName;
};

struct ClientAddr {
    int family;
    std::string ip;
    int port;
};

struct cmpAddr {
    bool operator()(const ClientAddr &a, const ClientAddr &b) const {
        if (a.family == b.family) {
            if (a.port == b.port) {
                return a.ip < b.ip;
            }

            return a.port < b.port;
        }

        return a.family < b.family;
    }
};

struct ServerNetworkData {
    pollfd client[MAX_PLAYERS + 2];
    sockaddr_in6 server;
    char buf[MAX_EVENT_SIZE];
    std::map<ClientAddr, ClientInfo, cmpAddr> clientId;
    std::set<int> freeTimerIds;
    std::set<std::string> usedNames;
};


#endif //SCREEN_WORMS_SERVER_H
