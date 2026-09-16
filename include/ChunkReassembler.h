#ifndef __CHUNKREASSEMBLER_H_
#define __CHUNKREASSEMBLER_H_

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <optional>

// 记录一个完整 Message 的属性
struct RtmpMessageHeader {
    uint32_t timestamp = 0;
    uint32_t messageLength = 0;
    uint8_t messageTypeId = 0;
    uint32_t messageStreamId = 0;
};

// 记录某一个 CSID 通道的组包上下文
struct RtmpChunkContext {
    RtmpMessageHeader header;    // 这个通道当前正在处理的 Message 头
    std::string payload;         // 用于将各个 Chunk 碎片拼接起来的缓冲区
    uint32_t bytesRead = 0;      // 这个 Message 已经接收了多少字节
    uint32_t timestampDelta = 0; // <--- 新增：用来记住这个通道的时间增量
    bool externTimestamp = false;
};


class ChunkReassembler {
public:
    bool feed(std::string &buffer);
    void setProcessMessageCallback(std::function<void (const RtmpMessageHeader&, const std::string&)> cb) {
        processMessage = std::move(cb);
    }
    void setChunkSize(uint32_t size) { inChunkSize_ = size;}
    #ifdef LITE_LIVE_TESTING
        std::optional<RtmpChunkContext> getChunkContext(int csid) {
            auto it = chunkContexts_.find(csid);
            if(it != chunkContexts_.end()) {
                return it->second;
            }
            return std::nullopt;
        }
    #endif
private:
    std::unordered_map<int, RtmpChunkContext> chunkContexts_;
    std::function<void (const RtmpMessageHeader&, const std::string&)> processMessage;
    uint32_t inChunkSize_ = 128;
};

#endif