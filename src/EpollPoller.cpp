#include "EpollPoller.h"
#include "Poller.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>


namespace  {
    std::uint32_t buildEpollEvent(EventMask mask, TriggerMode mode) {
        std::uint32_t event = 0;
        if(hasEvent(mask, EventMask::Readable)) {
            event |= EPOLLIN;
        }
        if(hasEvent(mask, EventMask::Writable)) {
            event |= EPOLLOUT;
        }
        if(mode == TriggerMode::Edge) {
            event |= EPOLLET;
        }

        event |= EPOLLRDHUP;
        return event;
    }
}

EpollPoller::EpollPoller() : epollFd_(epoll_create1(EPOLL_CLOEXEC)) {}

EpollPoller::~EpollPoller() {
    if (epollFd_ >= 0) {
        close(epollFd_);
    }
}

bool EpollPoller::Add(int fd, EventMask events, TriggerMode mode)  {
    if(fd < 0) {
        return false;
    }
    if(fd_states_.find(fd) != fd_states_.end()) {
        // Already added
        return false;
    }
    EventMask interest = events & (EventMask::Readable | EventMask::Writable);
    if (interest == EventMask::None) {
        return false;
    }
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = buildEpollEvent(interest, mode);
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        return false;
    }
    fd_states_[fd] = {interest, mode}; // Store the state of the added file descriptor
    return true;
}

bool EpollPoller::Remove(int fd) {
    if(fd < 0) {
        return false;
    }
    auto it = fd_states_.find(fd);
    if (it == fd_states_.end()) {
        // Not found
        return true;
    }
    bool success = true;
    if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        if(errno == ENOENT) {
            fprintf(stderr, "[WARN] EpollPoller::Remove fd=%d fd vanished (ENOENT), likely closed before Remove\n", fd);
        } else {
            perror("EpollPoller::Remove");
            success = false;
        }
    }
    fd_states_.erase(it); // Remove the state of the removed file descriptor
    return success;
}

int EpollPoller::Wait(int timeoutMs, std::vector<ReadyEvent>& events) {
    constexpr int kMaxEvents = 16;
    epoll_event evs[kMaxEvents];
    int nfds = 0;
    events.clear();
    while (true) {
        nfds = epoll_wait(epollFd_, evs, kMaxEvents, timeoutMs);
        if (nfds == -1) {
            if(errno == EINTR) {
                continue; // Interrupted by signal, retry
            } else {
                return -1; // Error occurred
            }
        }
        break; // Successfully got events
    }
    for (int i = 0; i < nfds; ++i) {
        int fd = evs[i].data.fd;
        EventMask mask = EventMask::None;
        if(evs[i].events & EPOLLERR) {
            mask |= EventMask::Error;
            int err = 0; socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);   
            fprintf(stderr, "[WARN] epoll fd=%d EPOLLERR SO_ERROR=%d (%s)\n", fd, err, strerror(err));
        }
        if(evs[i].events & (EPOLLHUP | EPOLLRDHUP)) {
            mask |= EventMask::Hangup;
        }
        if(evs[i].events & EPOLLIN) {
            mask |= EventMask::Readable;
        }
        if(evs[i].events & EPOLLOUT) {
            mask |= EventMask::Writable;
        }
        events.push_back({fd, mask});
    }
    return static_cast<int>(events.size());
}

bool EpollPoller::EnableWrite(int fd)  {
    if(fd < 0) {
        return false;
    }
    auto it = fd_states_.find(fd);
    if (it == fd_states_.end()) {
        // Not found
        return false;
    }
    if(hasEvent(it->second.events, EventMask::Writable)) {
        // Already enabled
        return true;
    }
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = buildEpollEvent(it->second.events | EventMask::Writable, it->second.mode);
    if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        return false;
    }
    it->second.events = it->second.events | EventMask::Writable; // Update the state to include writable
    return true;
}
bool EpollPoller::DisableWrite(int fd) {
    if(fd < 0) {
        return false;
    }
    auto it = fd_states_.find(fd);
    if (it == fd_states_.end()) {
        // Not found
        return false;
    }
    if(hasEvent(it->second.events, EventMask::Writable)) {
        epoll_event ev{};
        ev.data.fd = fd;
        ev.events = buildEpollEvent(it->second.events & ~EventMask::Writable, it->second.mode);
        if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
            if(errno == ENOENT) {
                it->second.events &= ~EventMask::Writable;
                return true;
            }
            return false;
        }
        it->second.events &= ~EventMask::Writable; // Update the state to remove writable
    }
    return true;
}

std::unique_ptr<Poller> CreatePoller() { 
    std::unique_ptr<EpollPoller> p = std::make_unique<EpollPoller>();
    if (!p->isValid()) {
        return nullptr;
    }
    return p;
}