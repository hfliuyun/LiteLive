#include "FlvReader.h"
#include <string>

FlvReader::~FlvReader() {
    if(file_.is_open()) {
        file_.close();
    }
}

bool FlvReader::Open(const std::string& filename) {
    file_.open(filename, std::ios::binary);
    if(!file_.is_open()) {
        return false;
    }

    //1.跳过 FLV header 和第一个 Previous Tag Size
    file_.seekg(FLV_HEADER_SIZE, std::ios::beg);
    file_.seekg(FLV_PREVIOUS_TAG_SIZE, std::ios::cur);
    return true;
}

bool FlvReader::readNextTag(FlvTag& tag) {
    if(!file_.is_open() || file_.eof()) {
        return false;
    }
    // 1. 读取 11 字节的 Tag Header
    char tagHeaderBuf[FLV_TAG_HEADER_SIZE];
    file_.read(tagHeaderBuf, FLV_TAG_HEADER_SIZE);
    if(file_.gcount() != FLV_TAG_HEADER_SIZE) {
        return false; // 读取失败或文件结束
    }
    // 2. 解析 Type 和 Length
    tag.tagType = static_cast<uint8_t>(tagHeaderBuf[0]);
    tag.dataSize = (static_cast<uint8_t>(tagHeaderBuf[1]) << 16) |
                   (static_cast<uint8_t>(tagHeaderBuf[2]) << 8) |
                   static_cast<uint8_t>(tagHeaderBuf[3]);
    // 3.解析时间戳 (FLV 的时间戳比较奇葩，低24位在前，扩展高8位在后)
    tag.timestamp = (static_cast<uint8_t>(tagHeaderBuf[4]) << 16) |
                    (static_cast<uint8_t>(tagHeaderBuf[5]) << 8) |
                    static_cast<uint8_t>(tagHeaderBuf[6]);
    tag.timestamp |= (static_cast<uint8_t>(tagHeaderBuf[7]) << 24); // 扩展时间戳
    // 解析 Stream ID
    tag.streamId = (static_cast<uint8_t>(tagHeaderBuf[8]) << 16) |
                    (static_cast<uint8_t>(tagHeaderBuf[9]) << 8) |
                    static_cast<uint8_t>(tagHeaderBuf[10]);
    // 3. 读取 Data
    tag.data.resize(tag.dataSize);
    file_.read(&tag.data[0], tag.dataSize);
    if(file_.gcount() != tag.dataSize) {
        return false; // 读取失败或文件结束 
    }
    // 4. 跳过 Previous Tag Size (4 字节)
    file_.seekg(FLV_PREVIOUS_TAG_SIZE, std::ios::cur);
    return true;

}