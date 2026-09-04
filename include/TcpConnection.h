#ifndef __TCPCONNECTION_H__
#define __TCPCONNECTION_H__

#include "Session.h"
#include "iostream"
#include "sys/socket.h"
#include <cerrno>
#include <cstdio>
#include <memory>
#include <unistd.h>

class EpollServer;
class TcpConnection {
private:
    int fd_;
    std::string readBuffer_;
    std::string writeBuffer_;
    EpollServer* epollServer_;

    std::shared_ptr<Session> session_;

public:
    TcpConnection(int fd, EpollServer* epollServer, std::shared_ptr<Session> session)
        : fd_(fd), epollServer_(epollServer), session_(session) {};

    void handldRead();

    void send(const std::string& data);

    void handleWrite();

    void CloseConnection();
};

#endif //__TCPCONNECTION_H__