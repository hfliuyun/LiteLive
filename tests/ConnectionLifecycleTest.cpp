#include <algorithm>
#include <cstddef>
#include <fcntl.h>
#include "HttpFlvSession.h"
#include "LiveServer.h"
#include "Poller.h"
#include "RtmpSession.h"
#include "Session.h"
#include <cerrno>
#include <iostream>
#include <memory>
#include <ostream>
#include <unistd.h>
#include "utils.h"
#include "TcpConnection.h"
// fd 是否仍被内核持有：close 过 → F_GETFD 返回 -1 且 errno == EBADF
static bool fdOpen(int fd) {
    errno = 0;
    return fcntl(fd, F_GETFD) != -1;
}
static bool fdClose(int fd) {
    errno = 0;
    fcntl(fd, F_GETFD);
    return errno == EBADF;
}
// Session::onDisconnect 有默认空实现（Session.h:12），只需覆盖两个纯虚函数
class CountingSession : public Session {
public:
    int disconnectCalls = 0;
    void onMessage(TcpConnection*, std::string&) override {}  void setLiveServer(LiveServer*) override {}
    void onDisconnect(TcpConnection*) override { ++disconnectCalls; }
};

//Case 1：CloseConnection 幂等，且它本身不关 fd
static void testDoubleConnection() {
    int fds[2];CHECK(makePair(fds));
    auto session = std::make_shared<CountingSession>();
    int cb = 0;
    auto conn = std::make_unique<TcpConnection>(fds[0], nullptr, session);// nullptr 安全：CloseConnection 不解引用 liveServer_
    conn->setOnCloseCb([&cb](int) {++cb;});
    conn->CloseConnection();
    CHECK(conn->isClose());
    CHECK(session->disconnectCalls == 1);
    CHECK(cb ==1);
    CHECK(fdOpen(fds[0]));// 延迟销毁：CloseConnection 不 close(fd)
    
    conn->CloseConnection();
    CHECK(session->disconnectCalls == 1); // 不重复通知业务层
    CHECK(cb == 1);                       // 不重复排队 → 不会重复 close(fd)
    CHECK(fdOpen(fds[0]));
    close(fds[0]); close(fds[1]);
}

//Case 2：延迟销毁的批边界
static void testSweepThenErased() {
    LiveServer server(0);
    int fds[2];CHECK(makePair(fds));
    CHECK(server.poller()->Add(fds[0], EventMask::Readable, TriggerMode::Edge));// 仿 acceptConnection :99
    auto session  = std::make_shared<CountingSession>();
    auto conn = std::make_unique<TcpConnection>(fds[0],&server,session);
    TcpConnection* raw = conn.get();// 先存原始指针！move 之后 unique_ptr 就空了
    size_t before = server.connectionCount();
    CHECK(server.connectionCount() == 0);
    CHECK(server.hasConnection(fds[0]) == false);
    server.injectConnection(fds[0], std::move(conn));
    
    //先断 connectionCount 和 hasConnection，确认你真的装进去了
    CHECK(server.connectionCount() == 1);
    CHECK(server.hasConnection(fds[0]));
    raw->CloseConnection();                // act: 只置位 + 排队，不立刻删
    //第一段断言：hasConnection(fd) 为真、fd 仍开着。今天的核心，写不出来说明没懂批边界
    CHECK(server.connectionCount() == 1);
    CHECK(server.hasConnection(fds[0]));
    CHECK(fdOpen(fds[0]));
    server.sweepOnce();                    // act: 批结束，统一销毁
    //第二段断言：hasConnection 变假、count 回到 before、fd 已被 close
    CHECK(server.connectionCount() == before);
    CHECK(server.hasConnection(fds[0]) == false);
    CHECK(fdClose(fds[0]));
    close(fds[1]);                         // fds[0] 已被 sweep close 过，别再 close
}

// Case 3：主播断线，观众全被踢，流条目消失
static void testPublisherDisconnectKicksAll() {
    LiveServer server(0);
    int pub[2], rtmp[2], flv[2];
    CHECK(makePair(pub)); CHECK(makePair(rtmp)); CHECK(makePair(flv));
    // 主播：RtmpSession，isPublishing_ == true、streamName_ == "test"；观众：一个 RtmpSession + 一个 HttpFlvSession
    // 三个都要 setLiveServer(&server)，否则 onDisconnect 解引用空指针；三个 TcpConnection(fd, &server, session) 都必须 setOnCloseCb(...)，
    // 因为 onCloseCb 是默认构造的空 std::function（TcpConnection.h:21），而 TcpConnection.cpp:83 无条件调用它 → bad_function_call → 进程直接死
    // 摆流：g_liveStreams 是 public 的 → LiveStream s; s.publisher = 主播裸指针; s.subscribers = {rtmp观众裸指针};
    // s.flvSubscribers = {flv观众裸指针}; server.g_liveStreams["test"] = s;
    // act：调 conn->CloseConnection() 还是 session->onDisconnect(conn)？先想清楚两者的差别
    // TODO(作业) 断言：两个观众 isClose()、流条目被 erase，再加一条你自己想的
    //   第四条提示：在这个调用顺序下，主播自己的连接 isClose 是真是假？
    auto pushSession = std::make_shared<RtmpSession> ();
    pushSession->setPublish(true);
    pushSession->setStream("test");
    pushSession->setLiveServer(&server);
    auto pushConn = std::make_unique<TcpConnection>(pub[0],&server,pushSession);

    auto userRtmpSession = std::make_shared<RtmpSession> ();
    auto useRtmpConn = std::make_unique<TcpConnection>(rtmp[0],&server,userRtmpSession);
    userRtmpSession->setLiveServer(&server);

    auto userFlvSession = std::make_shared<HttpFlvSession> ();
    auto useFlvConn = std::make_unique<TcpConnection>(flv[0],&server,userFlvSession);
    userFlvSession->setLiveServer(&server);
    

    server.g_liveStreams["test"].publisher = pushConn.get();
    server.g_liveStreams["test"].subscribers.push_back(useRtmpConn.get());
    server.g_liveStreams["test"].flvSubscribers.push_back(useFlvConn.get());


    auto rawPush = pushConn.get();
    auto rawUseRtmp = useRtmpConn.get();
    auto rawUseFlv = useFlvConn.get();

    server.injectConnection(pub[0], std::move(pushConn));
    server.injectConnection(rtmp[0], std::move(useRtmpConn));
    server.injectConnection(flv[0], std::move(useFlvConn));
    rawPush->CloseConnection();
    // ← 安全窗口：对象还活着，close_ 已置位
    CHECK(rawPush->isClose());        // 主播自己关了
    CHECK(rawUseRtmp->isClose());     // RTMP 观众被踢了
    CHECK(rawUseFlv->isClose());      // FLV 观众被踢了
    server.sweepOnce();
    // ← 从这里开始 raw 指针全部悬空，不能再碰
    CHECK(fdClose(rtmp[0]));
    CHECK(fdClose(flv[0]));
    CHECK(fdClose(pub[0]));
    CHECK(server.g_liveStreams.find("test") == server.g_liveStreams.end());

    close(pub[1]);
    close(rtmp[1]);
    close(flv[1]);
}

int main() {
    testDoubleConnection();
    testSweepThenErased();
    testPublisherDisconnectKicksAll();
    if (g_fails == 0) { std::cout << "ConnectionLifecycle 测试全部通过\n"; return 0; }
    std::cerr << g_fails << " 条契约检查失败\n";
    return 1;
}