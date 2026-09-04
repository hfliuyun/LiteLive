// 只依赖 Poller.h 的 echo server：用来把 KqueuePoller 和「迁移 EpollServer」分开验证。
// 故意不 include TcpConnection.h / RtmpSession.h。
#include "Poller.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

// 每条连接的用户态状态。
// peerClosed 必须自己记：E5b 实测证明 EV_CLEAR 下 EV_EOF 不粘滞，
// 「等下一个 Hangup 事件再关」是等不到的。
struct Conn {
    std::string out;          // 还没写出去的回显数据
    bool peerClosed = false;  // 已收到对端 FIN
};
using ConnMap = std::unordered_map<int, Conn>;

bool setNonBlocking(int fd) {
    if (fd < 0) return false;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
        return false;
    }
    return true;
}

// Remove 必须在 close 之前：RFC 0.7 #4 那个 bug 的正确顺序。
static void closeConn(Poller& poller, ConnMap& conns, int fd) {
    poller.Remove(fd);
    close(fd);
    conns.erase(fd);
}

// ET 铁律：一次通知就是唯一的通知，必须读到 EAGAIN。
// 返回 false 表示连接真的出错了，调用方应关闭。
static bool drainRead(Conn& c, int fd) {
    char buffer[1024];
    while (true) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            c.out.append(buffer, n);
            continue;
        }
        if (n == 0) {
            c.peerClosed = true;  // 只标记，不在这里关：out 里可能还有没发完的回显
            return true;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;  // 读干了，正常出口
        if (errno == EINTR) continue;
        perror("read");
        return false;
    }
}

// Write-on-demand：能写多少写多少，写不完就开启写关注，写干净就立刻注销。
// 返回 false 表示连接真的出错了（EPIPE / ECONNRESET 等），调用方应关闭。
static bool flushWrite(Poller& poller, Conn& c, int fd) {
    while (!c.out.empty()) {
        ssize_t n = write(fd, c.out.data(), c.out.size());
        if (n > 0) {
            c.out.erase(0, n);  // short write 是常态，继续写
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            poller.EnableWrite(fd);  // 背压：腾出空间再叫我
            return true;
        }
        if (errno == EINTR) continue;
        perror("write");  // EAGAIN 之外的 -1 才是真错误，不能一律当背压
        return false;
    }
    poller.DisableWrite(fd);  // E3：写关注绝不常驻，抽干立刻摘掉
    return true;
}

// 启动自检：把 Poller 契约里三条最关键的条款变成「被违反时必须失败」的检查。
// 这三条 echo server 的行为测试全都照不出来——例如 Remove 什么都不做时，
// 紧跟其后的 close() 会让内核自动清掉 knote，把故障完全遮住。
static bool selfTest() {
    auto poller = CreatePoller();
    if (!poller) {
        fprintf(stderr, "[selfTest] CreatePoller 返回 nullptr\n");
        return false;
    }
    std::vector<ReadyEvent> events;
    bool ok = true;

    // 1. RFC:573 —— Remove 之后（不 close）wait 绝不能再产生该 fd 的事件
    int a[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, a) == 0) {
        poller->Add(a[0], EventMask::Readable, TriggerMode::Level);
        if (write(a[1], "x", 1) != 1) ok = false;
        if (poller->Wait(0, events) != 1) {
            fprintf(stderr, "[selfTest] 1a 注册后应报 1 个事件\n");
            ok = false;
        }
        poller->Remove(a[0]);  // 故意不 close
        if (poller->Wait(0, events) != 0) {
            fprintf(stderr, "[selfTest] 1b Remove 之后仍报事件，后置条件被违反\n");
            ok = false;
        }
        close(a[0]);
        close(a[1]);
    }

    // 2. RFC:576 + E4 —— DisableWrite 幂等（内核对删不存在的 filter 报 ENOENT，不幂等）
    int b[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, b) == 0) {
        poller->Add(b[0], EventMask::Readable, TriggerMode::Edge);
        if (!poller->DisableWrite(b[0]) || !poller->DisableWrite(b[0])) {
            fprintf(stderr, "[selfTest] 2 DisableWrite 不幂等\n");
            ok = false;
        }
        poller->Remove(b[0]);
        close(b[0]);
        close(b[1]);
    }

    // 3. E2 —— 一个 fd 两个 filter 会返回两条 kevent，Wait 必须合并成一条
    int c[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, c) == 0) {
        poller->Add(c[0], EventMask::Readable | EventMask::Writable, TriggerMode::Edge);
        if (write(c[1], "x", 1) != 1) ok = false;
        int n = poller->Wait(0, events);
        bool merged = (n == 1) && (events.size() == 1) &&
                      hasEvent(events[0].mask, EventMask::Readable) &&
                      hasEvent(events[0].mask, EventMask::Writable);
        if (!merged) {
            fprintf(stderr, "[selfTest] 3 同 fd 的读写事件没有合并成一条（n=%d）\n", n);
            ok = false;
        }
        poller->Remove(c[0]);
        close(c[0]);
        close(c[1]);
    }

    printf("[selfTest] %s\n", ok ? "契约检查全部通过" : "契约检查失败");
    return ok;
}

int main() {
    // 写一个对端已经发过 RST 的 socket 会收到 SIGPIPE，默认动作是直接终止进程。
    // 放在组合根（main）而不是 Poller 里：换 epoll/kqueue 这行不用跟着走，
    // 所以它不属于 Poller。（真实服务器该放哪层仍是 D2 里待定的设计题。）
    signal(SIGPIPE, SIG_IGN);

    if (!selfTest()) return 1;

    int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        perror("socket");
        return 1;
    }
    int socket_opt = 1;
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &socket_opt, sizeof(socket_opt)) == -1) {
        perror("setsockopt(SO_REUSEADDR)");
        close(listener_fd);
        return 1;
    }
    if (!setNonBlocking(listener_fd)) {
        close(listener_fd);
        return 1;
    }
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);
    if (bind(listener_fd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind");
        close(listener_fd);
        return 1;
    }
    if (listen(listener_fd, 5) < 0) {
        perror("listen");
        close(listener_fd);
        return 1;
    }

    std::unique_ptr<Poller> poller = CreatePoller();
    if (!poller) {
        fprintf(stderr, "CreatePoller 失败\n");
        close(listener_fd);
        return 1;
    }
    // 监听 socket 用 Level：accept 漏一次不会丢连接，和 RFC 的 accept 决策一致。
    if (!poller->Add(listener_fd, EventMask::Readable, TriggerMode::Level)) {
        fprintf(stderr, "Add(listener_fd) 失败\n");
        close(listener_fd);
        return 1;
    }
    printf("echo server 监听 8080\n");
    fflush(stdout);

    ConnMap conns;
    std::vector<ReadyEvent> events;
    while (true) {
        int numEvents = poller->Wait(-1, events);
        if (numEvents < 0) {
            fprintf(stderr, "Wait 返回 -1，按契约视为致命故障，退出事件循环\n");
            break;
        }
        for (const auto& event : events) {
            if (event.fd == listener_fd) {
                while (true) {  // accept 循环到 EAGAIN
                    sockaddr_in clientAddr{};
                    socklen_t clientLen = sizeof(clientAddr);
                    int client_fd = accept(listener_fd, (struct sockaddr*)&clientAddr, &clientLen);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR || errno == ECONNABORTED) continue;
                        perror("accept");
                        break;
                    }
                    if (!setNonBlocking(client_fd)) {
                        close(client_fd);
                        continue;
                    }
                    // 客户端 fd 用 Edge：收成表 #3，配合 drainRead 的抽干循环。
                    if (!poller->Add(client_fd, EventMask::Readable, TriggerMode::Edge)) {
                        fprintf(stderr, "Add(client_fd=%d) 失败，直接关闭\n", client_fd);
                        close(client_fd);
                        continue;
                    }
                    conns[client_fd];  // 建立连接状态
                }
                continue;
            }

            auto it = conns.find(event.fd);
            if (it == conns.end()) continue;  // 本轮更早的事件已经把它关掉了
            Conn& c = it->second;

            if (hasEvent(event.mask, EventMask::Error)) {
                closeConn(*poller, conns, event.fd);
                continue;
            }

            bool alive = true;
            if (hasEvent(event.mask, EventMask::Readable)) {
                alive = drainRead(c, event.fd);
            }
            // Hangup 只记标记，真正的关闭统一交给下面的「积压清空」判断。
            if (alive && hasEvent(event.mask, EventMask::Hangup)) {
                c.peerClosed = true;
            }
            if (alive) {
                alive = flushWrite(*poller, c, event.fd);
            }
            if (!alive) {
                closeConn(*poller, conns, event.fd);
                continue;
            }
            // 优雅关闭：对端已走 + 积压已发完，才真正关。
            if (c.peerClosed && c.out.empty()) {
                closeConn(*poller, conns, event.fd);
            }
        }
    }

    for (auto& kv : conns) close(kv.first);
    close(listener_fd);
    return 0;
}
