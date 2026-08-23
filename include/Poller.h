#ifndef POLLER_H_
#define POLLER_H_
#include <cstdint>
#include <memory>
#include <vector>
enum class EventMask:uint8_t {
    None       = 0,
    Readable   = 1 << 0, //0x01: 可读 (epoll: EPOLLIN,   kqueue: EVFILT_READ)
    Writable   = 1 << 1, //0x02: 可写 (epoll: EPOLLOUT,  kqueue: EVFILT_WRITE)
    Error      = 1 << 2, //0x04: 错误 (epoll: EPOLLERR 内核自动上报,
                 //       kqueue: EVFILT_READ 置 EV_EOF 且 fflags!=0，errno 在 fflags)
    Hangup     = 1 << 3, //0x08: 挂断 (epoll: EPOLLRDHUP | EPOLLHUP, kqueue: EVFILT_READ + EV_EOF)
};

enum class TriggerMode:uint8_t {
    Level      = 0, //水平触发
    Edge       = 1, //边缘触发
};
struct ReadyEvent {
    int fd;
    EventMask mask;
};

inline constexpr EventMask operator|(EventMask a, EventMask b) {
    return static_cast<EventMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline constexpr EventMask operator&(EventMask a, EventMask b) {
    return static_cast<EventMask>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline constexpr EventMask operator~(EventMask a) {
    return static_cast<EventMask>(~static_cast<uint8_t>(a));
}
inline constexpr EventMask& operator|=(EventMask& a, EventMask b) {
    a = a | b;
    return a;
}
inline constexpr EventMask& operator&=(EventMask& a, EventMask b) {
    a = a & b;
    return a;
}

inline constexpr bool hasEvent(EventMask mask, EventMask event) {
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(event)) != 0;
}


class Poller {
public:
    virtual ~Poller() = default;

    virtual bool Add(int fd, EventMask events, TriggerMode mode) = 0;
    virtual bool Remove(int fd) = 0;
    virtual int Wait(int timeoutMs, std::vector<ReadyEvent>& events) = 0;

    
    virtual bool EnableWrite(int fd) = 0;
    virtual bool DisableWrite(int fd) = 0;
};

//工厂函数，用于创建适合当前平台的 Poller 实例
std::unique_ptr<Poller> CreatePoller();
#endif // POLLER_H_