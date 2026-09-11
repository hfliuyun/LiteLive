#ifndef _LIVESERVER_H__
#define _LIVESERVER_H__
#include "LiveStream.h"
#include <arpa/inet.h>
#include <cstddef>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Poller.h"
class TcpConnection;
class LiveServer {
private:
    int listenFd_;
    std::unique_ptr<Poller> poller_;
    std::map<int, std::unique_ptr<TcpConnection>> connections_; // fd -> TcpConnection*
    std::vector<int> toDelete_;
    void onConnectionCloseCallback(int fd);
public:
    LiveServer(int port);
    ~LiveServer();
    void run();
    void acceptConnection();
    void handleClient(int clientFd, EventMask event);
    Poller* poller() const { return poller_.get(); }
    void sweepOnce();

#ifdef LITE_LIVE_TESTING
    size_t connectionCount() const;
    bool hasConnection(int fd) const;
    void injectConnection(int fd, std::unique_ptr<TcpConnection> conn);
#endif

    std::unordered_map<std::string, LiveStream> g_liveStreams;
};

#endif //_LIVESERVER_H__