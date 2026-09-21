#ifndef __AMFPARSER_H__
#define __AMFPARSER_H__

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// 一个 AMF0 值。tagged union：type 标记下面哪个成员有效。
// 类型与宽度依据 Adobe AMF0 Specification v1.0 §2.1。

struct AmfValue {
    enum class Type {
        Number,      // 0x00
        Boolean,     // 0x01
        String,      // 0x02
        Object,      // 0x03
        Null,        // 0x05
        Undefined,   // 0x06
        EcmaArray,   // 0x08
        StrictArray, // 0x0A
        Date,        // 0x0B
        LongString,  // 0x0C
        XmlDocument, // 0x0F
        TypedObject, // 0x10
    };
    Type type = Type::Null;

    double number = 0.0;                              // Number / Date（Date 的 UTC 偏移丢弃）
    bool boolean = false;                              // Boolean
    std::string str;                                   // String / LongString / XmlDocument
                                                       // 以及 TypedObject 的类名
    std::vector<std::pair<std::string, AmfValue>> fields;  // Object / EcmaArray / TypedObject
    std::vector<AmfValue> items;                       // StrictArray
};

// typeId=20 的命令信封：命令名 + 事务编号 + 之后的所有参数值。
// 命令形状知识不放在这里，留在调用方的分派处按位置取。
struct RtmpCommand {
    std::string name;
    double transactionId = 0.0;
    std::vector<AmfValue> args;
};
// 无状态。零数据成员。所有方法 static。
// 任何失败（截断 / 越界 / 不支持的 marker / 递归过深）都返回 false。
// 未知 marker 无法"跳过"——marker 决定 payload 宽度，宽度未知就无法推进游标。
class Amf0Parser {

public:
    static bool parseValue(const uint8_t* p, size_t n, size_t& off, AmfValue& out);
    static bool parserCommand(const uint8_t* p, size_t n, RtmpCommand& out);
private:
    // 0x04 movieclip / 0x0D unsupported / 0x0E recordset 均标记为
    // "reserved, not supported"；0x09 只能作 Object 收尾，不能独立出现。
    static const int KmaxDepth = 16;   
    Amf0Parser() = delete;

    static bool parseValueAt(const uint8_t* p, size_t n, size_t& off, AmfValue& out, int depth);

    static bool readObjectEntries(const uint8_t* p,size_t n, size_t& off, std::vector<std::pair<std::string, 
                                  AmfValue>>& fields, int depth);
};


#endif
//__AMFPARSER_H__