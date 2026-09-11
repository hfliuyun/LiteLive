// CHECK：不受 NDEBUG 影响，报错带文件行号，失败后继续跑
#include <sys/socket.h>
#include <fcntl.h>
#include <iostream>
static int g_fails = 0;
#define CHECK(cond) \
    do{ if (!(cond)) { \
    std::cerr << __FILE__ << ":" << __LINE__ << " CHECK 失败: " #cond "\n"; \
    ++g_fails; } } while(0)
// socketpair + 非阻塞：Poller 测试的标准夹具
static bool makePair(int fds[2]) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return false;
    for(int i = 0; i < 2; ++i) {
        fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL, 0) | O_NONBLOCK);
    }
    return true;
}
