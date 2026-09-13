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

int main() {
    testCase1_Fmt0_SmallMsg_SingleChunk();
    testCase2_Fmt0ThenFmt1_TwoDeliveries();
    testCase3_Fmt0ThenFmt2_TwoDeliveries();
    testCase4_Fmt0LargeMsg_Fmt3Continuation();
    testCase5_OrphanFmt3_NoContext();
    if (g_fails == 0) { std::cout << "RtmpChunk 测试全部通过\n"; return 0; }
    std::cerr << g_fails << " 条契约检查失败\n";
    return 1;
}