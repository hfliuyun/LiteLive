#include "ChunkReassembler.h"
#include "RtmpSession.h"
#include "utils.h"
#include <cstdint>
#include <memory>
#include <string>

static void GopCache_PBeforeI_StaysEmpty_AndIsolated() {
    auto ctx = makeCtx();
    auto session2 = std::make_shared<RtmpSession>();
    session2->setLiveServer(ctx.server.get());

    ctx.session->setPublish(true);
    session2->setPublish(true);
    ctx.session->setStream("a");
    session2->setStream("b");

    std::string paylaod_hex_I = "170100000101";
    std::string payload_I = hexStringToBytes(paylaod_hex_I);

    std::string payload_hex_P = "270100020001";
    std::string payload_P = hexStringToBytes(payload_hex_P);

    auto msgHeader_P = RtmpMessageHeader{};
    auto msgHeader_I = RtmpMessageHeader{};

    msgHeader_I.messageLength = payload_I.size();
    msgHeader_I.messageTypeId = 9;

    msgHeader_P.messageLength = payload_P.size();
    msgHeader_P.messageTypeId = 9;

    auto& liveStream = ctx.server->g_liveStreams;
    liveStream.try_emplace("a");
    liveStream.try_emplace("b");

    CHECK(liveStream["b"].gopCache.empty());

    ctx.session->handlePayload(ctx.conn.get(), msgHeader_P, payload_P);

    CHECK(liveStream["b"].gopCache.empty());
    CHECK(liveStream["a"].gopCache.empty());

    ctx.session->handlePayload(ctx.conn.get(), msgHeader_I, payload_I);
    CHECK(liveStream["b"].gopCache.empty());
    CHECK(liveStream["a"].gopCache.size() == 1);
    CHECK(static_cast<uint8_t>(liveStream["a"].gopCache[0].payload[0]) == 0x17);

    ctx.session->handlePayload(ctx.conn.get(), msgHeader_P, payload_P);
    CHECK(liveStream["b"].gopCache.empty());
    CHECK(liveStream["a"].gopCache.size() == 2);
    CHECK(static_cast<uint8_t>(liveStream["a"].gopCache[0].payload[0]) == 0x17);
    CHECK(static_cast<uint8_t>(liveStream["a"].gopCache[1].payload[0]) == 0x27);
}

int main()
{
    GopCache_PBeforeI_StaysEmpty_AndIsolated();
    if (g_fails == 0) { std::cout << "RtmpMultiStreamTest 测试全部通过\n"; return 0; }
    std::cerr << g_fails << " 条契约检查失败\n";
    return 1;
}