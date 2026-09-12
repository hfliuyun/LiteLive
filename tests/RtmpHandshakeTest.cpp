// RtmpHandshakeTest —— Day 6 增量握手
//
// 接缝盘点：今天只加一处真接缝。
//   输入侧不需要接缝 —— onMessage(conn, std::string&) 的第二个参数就是缓冲区本身，
//   测试自己持有 std::string 后直接调用，断言里的 readBuffer.size() 就是 buf.size()。
//   输出侧不需要接缝 —— conn->send() 写进 socketpair 的 fds[0]，测试从对端 fds[1]
//   read 出来（drainPeer），对端 fd 本身就是观察点。
//   状态侧是唯一真接缝 —— RtmpSession::handshakeState()，LITE_LIVE_TESTING 下只读。
//
// 显式非目标：RtmpSession 不校验 C0 的版本内容。C0=0x04 这类非法版本在
// TcpConnection.cpp:26 的协议嗅探处就被路由成 HttpFlvSession，Session 层永远看不到，
// 校验职责在路由层不在会话层 —— 因此这里没有"C0 非法"的用例。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

#include "LiveServer.h"
#include "RtmpSession.h"
#include "TcpConnection.h"
#include "utils.h"

// Adobe RTMP 1.0 §5.2.2–5.2.4：C0/S0 各 1 字节，C1/S1/C2/S2 各 1536 字节
static constexpr size_t kC1Size = 1536;
static constexpr size_t kC0C1Size = 1537;                     // C0 + C1
static constexpr size_t kC2Size = 1536;
static constexpr size_t kFullSize = kC0C1Size + kC2Size;      // 3073
static constexpr size_t kS0S1S2Size = 1 + 1536 + 1536;        // 3073

// 长度是 off-by-one 高发区，编译期钉死，别等红灯才发现算错
static_assert(kC0C1Size == 1537, "C0+C1 必须恰好 1537 字节");
static_assert(kFullSize == 3073, "C0C1+C2 必须恰好 3073 字节，不是 3072");
static_assert(kS0S1S2Size == 3073, "S0+S1+S2 必须恰好 3073 字节");

// 构造 1536 字节的 C1/S1：time(4, 大端) + zero(4) + random(1528)
static std::string makeC1(uint32_t timestamp, uint8_t randomFill) {
    std::string c1(kC1Size, '\0');
    c1[0] = static_cast<char>(timestamp >> 24);
    c1[1] = static_cast<char>(timestamp >> 16);
    c1[2] = static_cast<char>(timestamp >> 8);
    c1[3] = static_cast<char>(timestamp);
    for (size_t i = 8; i < c1.size(); ++i)
        c1[i] = static_cast<char>(randomFill);
    return c1;
}

// C1 带可辨认的时间戳和填充值，方便逐字节比对
static const std::string kC1 = makeC1(0x11223344u, 0xA5);
// LiteLive 的 S1 是全 0（RtmpSession.cpp:50），simple 模式下 C2 回显 S1
static const std::string kC2(kC2Size, '\0');
static const std::string kC0C1 = std::string(1, static_cast<char>(0x03)) + kC1;

struct HsCtx {
    std::unique_ptr<LiveServer> server;   // LiveServer 声明了析构函数，既不可拷贝也不可移动
    int fds[2] = {-1, -1};
    std::shared_ptr<RtmpSession> session;
    std::unique_ptr<TcpConnection> conn;
};

static HsCtx makeCtx() {
    HsCtx ctx;
    ctx.server = std::make_unique<LiveServer>(0);
    CHECK(makePair(ctx.fds));
    // fds[0] 必须先注册：handleWrite 在 writeBuffer_ 清空后会调 DisableWrite(fd_)
    // （TcpConnection.cpp:72），不该对未注册的 fd 操作
    CHECK(ctx.server->poller()->Add(ctx.fds[0], EventMask::Readable, TriggerMode::Edge));

    ctx.session = std::make_shared<RtmpSession>();
    ctx.session->setLiveServer(ctx.server.get());
    ctx.conn = std::make_unique<TcpConnection>(ctx.fds[0], ctx.server.get(), ctx.session);
    // onCloseCb 默认是空 std::function（TcpConnection.h:20），而 TcpConnection.cpp:83
    // 无条件调用它 → 一旦触发 CloseConnection 就 bad_function_call 直接崩，
    // 测试会看不到红灯而当场死掉
    ctx.conn->setOnCloseCb([](int) {});
    return ctx;
}

// 输出侧唯一观察点：把对端 socket 里已就绪的字节全部读走
static std::string drainPeer(int fd) {
    std::string out;
    char buf[4096];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) out.append(buf, static_cast<size_t>(n));
        else break;   // EAGAIN 或对端已关闭：都没有更多字节
    }
    return out;
}

// 用例①：一次性喂入完整 3073 字节（C0C1+C2 粘在同一个缓冲区里）
static void testCase1WholePacket() {
    std::string message  = kC0C1 + kC2;
    auto ctx = makeCtx();
    ctx.session->onMessage(ctx.conn.get(), message);
    CHECK(ctx.session->handshakeState() == RtmpSession::STATE_HANDSHAKE_DONE);
    CHECK(message.size() == 0);
    const std::string sent = drainPeer(ctx.fds[1]);
    CHECK(sent.size() == kS0S1S2Size);
    CHECK(static_cast<uint8_t>(sent[0]) == 0x03);
}

// 用例②：逐字节喂入，一共 3073 次
static void testCase2OneByteAtATime() {
    auto ctx = makeCtx();
    std::string message {};
    for(size_t i =0 ; i <kC0C1Size; i++) {
        message.push_back(kC0C1[i]);
        ctx.session->onMessage(ctx.conn.get(),message);
    }
    CHECK(ctx.session->handshakeState() == RtmpSession::STATE_WAIT_C2);
    CHECK(message.size() == 0);
    const std::string sent = drainPeer(ctx.fds[1]);
    CHECK(sent.size() == kS0S1S2Size);
    CHECK(static_cast<uint8_t>(sent[0]) == 0x03);

    for(size_t i =0 ; i <kC2Size; i++) {
        message.push_back(kC2[i]);
        ctx.session->onMessage(ctx.conn.get(),message);
    }
    CHECK(ctx.session->handshakeState() == RtmpSession::STATE_HANDSHAKE_DONE);
    CHECK(message.size() == 0);

    CHECK(drainPeer(ctx.fds[1]).empty());
}

// 用例③：C0C1 完整，C2 只来 500 字节
static void testCase3PartialC2() {
    std::string message = kC0C1 + kC2.substr(0, 500);
    auto ctx = makeCtx();
    ctx.session->onMessage(ctx.conn.get(), message);
    CHECK(ctx.session->handshakeState() == RtmpSession::STATE_WAIT_C2);
    CHECK(message.size() == 500);
    CHECK(message == kC2.substr(0, 500));
    const std::string sent = drainPeer(ctx.fds[1]);
    CHECK(sent.size() == kS0S1S2Size);
    CHECK(static_cast<uint8_t>(sent[0]) == 0x03);

    message.append(kC2,500,kC2Size -500);
    ctx.session->onMessage(ctx.conn.get(), message);
    CHECK(message.empty());
    CHECK(ctx.session->handshakeState() == RtmpSession::STATE_HANDSHAKE_DONE);

}

// 用例⑤：3073 字节握手 + 追加一个"不完整 chunk"粘在后面
// （追加的 chunk 故意只给 basic header、消息头没到 —— 这样期望值才可精确确定）
static void testCase5HandshakePlusPartialChunk() {
    std::string message = kC0C1 + kC2;
    const std::string partialChunk = {static_cast<char>(0x01), 0x00, 0x00, 0x00};
    message.append(partialChunk);
    auto ctx = makeCtx();
    ctx.session->onMessage(ctx.conn.get(), message);
    CHECK(ctx.session->handshakeState() == RtmpSession::STATE_HANDSHAKE_DONE);
    CHECK(message.size() == 4);
    CHECK(message == partialChunk);
    const std::string sent = drainPeer(ctx.fds[1]);
    CHECK(sent.size() == kS0S1S2Size);
    CHECK(static_cast<uint8_t>(sent[0]) == 0x03);

}

int main() {
    testCase1WholePacket();
    testCase2OneByteAtATime();
    testCase3PartialC2();
    testCase5HandshakePlusPartialChunk();

    if (g_fails == 0) { std::cout << "RtmpHandshake 测试全部通过\n"; return 0; }
    std::cerr << g_fails << " 条契约检查失败\n";
    return 1;
}
