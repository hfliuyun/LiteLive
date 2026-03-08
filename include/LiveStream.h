#ifndef __LIVESTREAM_H__
#define __LIVESTREAM_H__
#include <cstdint>
#include <string>
#include <vector>

class TcpConnection;

// 定义一个历史数据包
struct RtmpPacket {
    uint8_t typeId;       // 8(音频) 或 9(视频)
    uint32_t timestamp;   // 绝对时间戳
    std::string payload;  // 真实数据
};

// 代表一个正在直播的流（比如 "test"）
struct LiveStream {
    TcpConnection* publisher = nullptr;           // 推流端 (OBS)
    std::vector<TcpConnection*> subscribers;      // 拉流端列表 (FFplay 观众们)
    // ！！！中途加入的观众必备的“三件套”缓存！！！
    std::string metadata;             // TypeID=18 的 AMF 数据
    std::string videoSequenceHeader;  // TypeID=9 且为 AVC Sequence Header 的数据
    std::string audioSequenceHeader;  // TypeID=8 且为 AAC Sequence Header 的数据

    //！！！新增：GOP 缓存（首屏秒开神器）！！！ 缓存关键帧（I帧）以及它之后的若干 P 帧，直到下一个 I 帧出现为止。
    std::vector<RtmpPacket> gopCache;
};

#endif // __LIVESTREAM_H__