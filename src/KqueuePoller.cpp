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
    // Implementation for adding file descriptor
    return true;
}

bool KqueuePoller::Remove(int fd) {
    // Implementation for removing file descriptor
    return true;
}

int KqueuePoller::Wait(int timeoutMs, std::vector<ReadyEvent>& events) {
    // Implementation for waiting on events
    return 0;
}

bool KqueuePoller::EnableWrite(int fd)  {
    // Implementation for enabling write event
    return true;
}
bool KqueuePoller::DisableWrite(int fd) {
    // Implementation for disabling write event
    return true;
}

std::unique_ptr<Poller> CreatePoller() {
    std::unique_ptr<KqueuePoller> p = std::make_unique<KqueuePoller>();
    if (!p->isValid()) {
        return nullptr;
    }
    return p;
}