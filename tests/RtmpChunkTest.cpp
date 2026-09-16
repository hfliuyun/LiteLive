#include "utils.h"
#include <iostream>
#include <optional>
#include <string>


static void testCase1_Fmt0_SmallMsg_SingleChunk(){
    auto ctx = makeCtx();
    std::string chunk = makeChunk(0, 5 , makeMessageHeaderFmt0(0x00, 0x0A, 0x14, 0x00), std::string(10,'\xAA'));
    std::string buffer = kC0C1 + kC2 + chunk;
    ctx.session->onMessage(ctx.conn.get(), buffer);
    CHECK(ctx.session->getCountProcessMessage() == 1);
    auto chunkctx = ctx.session->getChunkContext(5);
    CHECK(chunkctx != std::nullopt);
    CHECK(chunkctx->payload == std::string(10,'\xAA'));
    CHECK(buffer.empty());
}

static void testCase2_Fmt0ThenFmt1_TwoDeliveries() {
    auto ctx = makeCtx();
    std::string chunk1= makeChunk(0, 5 , makeMessageHeaderFmt0(0x00, 0x0A, 0x14, 0x00), std::string(10,'\xAA'));
    std::string chunk2= makeChunk(1, 5 , makeMsgHeaderFmt1(0x00, 0x05, 0x14), std::string(5,'\xBB'));
    std::string buffer = kC0C1 + kC2 + chunk1 + chunk2;
    ctx.session->onMessage(ctx.conn.get(), buffer);
    CHECK(ctx.session->getCountProcessMessage() == 2);
    auto chunkctx = ctx.session->getChunkContext(5);
    CHECK(chunkctx != std::nullopt);
    CHECK(chunkctx->payload == std::string(5,'\xBB'));
    CHECK(buffer.empty());
}
static void testCase3_Fmt0ThenFmt2_TwoDeliveries() {
    auto ctx = makeCtx();
    std::string chunk1= makeChunk(0, 5 , makeMessageHeaderFmt0(0x00, 0x0A, 0x14, 0x00), std::string(10,'\xAA'));
    std::string chunk2= makeChunk(2, 5 , makeMsgHeaderFmt2(2000), std::string(10,'\xCC'));
    std::string buffer = kC0C1 + kC2 + chunk1 + chunk2;
    ctx.session->onMessage(ctx.conn.get(), buffer);
    CHECK(ctx.session->getCountProcessMessage() == 2);
    auto chunkctx = ctx.session->getChunkContext(5);
    CHECK(chunkctx != std::nullopt);
    CHECK(chunkctx->payload == std::string(10,'\xCC'));
    CHECK(chunkctx->header.timestamp == 2000);
    CHECK(buffer.empty());
}

static void testCase4_Fmt0LargeMsg_Fmt3Continuation() {
    auto ctx = makeCtx();
    std::string chunk1= makeChunk(0, 5 , makeMessageHeaderFmt0(0x00, 200, 0x14, 0x00), std::string(128,'\xAA'));
    std::string chunk2= makeChunk(3, 5 , "", std::string(72,'\xBB'));
    std::string buffer = kC0C1 + kC2 + chunk1 + chunk2;
    ctx.session->onMessage(ctx.conn.get(), buffer);
    CHECK(ctx.session->getCountProcessMessage() == 1);
    auto chunkctx = ctx.session->getChunkContext(5);
    CHECK(chunkctx != std::nullopt);
    CHECK(chunkctx->payload == (std::string(128,'\xAA')+std::string(72,'\xBB')));
    CHECK(buffer.empty());
}

static void testCase5_OrphanFmt3_NoContext() {
    auto ctx = makeCtx();
    std::string chunk = makeChunk(3, 5 , "", std::string(10,'\xDD'));
    std::string buffer = kC0C1 + kC2 + chunk;
    ctx.session->onMessage(ctx.conn.get(), buffer);
    CHECK(ctx.session->getCountProcessMessage() == 0);
    auto chunkctx = ctx.session->getChunkContext(5);
    CHECK(ctx.conn->isClose());
    CHECK(chunkctx == std::nullopt);
}

static void testCase6_ExtendedCsid2Byte_NoHeaderShift() {
    auto ctx = makeCtx();
    std::string chunk {};
    chunk.push_back(static_cast<char>(0x01));
    chunk.push_back(static_cast<char>(0x40));
    chunk.push_back(static_cast<char>(0x00));
    chunk += makeMessageHeaderFmt0(0x00, 0x0A, 0x14, 0x00);
    chunk += std::string(10,'\xAA');
    std::string buffer = kC0C1 + kC2 + chunk;
    ctx.session->onMessage(ctx.conn.get(), buffer);
    CHECK(ctx.session->getCountProcessMessage() == 1);
    CHECK(ctx.session->getChunkContext(1) == std::nullopt);
    CHECK(ctx.session->getChunkContext(0) == std::nullopt);
    auto chunkctx = ctx.session->getChunkContext(128);
    CHECK(chunkctx != std::nullopt);
    CHECK(chunkctx->header.messageLength == 10);
    CHECK(chunkctx->header.timestamp ==0);
    CHECK(chunkctx->header.messageTypeId == 0x14);
    CHECK(chunkctx->header.messageStreamId == 0);
    CHECK(chunkctx->payload == std::string(10,'\xAA'));
    CHECK(buffer.empty());
    CHECK(chunkctx->bytesRead == 10);
}

static void testCase7_ExtendedCsid3Byte_AboveUint8() {
    // CSID=300：ext = 300-64 = 236 = 0x00EC → 低字节在前（小端）
    // 当前实现的截断值：300 mod 256 = 44
    auto ctx = makeCtx();
    std::string chunk {};
    chunk.push_back(static_cast<char>(0x01));   // fmt=0, marker=1 → 3 字节形态
    chunk.push_back(static_cast<char>(0xEC));   // byte1（低字节）= 236
    chunk.push_back(static_cast<char>(0x00));   // byte2（高字节）
    chunk += makeMessageHeaderFmt0(0x00, 0x0A, 0x14, 0x00);
    chunk += std::string(10,'\xBB');
    std::string buffer = kC0C1 + kC2 + chunk;
    ctx.session->onMessage(ctx.conn.get(), buffer);
    CHECK(ctx.session->getChunkContext(300) != std::nullopt);  // 正向：对的车道存在
    CHECK(ctx.session->getChunkContext(44)  == std::nullopt);  // 反向：截断值不能当车道
    CHECK(ctx.session->getChunkContext(1)   == std::nullopt);  // marker 不能当车道
    CHECK(ctx.session->getChunkContext(0)   == std::nullopt);
    CHECK(ctx.session->getCountProcessMessage() == 1);
    CHECK(buffer.empty());
}


static void testCase8_ExtendedCsid3Byte_MaxRange() {
    // CSID=65599：ext = 65599-64 = 65535 = 0xFFFF，值域上限
    // 这一条同时锁两个 bug：
    //   - 截断：65599 mod 256 = 63
    //   - signed char：byte2 = 0xFF → char = -1 → -1*256 + (-1) + 64 = -193
    //     只加宽类型、不加 cast 时，拿到的是负数 CSID 去查 map
    auto ctx = makeCtx();
    std::string chunk {};
    chunk.push_back(static_cast<char>(0x01));   // fmt=0, marker=1 → 3 字节形态
    chunk.push_back(static_cast<char>(0xFF));   // byte1（低字节）
    chunk.push_back(static_cast<char>(0xFF));   // byte2（高字节）
    chunk += makeMessageHeaderFmt0(0x00, 0x0A, 0x14, 0x00);
    chunk += std::string(10,'\xCC');
    std::string buffer = kC0C1 + kC2 + chunk;
    ctx.session->onMessage(ctx.conn.get(), buffer);

    CHECK(ctx.session->getChunkContext(65599) != std::nullopt);
    CHECK(ctx.session->getChunkContext(63)    == std::nullopt);
    CHECK(ctx.session->getChunkContext(1)     == std::nullopt);
    CHECK(ctx.session->getChunkContext(0)     == std::nullopt);
    CHECK(ctx.session->getCountProcessMessage() == 1);
    CHECK(buffer.empty());
}

static void testCase9_ExtendedCsid2Byte_AboveUint8() {
    // CSID=264：ext = 264-64 = 200 = 0xC8，2 字节形态的扩展字节越过 0x7F
    // 未加 cast 时：macOS 上 char 是 signed → char(0xC8) = 200-256 = -56
    //   → -56 + 64 = 8，消息会落到车道 8，而不是 264。
    //   车道 8 是一条真实 marker 车道，所以这不是"丢了消息"而是"串了流"。
    // testCase6 只覆盖了 2 字节形态的低半区（ext=0x40 → CSID 128），
    //   高半区 ext >= 0x80 此前没有任何测试。
    auto ctx = makeCtx();
    std::string chunk {};
    chunk.push_back(static_cast<char>(0x00));   // fmt=0, marker=0 → 2 字节形态
    chunk.push_back(static_cast<char>(0xC8));   // 扩展字节 = 264 - 64
    chunk += makeMessageHeaderFmt0(0x00, 0x0A, 0x14, 0x00);
    chunk += std::string(10,'\xDD');
    std::string buffer = kC0C1 + kC2 + chunk;
    ctx.session->onMessage(ctx.conn.get(), buffer);

    CHECK(ctx.session->getChunkContext(264) != std::nullopt);   // 正向：落在对的车道
    auto chunkctx = ctx.session->getChunkContext(264);
    CHECK(chunkctx->header.messageLength == 10);
    CHECK(chunkctx->header.timestamp == 0);
    CHECK(chunkctx->header.messageTypeId == 0x14);
    CHECK(chunkctx->header.messageStreamId == 0);
    CHECK(chunkctx->payload == std::string(10,'\xDD'));
    CHECK(chunkctx->bytesRead == 10);
    CHECK(ctx.session->getChunkContext(8)  == std::nullopt);    // 反向：signed-char 回绕值不能当车道
    CHECK(ctx.session->getChunkContext(0)  == std::nullopt);    // marker 不能当车道
    CHECK(ctx.session->getCountProcessMessage() == 1);
    CHECK(buffer.empty());
}

static void testCase10_ExtTimestampFmt1_DeltaAccumulates() {
    auto ctx = makeCtx();
    std::string chunk1= makeChunk(0, 5 , makeMessageHeaderFmt0(0x01, 0x0A, 0x14, 0x00), std::string(10,'\xAA'));
    std::string chunk2{};
    chunk2.push_back(static_cast<char>(0x45));
    chunk2+=std::string(3,'\xFF');
    chunk2.push_back(static_cast<char>(0x05));
    chunk2+=std::string(3,'\xF3');
    chunk2.push_back(static_cast<char>(0x00));chunk2.push_back(static_cast<char>(0x00));chunk2.push_back(static_cast<char>(0x0A));
    chunk2.push_back(static_cast<char>(0x14));
    chunk2+=std::string(10,'\xBB');
    std::string buffer = kC0C1 + kC2 + chunk1 + chunk2;
    ctx.session->onMessage(ctx.conn.get(), buffer);
    CHECK(ctx.session->getCountProcessMessage() == 2);
    auto chunkctx = ctx.session->getChunkContext(5);
    CHECK(chunkctx->header.timestamp == 0x05F3F3F4);
    CHECK(chunkctx->header.messageLength == 10);
    CHECK(chunkctx->header.messageTypeId == 0x14);
    CHECK(chunkctx->payload == std::string(10,'\xBB'));
    CHECK(chunkctx->bytesRead == 10);
    CHECK(buffer.empty());
}

static void testCase11_ExtTimestampFmt2_DeltaAccumulates() {
    // fmt2 带扩展 delta。base 故意取非零（ts=2）：
    // "扩展字段是 delta" 和 "扩展字段被当绝对值" 会算出不同的 timestamp，非零 base 才能切开这两条路。
    auto ctx = makeCtx();
    std::string chunk1 = makeChunk(0, 5, makeMessageHeaderFmt0(0x02, 0x0A, 0x14, 0x00), std::string(10,'\xCC'));
    std::string chunk2 {};
    chunk2.push_back(static_cast<char>(0x85));   // fmt=2, csid=5
    chunk2 += std::string(3,'\xFF');             // timestamp delta 哨兵
    chunk2.push_back(static_cast<char>(0x00));   // extended delta = 0x00050001
    chunk2.push_back(static_cast<char>(0x05));
    chunk2.push_back(static_cast<char>(0x00));
    chunk2.push_back(static_cast<char>(0x01));
    chunk2 += std::string(10,'\xEE');
    std::string buffer = kC0C1 + kC2 + chunk1 + chunk2;
    ctx.session->onMessage(ctx.conn.get(), buffer);

    CHECK(ctx.session->getCountProcessMessage() == 2);
    auto chunkctx = ctx.session->getChunkContext(5);
    // TODO: 规范里 fmt2 的扩展字段装的是 delta 还是绝对时间戳？按此算出期望值
    CHECK(chunkctx->header.timestamp == 0x050003);
    // TODO: fmt2 的 header 只有时间字段，messageLength 和 messageTypeId 从哪来？
    CHECK(chunkctx->header.messageLength == 10);
    CHECK(chunkctx->header.messageTypeId == 0x14);
    CHECK(chunkctx->payload == std::string(10, '\xEE'));
    CHECK(chunkctx->bytesRead == 10);
    CHECK(buffer.empty());
}

static void testCase13_ExtTimestampTruncated_WaitsForMoreData() {
    // 哨兵 0xFFFFFF 之后只给 2 字节，扩展字段没收齐。
    // 这一条锁的是：不够必须等，且等的时候要什么都没动。
    auto ctx = makeCtx();
    std::string chunk1 = makeChunk(0, 5, makeMessageHeaderFmt0(0x00, 0x0A, 0x14, 0x00), std::string(10,'\xCC'));
    std::string chunk2 {};
    chunk2.push_back(static_cast<char>(0x45));        // fmt=1, csid=5
    chunk2 += std::string(3,'\xFF');                  // timestamp delta 哨兵
    chunk2.push_back(static_cast<char>(0x00));        // 扩展字段只到第 2 字节
    chunk2.push_back(static_cast<char>(0x00));
    std::string buffer = kC0C1 + kC2 + chunk1 + chunk2;
    ctx.session->onMessage(ctx.conn.get(), buffer);

    // TODO: 交付了几条？chunk1 算不算一条？
    CHECK(ctx.session->getCountProcessMessage() == 1);
    // TODO: 半截 chunk 在 buffer 里的状态怎么断言？注意 buffer 前面本来还有 kC0C1+kC2
    CHECK(buffer == chunk2);
    auto chunkctx = ctx.session->getChunkContext(5);
    // TODO: 半截 chunk 有没有污染已建立的上下文？
    CHECK(chunkctx->payload == std::string(10,  '\xCC'));
    CHECK(chunkctx->bytesRead == 10);
    CHECK(chunkctx->header.timestamp == 0);
}

int main() {
    testCase1_Fmt0_SmallMsg_SingleChunk();
    testCase2_Fmt0ThenFmt1_TwoDeliveries();
    testCase3_Fmt0ThenFmt2_TwoDeliveries();
    testCase4_Fmt0LargeMsg_Fmt3Continuation();
    testCase5_OrphanFmt3_NoContext();
    testCase6_ExtendedCsid2Byte_NoHeaderShift();
    testCase7_ExtendedCsid3Byte_AboveUint8();
    testCase8_ExtendedCsid3Byte_MaxRange();
    testCase9_ExtendedCsid2Byte_AboveUint8();
    testCase10_ExtTimestampFmt1_DeltaAccumulates();
    testCase11_ExtTimestampFmt2_DeltaAccumulates();
    testCase13_ExtTimestampTruncated_WaitsForMoreData();
    if (g_fails == 0) { std::cout << "RtmpChunk 测试全部通过\n"; return 0; }
    std::cerr << g_fails << " 条契约检查失败\n";
    return 1;
}