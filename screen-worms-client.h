#ifndef SCREEN_WORMS_CLIENT_H
#define SCREEN_WORMS_CLIENT_H

#include "common.h"

#define MSG_FREQUENCY 30

#define TURN_RIGHT 1
#define TURN_LEFT 2

enum timer_num {CYCLIC, SERVER_SOCK, GUI_SOCK};

struct ClientParameters {
    char *serverName;
    int serverPort;
    char *guiName;
    int guiPort;
    std::string playerName;
    uint64_t sessionId;
    uint8_t turnDirection;
    uint32_t nextExpectedEventNo;
    uint32_t gameId;

    uint32_t width;
    uint32_t height;
    std::vector<std::string> playerNames;
    EventVector events;
    bool finished;
};

struct NetInfo {
    sockaddr_in6 serverAddr, guiAddr;
    int serverSock, guiSock;
    // timer[0] -> 30ms cyclic, timer[1] -> serverSock, timer[2] -> guiSock
    pollfd timer[3];
};


#endif //SCREEN_WORMS_CLIENT_H
