#include "Amf0Parser.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace  {
    constexpr uint8_t kNumber      = 0x00;
    constexpr uint8_t kBoolean     = 0x01;
    constexpr uint8_t kString      = 0x02;
    constexpr uint8_t kObject      = 0x03;
    constexpr uint8_t kNull        = 0x05;
    constexpr uint8_t kUndefined   = 0x06;
    constexpr uint8_t kEcmaArray   = 0x08;
    constexpr uint8_t kObjectEnd   = 0x09;
    constexpr uint8_t kStrictArray = 0x0A;
    constexpr uint8_t kDate        = 0x0B;
    constexpr uint8_t kLongString  = 0x0C;
    constexpr uint8_t kXmlDocument = 0x0F;
    constexpr uint8_t kTypedObject = 0x10;

// 如果是 GCC / Clang，__BYTE_ORDER__ 内建可用；MSVC 架构默认全是小端
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define IS_BIG_ENDIAN 1
#else
    #define IS_BIG_ENDIAN 0
#endif

    uint16_t be16(const uint8_t* p) {
        //// 外层 cast 仅用于消除某些严格编译器的隐式截断警告（如 -Wconversion）
        //return static_cast<uint16_t>(p[0] << 8 | p[1]);
        uint16_t val;
        std::memcpy(&val, p, sizeof(val));
        #if !IS_BIG_ENDIAN
            val = __builtin_bswap16(val);
        #endif
        return val;
    }

    uint32_t be32(const uint8_t* p) {
        // return  (static_cast<uint32_t>(p[0] << 24)) |
        //         (static_cast<uint32_t>(p[1]) << 16) |
        //         (static_cast<uint32_t>(p[2]) << 8)  |
        //         p[3];
        uint32_t val;
        std::memcpy(&val, p, sizeof(val));
        #if !IS_BIG_ENDIAN
            val = __builtin_bswap32(val);
        #endif
        return val;
    }

    double beDouble(const uint8_t* p) {
        uint64_t raw;
        std::memcpy(&raw, p, sizeof(raw));
        #if !IS_BIG_ENDIAN
            raw = __builtin_bswap64(raw);
        #endif

        double out;
        std::memcpy(&out, &raw, sizeof(out));
        return out;
    }
} //namespace

bool Amf0Parser::parseValue(const uint8_t *p, size_t n, size_t &off, AmfValue &out) {
    return parseValueAt(p, n, off, out, 0);
}

// Object / EcmaArray / TypedObject 共用：(U16 keylen + key + value)*，
// 以 U16 0 + 0x09 收尾。比 FFmpeg 严一处：要求收尾字节确实是 0x09。
bool Amf0Parser::readObjectEntries(const uint8_t* p,size_t n, size_t& off, std::vector<std::pair<std::string, 
                                  AmfValue>>& fields, int depth) {
    for(;;) {
        if(off + 2 > n) return false;
        const uint16_t keyLen = be16(p + off);
        off += 2;

        if(keyLen == 0) {
            if(off +1 > n) return false;
            if(p[off] != kObjectEnd) return false;
            ++ off;
            return true;
        }

        if(off + keyLen > n) return false;
        std::string key(reinterpret_cast<const char*>(p + off), keyLen);
        off += keyLen;

        AmfValue value;
        if(!parseValueAt(p, n, off, value, depth + 1))
            return false;
        fields.emplace_back(std::move(key), std::move(value));
    }                             
}

bool Amf0Parser::parseValueAt(const uint8_t* p, size_t n, size_t& off, AmfValue& out, int depth) {
    if(p == nullptr || off >= n) return false;
    if(depth > KmaxDepth) return false;

    const uint8_t marker = p[off];
    ++off;

    switch (marker) {
        case kNumber: {
            if(off + 8 > n) return false;
            out.type = AmfValue::Type::Number;
            out.number = beDouble(p + off);
            off += 8;
            return true;
        }

        case kBoolean: {
            if(off +1 > n) return false;
            out.type = AmfValue::Type::Boolean;
            out.boolean = (p[off] != 0);
            ++off;
            return true;
        }

        case kString: {
            if(off + 2 > n) return false;
            const uint16_t len = be16(p + off);
            off += 2;
            if(off + len > n) return false;
            out.type = AmfValue::Type::String;
            out.str.assign(reinterpret_cast<const char*>(p + off),len);
            off += len;
            return true;
        }

        case kLongString:
        case kXmlDocument: {
            if(off + 4 > n) return false;
            const uint32_t len = be32(p + off);
            off += 4;
            if(len > n - off) return false;// 用 n-off 比，防 uint32 回绕
            out.type = (marker == kLongString) ? AmfValue::Type::LongString
                                               : AmfValue::Type::XmlDocument;
            out.str.assign(reinterpret_cast<const char*>(p + off), len);
            off += len;
            return true;
        }

        case kObject: {
            out.type = AmfValue::Type::Object;
            return readObjectEntries(p, n, off, out.fields, depth);
        }

        case kEcmaArray: {
            if(off+4 > n) return false;
            // U32 条目数只作越界保护，实际条数由 0 长度 key 收尾为准
            off += 4;
            out.type = AmfValue::Type::EcmaArray;
            return readObjectEntries(p, n, off, out.fields, depth);
        }

        case kStrictArray: {
            if(off + 4 > n) return false;
            const uint32_t count = be32(p + off);
            off += 4;
            out.type = AmfValue::Type::StrictArray;
            for(uint32_t i = 0; i < count; i++) {
                AmfValue v;
                if(!parseValueAt(p, n,off, v, depth + 1))
                    return false;
                out.items.push_back(std::move(v));
            }
            return true;
        }

        case kDate: {
            if(off + 10 > n) return false;
            out.type = AmfValue::Type::Date;
            out.number = beDouble(p + off);
            off += 10;
            return true;
        }

        case kTypedObject: {
            if(off + 2 > n) return false;
            const uint16_t nameLen = be16(p + off);
            off += 2;
            if(off + nameLen > n) return false;
            out.type = AmfValue::Type::TypedObject;
            out.str.assign(reinterpret_cast<const char*>(p + off), nameLen);
            off += nameLen;
            return readObjectEntries(p, n, off, out.fields, depth);
        }
        
        case kNull: {
            out.type = AmfValue::Type::Null;
            return true;
        }

        case kUndefined: {
            out.type = AmfValue::Type::Undefined;
            return true;
        }

        default:
            // 0x04 / 0x07 / 0x09(独立出现) / 0x0D / 0x0E / 其他 —— 判协议错误
            return false;
    }
}

bool Amf0Parser::parserCommand(const uint8_t *p, size_t n, RtmpCommand &out) {
    out = RtmpCommand(); //失败时不留下半成品

    size_t off = 0;
    AmfValue nameVal;
    if(!parseValueAt(p, n, off, nameVal, 0)) return false;
    if(nameVal.type != AmfValue::Type::String &&
       nameVal.type != AmfValue::Type::LongString) return false;
    out.name = std::move(nameVal.str);

    AmfValue txnVal;
    if( !parseValueAt(p, n, off, txnVal, 0)) return false;
    if(txnVal.type != AmfValue::Type::Number) return false;
    out.transactionId = txnVal.number;
    

    // 其余值原样收进 args，包括 Null。
    // 保留 wire 原样 → 分派器能显式断言"这里必须是 Null"，
    // 于是"Null 位置放错类型"这种坏输入就可测了
    while(off < n) {
        AmfValue v;
        if(!parseValueAt(p, n, off, v, 0)) return false;
        out.args.push_back(std::move(v));
    }
    return true;
}