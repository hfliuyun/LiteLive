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
