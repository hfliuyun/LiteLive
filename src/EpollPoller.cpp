#include "EpollPoller.h"
#include <iostream>
#include <memory>
#include <unistd.h>
EpollPoller::EpollPoller() : epollFd_(epoll_create1(0)) {}

EpollPoller::~EpollPoller() {
    if (epollFd_ >= 0) {
        close(epollFd_);
    }
}

bool EpollPoller::Add(int fd, EventMask events, TriggerMode mode)  {
    // Implementation for adding file descriptor
    return true;
}

bool EpollPoller::Remove(int fd) {
    // Implementation for removing file descriptor
    return true;
}

int EpollPoller::Wait(int timeoutMs, std::vector<ReadyEvent>& events) {
    // Implementation for waiting on events
    return 0;
}

bool EpollPoller::EnableWrite(int fd)  {
    // Implementation for enabling write event
    return true;
}
bool EpollPoller::DisableWrite(int fd) {
    // Implementation for disabling write event
    return true;
}

std::unique_ptr<Poller> CreatePoller() { 
    std::unique_ptr<EpollPoller> p = std::make_unique<EpollPoller>();
    if (!p->isValid()) {
        return nullptr;
    }
    return p;
}