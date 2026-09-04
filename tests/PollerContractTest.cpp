#include "Poller.h"
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// CHECK：不受 NDEBUG 影响，报错带文件行号，失败后继续跑
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


// Case 1：Remove 之后绝不再报该 fd 的事件

static void testRemoveThenSilent() {
    std::unique_ptr<Poller> poller = CreatePoller();
    CHECK(poller);
    int fds[2];
    CHECK(makePair(fds));
    CHECK(poller->Add(fds[0], EventMask::Readable, TriggerMode::Level));
    CHECK(write(fds[1], "x", 1) == 1);
    std::vector<ReadyEvent> events;
    CHECK(poller->Wait(0, events) == 1);

    CHECK(poller->Remove(fds[0]));
    CHECK(poller->Wait(0, events) == 0);
    CHECK(poller->Remove(fds[0]));
    close(fds[0]); close(fds[1]);
}

// Case 2：DisableWrite 必须幂等（内核对"删不存在的 filter"报 ENOENT，不保护就返回 false）
static void testDisableWriteIdempotent() {
    std::unique_ptr<Poller> poller = CreatePoller();
    CHECK(poller);
    int fds[2]; CHECK(makePair(fds));
    CHECK(poller->Add(fds[0], EventMask::Readable, TriggerMode::Edge));
    CHECK(poller->DisableWrite(fds[0]));           // 从未开启过 Writable，第一次也必须成功
    CHECK(poller->DisableWrite(fds[0]));           // 连续调用也不能失败
    CHECK(poller->DisableWrite(fds[0]));
    poller->Remove(fds[0]);
    close(fds[0]); close(fds[1]);
}

// Case 3：对端发 FIN，本端应报 HalfClose = Readable|Hangup
// 用 Level 而非 Edge：kqueue 的 EV_EOF 在 EV_CLEAR 下不粘滞（见 PollerEchoTest.cpp:15），Edge 会漏报
// nev 该是多少？mask 里该有哪两个位？ nev是1 mask里有Hangup 和Readable
static void testHalfCloseReportsHangup() {
    std::unique_ptr<Poller> poller = CreatePoller();
    CHECK(poller);
    int fds[2]; CHECK(makePair(fds));
    CHECK(poller->Add(fds[0], EventMask::Readable, TriggerMode::Level));
    CHECK(shutdown(fds[1], SHUT_WR) == 0);         // act: 对端关写端，发 FIN

    std::vector<ReadyEvent> events;
    int nev = poller->Wait(0, events);
    CHECK(nev == 1);
    for(auto ev:events) {
        CHECK(hasEvent(ev.mask, EventMask::Hangup));
        CHECK(hasEvent(ev.mask, EventMask::Readable));
        CHECK(!hasEvent(ev.mask, EventMask::Writable));
        CHECK(!hasEvent(ev.mask, EventMask::Error));
    }
    poller->Remove(fds[0]);
    close(fds[0]); close(fds[1]);
}

// Case 4：EnableWrite / DisableWrite 往返：写关注出现又消失，读关注全程不受影响
// 用 Level：kqueue 的 EV_CLEAR 会吞掉"注册时已可写"的状态，Edge 下 Writable 不会立刻报
// 任务：补第 1、2 次 Wait 的断言（Writable 出现 → 消失，Readable 两次都在）
static void testEnableWriteRoundTrip() {
    std::unique_ptr<Poller> poller = CreatePoller();
    CHECK(poller);
    int fds[2]; CHECK(makePair(fds));
    CHECK(poller->Add(fds[0], EventMask::Readable, TriggerMode::Level));
    CHECK(write(fds[1], "x", 1) == 1);             // 先让 fds[0] 可读，第 2 次才谈得上"Readable 仍在"
    CHECK(poller->EnableWrite(fds[0]));            // 开启写关注

    std::vector<ReadyEvent> events;
    int nev = poller->Wait(0, events);             // 第 1 次：Writable 应出现
    CHECK(nev == 1);
    
    for(auto ev:events) {
        CHECK(!hasEvent(ev.mask, EventMask::Hangup));
        CHECK(!hasEvent(ev.mask, EventMask::Error));
        CHECK(hasEvent(ev.mask, EventMask::Readable));
        CHECK(hasEvent(ev.mask, EventMask::Writable));
    }


    CHECK(poller->DisableWrite(fds[0]));           // 关闭写关注
    nev = poller->Wait(0, events);                 // 第 2 次：Writable 消失，Readable 仍在
    CHECK(nev == 1);
    for(auto ev:events) {
        CHECK(!hasEvent(ev.mask, EventMask::Hangup));
        CHECK(!hasEvent(ev.mask, EventMask::Error));
        CHECK(!hasEvent(ev.mask, EventMask::Writable));
        CHECK(hasEvent(ev.mask, EventMask::Readable));
    }
    poller->Remove(fds[0]);
    close(fds[0]); close(fds[1]);
}
int main() {
    testRemoveThenSilent();
    testDisableWriteIdempotent();
    testHalfCloseReportsHangup();
    testEnableWriteRoundTrip();
    if (g_fails == 0) { std::cout << "Poller 契约测试全部通过\n"; return 0; }
    std::cerr << g_fails << " 条契约检查失败\n";
    return 1;
}