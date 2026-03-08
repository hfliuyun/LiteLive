#ifndef __RTMPSESSION_H__
#define __RTMPSESSION_H__

#include "EpollServer.h"
#include "Session.h"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// 记录一个完整 Message 的属性
struct RtmpMessageHeader {
    uint32_t timestamp = 0;
    uint32_t messageLength = 0;
    uint8_t messageTypeId = 0;
    uint32_t messageStreamId = 0;
};

std::array<uint8_t, 8> doubleToBigEndian(double value);

// 记录某一个 CSID 通道的组包上下文
struct RtmpChunkContext {
    RtmpMessageHeader header;    // 这个通道当前正在处理的 Message 头
    std::string payload;         // 用于将各个 Chunk 碎片拼接起来的缓冲区
    uint32_t bytesRead = 0;      // 这个 Message 已经接收了多少字节
    uint32_t timestampDelta = 0; // <--- 新增：用来记住这个通道的时间增量
};

class RtmpSession : public Session {

public:
    void onMessage(TcpConnection* conn, std::string& readBuffer) override;
    void setEpollServer(EpollServer* epollServer) override { epollServer_ = epollServer; }
    void onDisconnect(TcpConnection* conn) override;

private:
    void sendS0S1S2(TcpConnection* conn, const std::string& c1);
    bool verifyC2(const std::string& c2, const std::string& s1);
    void handleRtmpChunk(TcpConnection* conn, std::string& readBuffer);
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

    // 定义握手的状态机
    enum HandshakeState {
        STATE_WAIT_C0C1,      // 等待读取 1537 字节的 C0+C1
        STATE_WAIT_C2,        // 等待读取 1536 字节的 C2
        STATE_HANDSHAKE_DONE, // 握手完成，准备解析 RTMP Chunk
    };
    HandshakeState handshakeState_ = STATE_WAIT_C0C1;

    // 映射表：CSID -> 对应的组包上下文
    std::unordered_map<int, RtmpChunkContext> chunkContexts_;

    // 服务器当前约定的最大接收块大小，默认为 128
    uint32_t inChunkSize_ = 128;
    uint32_t outChunkSize_ = 128; // 服务器发送时使用的 Chunk Size，默认为 128

    EpollServer* epollServer_ = nullptr; // 用于访问全局的直播流列表等资源
    bool isPublishing_ = false;          // 标记当前连接是否正在推流
    std::string streamName_;             // 存储当前正在推流的流名字
};

#endif // __RTMPSESSION_H__