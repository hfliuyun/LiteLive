#include "EpollServer.h"
#include "Session.h"
#include "HttpHlsSession.h"
#include "TcpConnection.h"
#include "HttpFlvSession.h"
#include <memory>
#include <sys/socket.h>
#include <fcntl.h>
#include "RtmpSession.h"
EpollServer::EpollServer(int port) {
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

    epollFd_ = epoll_create1(0);
    if (epollFd_ == -1) {
        std::cerr << "Failed to create epoll instance" << std::endl;
        exit(EXIT_FAILURE);
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listenFd_;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &event) == -1) {
        std::cerr << "Failed to add listen socket to epoll" << std::endl;
        exit(EXIT_FAILURE);
    }
}

void EpollServer::run() {
    const int MAX_EVENTS = 10;
    epoll_event events[MAX_EVENTS];
    while (true) {
        int numEvents = epoll_wait(epollFd_, events, MAX_EVENTS, -1);
        if (numEvents == -1) {
            std::cerr << "epoll_wait failed" << std::endl;
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < numEvents; ++i) {
            if (events[i].data.fd == listenFd_) {
                std::cout << "New connection received" << std::endl;
                acceptConnection();
            } else {
                // Handle data from existing connection
                std::cout << "Data received from client" << std::endl;
                handleClient(events[i].data.fd, events[i]);
            }
        }
    }
}

void EpollServer::acceptConnection() {
    // Handle new connection
    sockaddr_in clientAddr{};
    socklen_t clientAddrLen = sizeof(clientAddr);
    int clientFd = accept(listenFd_, (sockaddr*)&clientAddr, &clientAddrLen);
    if (clientFd == -1) {
        std::cerr << "Failed to accept connection" << std::endl;
    }
    int flags = fcntl(clientFd, F_GETFL, 0);
    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

    #if 0
    std::shared_ptr<Session> session = std::make_shared<HttpHlsSession>();
    elif 0
    std::shared_ptr<Session> session = std::make_shared<HttpFlvSession>();
    #else
    std::shared_ptr<Session> session = std::make_shared<RtmpSession>();
    session->setEpollServer(this);
    #endif
    TcpConnection*conn = new TcpConnection(clientFd, this, session);
    connections_[clientFd] = conn;
    epoll_event event{};
    event.events = EPOLLIN | EPOLLET; // 边缘触发
    event.data.fd = clientFd;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, clientFd, &event) == -1) {
        std::cerr << "Failed to add client socket to epoll" << std::endl;
        close(clientFd);
    }
}

void EpollServer::handleClient(int clientFd,epoll_event event) {
    TcpConnection* conn = connections_[clientFd];
   if(event.events & EPOLLIN) {
        conn->handldRead();
    }
    if(event.events & EPOLLOUT) {
        conn->handleWrite();
    }
}

EpollServer::~EpollServer() {
    close(listenFd_);
    close(epollFd_);
    for (auto& pair : connections_) {
        delete pair.second;
    }
}   