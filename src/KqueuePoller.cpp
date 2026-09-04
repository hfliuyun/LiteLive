#include "KqueuePoller.h"
#include "Poller.h"
#include <memory>
#include <unistd.h>
KqueuePoller::KqueuePoller() : kqueue_fd_(kqueue()) {}
KqueuePoller::~KqueuePoller() {
    if (kqueue_fd_ >= 0) {
        close(kqueue_fd_);
    }
}

bool KqueuePoller::Add(int fd, EventMask events, TriggerMode mode)  {
    if(fd < 0) {
        return false;
    }
    if(fd_states_.find(fd) != fd_states_.end()) {
        return false; // Already added
    }
    
    //严格过滤 Report-Only 位（只关注 Readable 和 Writable） ──
    EventMask interest = events & (EventMask::Readable | EventMask::Writable);
    //过滤后如果为空（如只传了 Hangup），返回 false，不建表项 ──
    if (interest == EventMask::None) {
        return false;
    }
    struct kevent change;
    uint16_t flags = EV_ADD | (mode == TriggerMode::Edge ? EV_CLEAR : 0);
    bool readAdded = false;
    if(hasEvent(interest, EventMask::Readable)) {
        EV_SET(&change, fd, EVFILT_READ, flags, 0, 0, nullptr);
        if (kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr) == -1) {
            perror("kevent add read");
            return false;
        }
        readAdded = true;
    }
    if(hasEvent(interest, EventMask::Writable)) {
        EV_SET(&change, fd, EVFILT_WRITE, flags, 0, 0, nullptr);
        if (kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr) == -1) {
            perror("kevent add write");
            // 回滚保护：如果写失败，必须把已挂载成功的读 filter 从内核撤销！
            if (readAdded) {
                EV_SET(&change, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
                kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr);
            }
            return false;
        }
    }
    fd_states_[fd] = {interest, mode}; // Store the state of the fd
    return true;
}

bool KqueuePoller::Remove(int fd) {
    auto it = fd_states_.find(fd);
    if(it == fd_states_.end()) {
        return true; // fd not tracked, consider it removed
    }
    bool success = true;
    struct kevent change;
    if(hasEvent(it->second.events, EventMask::Readable)) {
        EV_SET(&change, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        if (kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr) == -1 ) {
            if (errno == ENOENT) {
                // S7 告警：表里有但内核没了，证明上层在 Remove 之前把 fd 提前 close 了！
                fprintf(stderr, "[WARN] KqueuePoller::Remove fd=%d read knote vanished (ENOENT), likely closed before Remove\n", fd);
            } else {
                perror("KqueuePoller::Remove read");
                success = false;
            }
        }
    }
    if(hasEvent(it->second.events, EventMask::Writable)) {
        EV_SET(&change, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        if (kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr) == -1 ) {
            if (errno == ENOENT) {
                // S7 告警：表里有但内核没了，证明上层在 Remove 之前把 fd 提前 close 了！
                fprintf(stderr, "[WARN] KqueuePoller::Remove fd=%d write knote vanished (ENOENT), likely closed before Remove\n", fd);
            } else {
                perror("KqueuePoller::Remove write");
                success = false;
            }
        }
        
    }
    fd_states_.erase(it); // Remove the state of the fd
    return success;
}

int KqueuePoller::Wait(int timeoutMs, std::vector<ReadyEvent>& events) {
    events.clear();
    struct timespec ts;
    struct timespec* ts_ptr = nullptr;
    if(timeoutMs == 0) {
        ts = {0, 0}; // Non-blocking
        ts_ptr = &ts;
    } else if(timeoutMs > 0) {
        ts.tv_sec = timeoutMs / 1000;
        ts.tv_nsec = (timeoutMs % 1000) * 1000000;
        ts_ptr = &ts;
    }
    
    constexpr int kMaxEvents = 16;
    constexpr int kKeventBufferSize = kMaxEvents * 2;
    struct kevent keventList[kKeventBufferSize];

    int nev = 0;
    while(true) {
        nev = kevent(kqueue_fd_, nullptr, 0, keventList, kKeventBufferSize, ts_ptr);
        if(nev == -1) {
            if(errno == EINTR) {
                continue; // Retry on interrupt
            } else {
                perror("kevent wait");
                return -1;
            }
        }
        break;
    }
    if (nev >0) {
        for(int i = 0; i < nev; i++) {
            const struct kevent& kev = keventList[i];
            int fd = static_cast<int>(kev.ident);
            EventMask mask = EventMask::None;

            if(kev.flags & EV_ERROR) {
                mask |= EventMask::Error;
            } else {
                if(kev.flags & EV_EOF) {
                    mask |= EventMask::Hangup;
                    if(kev.fflags != 0) { //置 EV_EOF 且 fflags!=0 为 Socket 运行时错误
                        mask |= EventMask::Error;
                    }
                }
                if(kev.filter == EVFILT_READ) {
                    mask |= EventMask::Readable;
                } else if(kev.filter == EVFILT_WRITE) {
                    mask |= EventMask::Writable;
                }
            }

            bool found = false;
            for(auto& event : events) {
                if(event.fd == fd) {
                    event.mask |= mask;
                    found = true;
                    break;
                }
            }
            if(!found) {
                events.push_back({fd, mask});
            }
        }
    }
    return static_cast<int>(events.size());
}

bool KqueuePoller::EnableWrite(int fd)  {
    auto it = fd_states_.find(fd);
    if(it == fd_states_.end()) {
        return false; // fd not tracked
    }
    if(hasEvent(it->second.events, EventMask::Writable)) {
        return true; // Already enabled
    }
    struct kevent change;
    EV_SET(&change, fd, EVFILT_WRITE, EV_ADD | (it->second.mode == TriggerMode::Edge ? EV_CLEAR : 0), 0, 0, nullptr);
    if (kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent enable write");
        return false;
    }
    it->second.events |= EventMask::Writable; // Update the state of the fd
    return true;
}
bool KqueuePoller::DisableWrite(int fd) {
    auto it = fd_states_.find(fd);
    if(it == fd_states_.end() || !hasEvent(it->second.events, EventMask::Writable)) {
        return true; // Already disabled or not tracked
    }
    struct kevent change;
    EV_SET(&change, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    if (kevent(kqueue_fd_, &change, 1, nullptr, 0, nullptr) == -1) {
        // 依据 D1：若报 ENOENT 说明内核已经没有这个 knote，契约目标达成，仍视为成功
        if (errno == ENOENT) {
            it->second.events &= ~EventMask::Writable;
            return true;
        }
        perror("KqueuePoller::DisableWrite kevent");
        return false;
    }
    it->second.events &= ~EventMask::Writable; // Update the state of the fd
    return true;
}

std::unique_ptr<Poller> CreatePoller() {
    std::unique_ptr<KqueuePoller> p = std::make_unique<KqueuePoller>();
    if (!p->isValid()) {
        return nullptr;
    }
    return p;
}