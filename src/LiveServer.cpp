#include "LiveServer.h"
#include "HttpFlvSession.h"
#include "HttpHlsSession.h"
#include "Session.h"
#include "TcpConnection.h"
#include <cstddef>
#include <fcntl.h>
#include <memory>
#include <sys/socket.h>
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
    TcpConnection* conn = new TcpConnection(clientFd, this, session);
    connections_[clientFd] = conn;
    if (!poller_->Add(clientFd, EventMask::Readable, TriggerMode::Edge)) {
        std::cerr << "Failed to add client socket to poller" << std::endl;
        delete conn;
        connections_.erase(clientFd);
        close(clientFd);
    }
}

void LiveServer::handleClient(int clientFd, EventMask event) {
    auto it = connections_.find(clientFd);
    if (it == connections_.end()) {
        std::cerr << "Connection not found for fd: " << clientFd << std::endl;
        return;
    }
    TcpConnection* conn = it->second;
    // if (hasEvent(event, EventMask::Error)) {
    //     std::cerr << "Error event on fd: " << clientFd << std::endl;
    //     closeConn(*poller_, connections_, clientFd);
    //     return;
    // }
    // if (hasEvent(event, EventMask::Hangup)) {
    //     std::cerr << "Hangup event on fd: " << clientFd << std::endl;
    //     closeConn(*poller_, connections_, clientFd);
    //     return;
    // }
    if (hasEvent(event, EventMask::Readable)) {
        conn->handldRead();
    }
    if (hasEvent(event, EventMask::Writable)) {
        conn->handleWrite();
    }
}

LiveServer::~LiveServer() {
    close(listenFd_);
    for (auto& pair : connections_) {
        delete pair.second;
    }
}