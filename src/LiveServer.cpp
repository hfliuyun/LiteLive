#include "LiveServer.h"
#include "HttpFlvSession.h"
#include "HttpHlsSession.h"
#include "Session.h"
#include "TcpConnection.h"
#include <cstddef>
#include <fcntl.h>
#include <memory>
#include <sys/socket.h>
#include <iostream>
#include <unistd.h>
#include <utility>
LiveServer::LiveServer(int port) {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        exit(EXIT_FAILURE);
    }
    int socket_opt = 1;
    if (setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &socket_opt, sizeof(socket_opt)) == -1) {
        std::cerr << "Failed to set socket options" << std::endl;
        exit(EXIT_FAILURE);
    }
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenFd_, (sockaddr*)&serverAddr, sizeof(serverAddr))) {
        std::cerr << "Failed to bind socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    if (listen(listenFd_, SOMAXCONN) == -1) {
        std::cerr << "Failed to listen on socket" << std::endl;
        exit(EXIT_FAILURE);
    }

    poller_ = std::unique_ptr<Poller>(CreatePoller());
    if (!poller_) {
        std::cerr << "Failed to create poller instance" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!poller_->Add(listenFd_, EventMask::Readable, TriggerMode::Level)) {
        std::cerr << "Failed to add listen socket to poller" << std::endl;
        exit(EXIT_FAILURE);
    }

}

void LiveServer::run() {
    const int MAX_EVENTS = 10;
    std::vector<ReadyEvent> events(MAX_EVENTS);
    while (true) {
        int numEvents = poller_->Wait(-1, events);
        if (numEvents == -1) {
            std::cerr << "poller_->Wait failed" << std::endl;
            //exit(EXIT_FAILURE);
        }
        for (int i = 0; i < numEvents; ++i) {
            if (events[i].fd == listenFd_) {
                std::cout << "New connection received" << std::endl;
                acceptConnection();
            } else {
                // Handle data from existing connection
                std::cout << "Data received from client" << std::endl;
                handleClient(events[i].fd, events[i].mask);
            }
        }
    sweepOnce();
    }
}

void LiveServer::acceptConnection() {
    // Handle new connection
    sockaddr_in clientAddr{};
    socklen_t clientAddrLen = sizeof(clientAddr);
    int clientFd = accept(listenFd_, (sockaddr*)&clientAddr, &clientAddrLen);
    if (clientFd == -1) {
        std::cerr << "Failed to accept connection" << std::endl;
        return;
    }
    int flags = fcntl(clientFd, F_GETFL, 0);
    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

#if 0
    std::shared_ptr<Session> session = std::make_shared<HttpHlsSession>();
    elif 0
    std::shared_ptr<Session> session = std::make_shared<HttpFlvSession>();
#else
    std::shared_ptr<Session> session = nullptr;
#endif
    auto conn = std::make_unique<TcpConnection>(clientFd, this, session);
    conn->setOnCloseCb([this](int fd) {onConnectionCloseCallback(fd);});
    if (!poller_->Add(clientFd, EventMask::Readable, TriggerMode::Edge)) {
        std::cerr << "Failed to add client socket to poller" << std::endl;
        close(clientFd);
    } else {
        connections_[clientFd] = std::move(conn);
    }
}

void LiveServer::handleClient(int clientFd, EventMask event) {
    auto it = connections_.find(clientFd);
    if (it == connections_.end()) {
        std::cerr << "Connection not found for fd: " << clientFd << std::endl;
        return;
    }
    if (hasEvent(event, EventMask::Error)) {
        it->second->CloseConnection();
        return;
    }
    if (hasEvent(event, EventMask::Hangup)) {
        std::cerr << "Hangup event on fd: " << clientFd << std::endl;
        it->second->CloseConnection();
        return;
    }
    if(it->second->isClose()) return;
    if (hasEvent(event, EventMask::Readable)) {
        it->second->handldRead();
    }
    if(it->second->isClose()) return;
    if (hasEvent(event, EventMask::Writable)) {
        it->second->handleWrite();
    }
}


void LiveServer::onConnectionCloseCallback(int fd) {
    poller_->Remove(fd);
    toDelete_.push_back(fd);

}

void LiveServer::sweepOnce(){
    for(int fd: toDelete_) {
        close(fd);
        connections_.erase(fd);
    }
    toDelete_.clear();
}

#ifdef LITE_LIVE_TESTING
size_t LiveServer::connectionCount() const {
    return connections_.size();
}
bool LiveServer::hasConnection(int fd) const {
    auto it = connections_.find(fd);
    return it != connections_.end() ? true : false;
}
void LiveServer::injectConnection(int fd, std::unique_ptr<TcpConnection> conn) {
    conn->setOnCloseCb([this](int fd){onConnectionCloseCallback(fd);});
    connections_[fd] = std::move(conn);
}
#endif

LiveServer::~LiveServer() {
    close(listenFd_);
    for(auto &it :connections_){
        close(it.first);
    }
    connections_.clear();
}