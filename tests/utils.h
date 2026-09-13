// CHECK：不受 NDEBUG 影响，报错带文件行号，失败后继续跑
#ifndef __RTMP_TEST_UTILS
#define __RTMP_TEST_UTILS
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <fcntl.h>
#include <iostream>
#include "RtmpSession.h"
#include "TcpConnection.h"

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
// 构造 1 字节 Basic Header
static uint8_t makeBasicHeader(uint8_t fmt, uint8_t csid) {
    return static_cast<uint8_t>(fmt << 6| csid & 0x3f);
}
//构造 fmt0 的 11 字节 Message Header
static std::string makeMessageHeaderFmt0(uint32_t ts, uint32_t len, uint8_t type, uint32_t streamId) {
    std::string h;
    h.push_back(static_cast<char>((ts >> 16) & 0xFF));
    h.push_back(static_cast<char>((ts >> 8) & 0xFF));
    h.push_back(static_cast<char>(ts & 0xFF));
    h.push_back(static_cast<char>((len >> 16) & 0xFF));
    h.push_back(static_cast<char>((len >> 8) & 0xFF));
    h.push_back(static_cast<char>(len & 0xFF));
    h.push_back(static_cast<char>(type));
    // streamId 小端（当前代码读的是大端，这里先对齐写入侧 RtmpSession.cpp:730-734）
    h.push_back(static_cast<char>(streamId & 0xFF));
    h.push_back(static_cast<char>((streamId >> 8) & 0xFF));
    h.push_back(static_cast<char>((streamId >> 16) & 0xFF));
    h.push_back(static_cast<char>((streamId >> 24) & 0xFF));
    return h;
}

// 构造 fmt1 的 7 字节 Message Header
static std::string makeMsgHeaderFmt1(uint32_t delta, uint32_t len, uint8_t type) {
    std::string h;
    h.push_back(static_cast<char>((delta >> 16) & 0xFF));
    h.push_back(static_cast<char>((delta >> 8) & 0xFF));
    h.push_back(static_cast<char>(delta & 0xFF));
    h.push_back(static_cast<char>((len >> 16) & 0xFF));
    h.push_back(static_cast<char>((len >> 8) & 0xFF));
    h.push_back(static_cast<char>(len & 0xFF));
    h.push_back(static_cast<char>(type));
    return h;
}

// 构造 fmt2 的 3 字节 Message Header
static std::string makeMsgHeaderFmt2(uint32_t delta) {
    std::string h;
    h.push_back(static_cast<char>((delta >> 16) & 0xFF));
    h.push_back(static_cast<char>((delta >> 8) & 0xFF));
    h.push_back(static_cast<char>(delta & 0xFF));
    return h;
}

// 组装一个完整 chunk
static std::string makeChunk(uint8_t fmt, uint8_t csid, const std::string& msgHeader, const std::string& payload) {
    std::string chunk;
    chunk.push_back(static_cast<char>(makeBasicHeader(fmt, csid)));
    chunk.append(msgHeader);
    chunk.append(payload);
    return chunk;
}

#endif