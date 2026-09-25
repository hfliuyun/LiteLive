#include "LiveServer.h"
#include "Poller.h"
#include "RtmpSession.h"
#include "TcpConnection.h"
#include "utils.h"
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

static void testSlowConsumerClosedWhenSendQueueOverflows_AndHealthyPeerUnaffected() {
    LiveServer server(0);
    int slow[2], fast[2];
    CHECK(makePair(slow)); CHECK(makePair(fast));
    CHECK(server.poller()->Add(slow[0], EventMask::Readable, TriggerMode::Edge));
    CHECK(server.poller()->Add(fast[0], EventMask::Readable, TriggerMode::Edge));

    auto slowSession = std::make_shared<RtmpSession>();
    slowSession->setLiveServer(&server);
    auto fastSession = std::make_shared<RtmpSession>();
    fastSession->setLiveServer(&server);

    auto slowConn = std::make_unique<TcpConnection>(slow[0], &server, slowSession);
    auto fastConn = std::make_unique<TcpConnection>(fast[0], &server, fastSession);
    slowConn->setOnCloseCb([](int){});
    fastConn->setOnCloseCb([](int){});

    const std::string chunk(4 * 1024, 'x');
    for (int i = 0; i < 1000 && !slowConn->isClose(); ++i) {
        slowConn->send(chunk);
        fastConn->send(chunk);
        drainPeer(fast[1]);
    }
    CHECK(slowConn->isClose());
    CHECK(!fastConn->isClose());

    // 阈值与 TcpConnection::kWriteQueueLimit 同值（64KB）：下界是触发条件的字面翻译，
    // 上界成立因为每轮最多多一个 chunk；两边都与内核缓冲大小无关，CI 稳
    CHECK(slowConn->pendingBytes() > 64 * 1024);
    CHECK(slowConn->pendingBytes() <= 64 * 1024 + 4 * 1024);


    fastConn->send("x");
    std::string fastResponse = drainPeer(fast[1]);
    CHECK(fastResponse == "x");

    close(slow[0]); close(fast[0]);
    close(slow[1]);
    close(fast[1]);
}

int main()
{
    testSlowConsumerClosedWhenSendQueueOverflows_AndHealthyPeerUnaffected();
    if (g_fails == 0) { std::cout << "BackpressureTest 测试全部通过\n"; return 0; }
    std::cerr << g_fails << " 条契约检查失败\n";
    return 1;
}