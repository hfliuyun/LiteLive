#include <iostream>
#include <string>
#include "ChunkReassembler.h"
#include "utils.h"
#include "Amf0Parser.h"

//0200077075626c697368004014000000000000050200066c69766534320200046c697665
static void testCase1_AMF0_Publish() {
    auto ctx = makeCtx();
    std::string paylaod_hex = "0200077075626c697368004014000000000000050200066c69766534320200046c697665";
    std::string payload = hexStringToBytes(paylaod_hex);
    RtmpMessageHeader msgheader{};
    msgheader.messageLength = 36;
    msgheader.messageTypeId = 20;
    msgheader.messageStreamId =1;
    ctx.session->handlePayload(ctx.conn.get(), msgheader, payload);
    CHECK(ctx.server->g_liveStreams.size() == 1);
    CHECK(ctx.server->g_liveStreams.find("live42") != ctx.server->g_liveStreams.end());
}

//
// testCase2: 锁 A —— Object 解析（真 FFmpeg connect，197 字节）
static void testCase2_AMF0_Connect_Object() {
    auto ctx = makeCtx();
    std::string payload_hex = "020007636f6e6e656374003ff00000000000000300036170700200046c6976650008666c61736856657202000d4c4e5820392c302c3132342c320005746355726c02002572746d703a2f2f6c69746561766170702e71636c6f75642e636f6d3a313933352f6c6976650004667061640100000c6361706162696c697469657300402e000000000000000b617564696f436f646563730040afce0000000000000b766964656f436f6465637300406f800000000000000d766964656f46756e6374696f6e003ff0000000000000000009";
    std::string payload = hexStringToBytes(payload_hex);
    RtmpMessageHeader msgheader{};
    msgheader.messageLength = 197;
    msgheader.messageTypeId = 20;
    msgheader.messageStreamId = 1;
    ctx.session->handlePayload(ctx.conn.get(), msgheader, payload);

    // TODO: drainPeer + 断言含 "_result"（connect 成功回复，证明 Object 解析没崩）
    const std::string sent = drainPeer(ctx.fds[1]);
    CHECK(sent.find("_result") != std::string::npos);
}
// testCase3: 锁 B+C —— play 参数校验 + streamName 来源
static void testCase3_AMF0_Play_ThreeArgs() {
    auto ctx = makeCtx();
    std::string payload_hex = "020004706c6179004010000000000000050200186c697465617664656d6f706c6179657273747265616d696400c09f400000000000";
    std::string payload = hexStringToBytes(payload_hex);
    RtmpMessageHeader msgheader{};
    msgheader.messageLength = 39;
    msgheader.messageTypeId = 20;
    msgheader.messageStreamId = 1;
    ctx.session->handlePayload(ctx.conn.get(), msgheader, payload);

    // TODO: drainPeer + 断言含 "livestream"（锁 C：streamName 取自 args[1].str，不是 cmd.name 的 "play"）
    // TODO: drainPeer + 断言含 "NetStream.Play.Start"（锁 B：play 完整跑完没被拒）
    const std::string sent = drainPeer(ctx.fds[1]);
    CHECK(sent.find("NetStream.Play.Start") != std::string::npos);
}

// testCase4: 纯解析 —— 锁 parserCommand 返回值 + 解析内容
static void testCase4_PureParser_Publish() {
    std::string payload_hex = "0200077075626c697368004014000000000000050200066c69766534320200046c697665";
    std::string payload = hexStringToBytes(payload_hex);

    RtmpCommand cmd;
    bool ok = Amf0Parser::parserCommand(
        reinterpret_cast<const uint8_t*>(payload.data()),
        payload.size(),
        cmd);

    CHECK(ok == true);
    CHECK(cmd.name == "publish");
    CHECK(cmd.args.size() == 3);       // Null, String streamName, String type
    CHECK(cmd.args[1].type == AmfValue::Type::String);
    CHECK(cmd.args[1].str == "live42");
}

// testCase5: 坏输入 —— 截断 + 未知 marker，锁 parserCommand == false
static void testCase5_PureParser_BadInput() {
    // 5a: 截断 —— 只到 txnId 就断
    {
        std::string payload_hex = "0200077075626c697368004014000000000000";
        std::string payload = hexStringToBytes(payload_hex);

        RtmpCommand cmd;
        bool ok = Amf0Parser::parserCommand(
            reinterpret_cast<const uint8_t*>(payload.data()),
            payload.size(),
            cmd);

        CHECK(ok == true);
        CHECK(cmd.args.empty());
    }

    // 5b: 未知 marker 0x04 (movieclip, AMF0 规范 "reserved, not supported")
    {
        std::string payload_hex = "020004706c617900401000000000000004";
        std::string payload = hexStringToBytes(payload_hex);

        RtmpCommand cmd;
        bool ok = Amf0Parser::parserCommand(
            reinterpret_cast<const uint8_t*>(payload.data()),
            payload.size(),
            cmd);

        CHECK(ok == false);
    }

    // 5c: 会话级 —— 坏输入经 handlePayload，确认 g_liveStreams 没被污染
    {
        auto ctx = makeCtx();
        std::string payload_hex = "0200077075626c697368004014000000000000";
        std::string payload = hexStringToBytes(payload_hex);
        RtmpMessageHeader msgheader{};
        msgheader.messageLength = 20;
        msgheader.messageTypeId = 20;
        msgheader.messageStreamId = 1;
        ctx.session->handlePayload(ctx.conn.get(), msgheader, payload);

        CHECK(ctx.server->g_liveStreams.empty());
    }
}

int main()
{
    testCase1_AMF0_Publish();
    testCase2_AMF0_Connect_Object();
    testCase3_AMF0_Play_ThreeArgs();
    testCase4_PureParser_Publish();
    testCase5_PureParser_BadInput();
    if (g_fails == 0) { std::cout << "RtmpAmf0Test 测试全部通过\n"; return 0; }
    std::cerr << g_fails << " 条契约检查失败\n";
    return 1;
}