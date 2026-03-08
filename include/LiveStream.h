#ifndef __LIVESTREAM_H__
#define __LIVESTREAM_H__
#include "FlvReader.h"
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
    std::vector<TcpConnection*> flvSubscribers;  // HTTP-FLV 观众！
    // ！！！中途加入的观众必备的“三件套”缓存！！！
    std::string metadata;             // TypeID=18 的 AMF 数据
    std::string videoSequenceHeader;  // TypeID=9 且为 AVC Sequence Header 的数据
    std::string audioSequenceHeader;  // TypeID=8 且为 AAC Sequence Header 的数据

    //！！！新增：GOP 缓存（首屏秒开神器）！！！ 缓存关键帧（I帧）以及它之后的若干 P 帧，直到下一个 I 帧出现为止。
    std::vector<RtmpPacket> gopCache;
};

// 将原始数据打包为 FLV Tag 的极其优雅的辅助函数
inline std::string makeFlvTag(uint8_t type, uint32_t timestamp, const std::string& payload) {
    std::string tag;
    tag.reserve(FLV_TAG_HEADER_SIZE + payload.size() + FLV_PREVIOUS_TAG_SIZE);
    
    tag.push_back(type); //1.tag type 8bits
    uint32_t dataSize = payload.size();
    //2.Tag Data size 24bits
    tag.push_back((dataSize >> 16) & 0xFF);
    tag.push_back((dataSize >> 8) & 0xFF);
    tag.push_back(dataSize & 0xFF);
    //3.Timestamp 24bits 拓展时间戳在最高位
    tag.push_back((timestamp >> 16) & 0xFF);
    tag.push_back((timestamp >> 8) & 0xFF);
    tag.push_back(timestamp & 0xFF);
    //4.Timestamp extension 8bits
    tag.push_back((timestamp >> 24) & 0xFF);
    //5.StreamID (总是0)
    tag.push_back(0);
    tag.push_back(0); 
    tag.push_back(0);

    //6.Payload
    tag.append(payload);

    uint32_t previousTagSize = FLV_TAG_HEADER_SIZE + payload.size();
    //7.Previous Tag Size 24bits
    tag.push_back((previousTagSize >> 24) & 0xFF);
    tag.push_back((previousTagSize >> 16) & 0xFF);
    tag.push_back((previousTagSize >> 8) & 0xFF);
    tag.push_back(previousTagSize & 0xFF);

    return tag;



}
#endif // __LIVESTREAM_H__