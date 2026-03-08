#ifndef __FLV_READER_H__
#define __FLV_READER_H__

#include <cstddef>
#include <cstdint>
#include <string>
#include <fstream>


constexpr size_t FLV_HEADER_SIZE = 9;
constexpr size_t FLV_PREVIOUS_TAG_SIZE = 4;
constexpr size_t FLV_TAG_HEADER_SIZE = 11;

struct FlvTag {
    uint8_t tagType; // 1字节
    uint32_t dataSize; // 3字节大端
    uint32_t timestamp; // 3字节大端 + 1字节扩展
    uint32_t streamId; // 3字节大端，通常为0
    std::string data; // dataSize 字节
};

class FlvReader {
public:
    FlvReader() {};
    ~FlvReader();

    bool Open(const std::string& filename);
    bool readNextTag(FlvTag& tag);
private:
    std::ifstream file_;
};

#endif // __FLV_READER_H__