#ifndef KQUEUE_POLLER_H_
#define KQUEUE_POLLER_H_
#include "Poller.h"
#include <sys/event.h>
#include <unordered_map>

class KqueuePoller : public Poller {
public:
    KqueuePoller();
    ~KqueuePoller() override;
    bool isValid() const { return kqueue_fd_ >= 0; }
    bool Add(int fd, EventMask events, TriggerMode mode) override;
    bool Remove(int fd) override;
    int Wait(int timeoutMs, std::vector<ReadyEvent>& events) override;

    bool EnableWrite(int fd) override;
    bool DisableWrite(int fd) override;
private:
    int kqueue_fd_ = -1;
    struct FdState {
        EventMask events;
        TriggerMode mode;
    };
    std::unordered_map<int, FdState> fd_states_;
};
#endif // KQUEUE_POLLER_H_