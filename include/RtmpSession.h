#ifndef __RTMPSESSION_H__
#define __RTMPSESSION_H__

#include "LiveServer.h"
#include "Session.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <array>
#include "ChunkReassembler.h"

std::array<uint8_t, 8> doubleToBigEndian(double value);
class RtmpSession : public Session {

public:
    enum HandshakeState {
        STATE_WAIT_C0C1,      // 等待读取 1537 字节的 C0+C1
        STATE_WAIT_C2,        // 等待读取 1536 字节的 C2
        STATE_HANDSHAKE_DONE, // 握手完成，准备解析 RTMP Chunk
    };
    void onMessage(TcpConnection* conn, std::string& readBuffer) override;
    void setLiveServer(LiveServer* liveServer) override { liveServer_ = liveServer; }
    void onDisconnect(TcpConnection* conn) override;

#ifdef LITE_LIVE_TESTING
    void setPublish(bool Publishing) {isPublishing_ = Publishing;}
    void setStream(std::string name) {streamName_ = name;}
    HandshakeState handshakeState() const { return handshakeState_; }
    std::optional<RtmpChunkContext> getChunkContext(int csid) {
        if(!chunkreassembler_) return std::nullopt;
        return chunkreassembler_->getChunkContext(csid);
    }
    int countCallbackNum = 0;
    int getCountProcessMessage() { return countCallbackNum;}
#endif

private:
    void sendS0S1S2(TcpConnection* conn, const std::string& c1);
    bool verifyC2(const std::string& c2, const std::string& s1);
    //// 辅助函数：将大端序的 uint32 写入字符串
    void writeUint32BE(std::string& buffer, uint32_t value);
    // 辅助函数：将大端序的 uint24 写入字符串 (RTMP 长度专用)
    void writeUint24BE(std::string& buffer, uint32_t value);
    // 核心发包函数 (假设 payload 已经准备好，且不超过当前 ChunkSize)
    void sendRtmpMessage(TcpConnection* conn, uint8_t csid, uint8_t typeId, const std::string& payload,
                         uint32_t timestamp = 0, uint32_t msgStreamId = 0);
    void processFullMessage(TcpConnection* conn, const RtmpMessageHeader& header, const std::string& payload);
    void sendConnectResult(TcpConnection* conn);
    void sendCreateStreamResult(TcpConnection* conn, double transIdFromClient);
    void sendPlayResponse(TcpConnection* conn);
    void sendPublishResponse(TcpConnection* conn, double transIdFromClient);

    HandshakeState handshakeState_ = STATE_WAIT_C0C1;

    // Chunk Parser
    std::unique_ptr<ChunkReassembler> chunkreassembler_;


    // 服务器当前约定的最大接收块大小，默认为 128
    uint32_t outChunkSize_ = 128; // 服务器发送时使用的 Chunk Size，默认为 128

    LiveServer* liveServer_ = nullptr; // 用于访问全局的直播流列表等资源
    bool isPublishing_ = false;          // 标记当前连接是否正在推流
    std::string streamName_;             // 存储当前正在推流的流名字
};

#endif // __RTMPSESSION_H__