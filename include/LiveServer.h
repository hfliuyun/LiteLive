#ifndef _LIVESERVER_H__
#define _LIVESERVER_H__
#include "LiveStream.h"
#include <arpa/inet.h>
#include <iostream>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include "Poller.h"
class TcpConnection;
class LiveServer {
private:
    int listenFd_;
    std::unique_ptr<Poller> poller_;
    std::map<int, TcpConnection*> connections_; // fd -> TcpConnection*

public:
    LiveServer(int port);
    ~LiveServer();
    void run();
    void acceptConnection();
    void handleClient(int clientFd, EventMask event);
    Poller* poller() const { return poller_.get(); }
    std::unordered_map<std::string, LiveStream> g_liveStreams;
};

#endif //_LIVESERVER_H__