#include "Poller.h"
#include <cassert>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

int main() {
    std::unique_ptr<Poller> poller = CreatePoller();
    if (!poller) {
        std::cerr << "Failed to create Poller instance." << std::endl;
        return 1;
    }
    std::cout << "Poller instance created successfully." << std::endl;

    // 契约级测试：Remove 之后（不调 close），Wait 绝不应再返回事件
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);

    assert(poller->Add(fds[0], EventMask::Readable, TriggerMode::Edge) == true);
    assert(write(fds[1], "x", 1) == 1);

    std::vector<ReadyEvent> events;
    int nev = poller->Wait(100, events);
    assert(nev == 1 && events[0].fd == fds[0]);
    std::cout << "[Contract Check 1] Event triggered before Remove (nev=1)" << std::endl;

    // 关键照妖镜：只调 Remove，绝不调 close！
    assert(poller->Remove(fds[0]) == true);

    nev = poller->Wait(50, events);
    if (nev != 0) {
        std::cerr << "❌ 契约失败！Remove 之后未 close 依然产生了事件，nev=" << nev << std::endl;
        return 1;
    }
    std::cout << "[Contract Check 2] Remove without close returned nev=0! Contract passed. ✅" << std::endl;

    close(fds[0]);
    close(fds[1]);
    return 0;
}
