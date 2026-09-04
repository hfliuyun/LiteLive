#ifndef EPOLL_POLLER_H_
#define EPOLL_POLLER_H_
#include "Poller.h"
#include <sys/epoll.h>
#include <unordered_map>

class EpollPoller : public Poller {
public:
    EpollPoller();
    ~EpollPoller() override;

    bool isValid() const { return epollFd_ >= 0; }

    bool Add(int fd, EventMask events, TriggerMode mode) override;
    bool Remove(int fd) override;
    int Wait(int timeoutMs, std::vector<ReadyEvent>& events) override;
    
    
    bool EnableWrite(int fd) override;
    bool DisableWrite(int fd) override;

private:
    int epollFd_ = -1;
    struct FdState {
        EventMask events;
        TriggerMode mode;
    };
    std::unordered_map<int, FdState> fd_states_;//add 后置条件承诺的状态表
};
#endif // EPOLL_POLLER_H_