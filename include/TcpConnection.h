#ifndef __TCPCONNECTION_H__
#define __TCPCONNECTION_H__

#include "Session.h"
#include "iostream"
#include "sys/socket.h"
#include <cerrno>
#include <cstdio>
#include <memory>
#include <unistd.h>

class LiveServer;
class TcpConnection {
private:
    int fd_;
    std::string readBuffer_;
    std::string writeBuffer_;
    LiveServer* liveServer_;

    std::shared_ptr<Session> session_;

public:
    TcpConnection(int fd, LiveServer* liveServer, std::shared_ptr<Session> session)
        : fd_(fd), liveServer_(liveServer), session_(session) {};

    void handldRead();

    void send(const std::string& data);

    void handleWrite();

    void CloseConnection();
};

#endif //__TCPCONNECTION_H__