#ifndef _EPOLLSERVER_H__
#define _EPOLLSERVER_H__
#include <memory>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include "LiveStream.h"
#include <unordered_map>
class TcpConnection;
class EpollServer {
private:
    int listenFd_;
    int epollFd_;
    std::map<int, TcpConnection*> connections_; // fd -> TcpConnection*
    
public:
    EpollServer(int port);
    ~EpollServer();
    void run();
    void acceptConnection();
    void handleClient(int clientFd,epoll_event event);
    int getEpollFd() const { return epollFd_; }
    std::unordered_map<std::string, LiveStream> g_liveStreams;
};

#endif //_EPOLLSERVER_H__