#include "ChunkReassembler.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

bool  ChunkReassembler::feed(std::string &readBuffer) {
        while (!readBuffer.empty()) {
            // 1.解析Basic Header
            uint8_t fmt = (readBuffer[0] >> 6) & 0x03;
            uint8_t csid_marker = readBuffer[0] & 0x3F;
            int headerSize; // Basic header 长度
            if(csid_marker == 0) {
                headerSize = 2;
            } else if(csid_marker == 1) {
                headerSize = 3;
            } else {
                headerSize = 1;
            }
            if(readBuffer.size() < headerSize) {
                return true;
            }
            uint32_t csid;
            if(csid_marker == 0) {
                csid = static_cast<uint32_t>(static_cast<uint8_t>(readBuffer[1])) + 64;
            } else if(csid_marker == 1) {
                csid = static_cast<uint32_t>((static_cast<uint8_t>(readBuffer[2]) << 8
                       |  static_cast<uint8_t>(readBuffer[1]))) + 64;
            } else {
                csid = csid_marker;
            }
            std::cout << "Parsed RTMP Chunk - fmt: " << (int)fmt << ", csid: " << (int)csid << std::endl;

            // 根据 fmt 判断 Message Header 的长度
            int msgHeaderSize = 0;
            if (fmt == 0)
                msgHeaderSize = 11;
            else if (fmt == 1)
                msgHeaderSize = 7;
            else if (fmt == 2)
                msgHeaderSize = 3;
            else if (fmt == 3)
                msgHeaderSize = 0;
            // 如果连协议头都不够，说明网络包被腰斩了，直接返回等下一次 epoll
            if (readBuffer.size() < headerSize + msgHeaderSize) {
                return true;
            }
            // 2. 提取或创建该 CSID 的上下文状态
            auto it = chunkContexts_.find(csid);
            if(it == chunkContexts_.end()) {
                if(fmt == 1 || fmt == 2 || fmt == 3) return false;
                it = chunkContexts_.emplace(csid,RtmpChunkContext{}).first;
            }
            RtmpChunkContext& ctx = it->second;
            uint32_t ts_flag = 0;
            if(fmt != 3) {
                ts_flag = static_cast<uint32_t>((static_cast<uint8_t>(readBuffer[headerSize]) << 16) |
                                                (static_cast<uint8_t>(readBuffer[headerSize + 1]) << 8)) |
                                                static_cast<uint8_t>(readBuffer[headerSize + 2]);
            }
            uint8_t exTimeLength = 0;
            if(fmt != 3 && ts_flag == 0xFFFFFF) {
                exTimeLength = 4;
            }
            msgHeaderSize += exTimeLength;

            // 3. 根据 fmt 解析并更新上下文状态
            if (fmt == 0) {
                if (readBuffer.size() < headerSize + 11 + exTimeLength) {
                    return true; // 等待读取完整的 Message Header
                }
                // 解析 Message Header (11 字节)
                //// RTMP 是大端字节序 (Network Byte Order)
                if(exTimeLength) {
                    ctx.header.timestamp = static_cast<uint32_t>((static_cast<uint8_t>(readBuffer[headerSize+3]) << 24) |
                                            (static_cast<uint8_t>(readBuffer[headerSize + 4]) << 16)) |
                                            (static_cast<uint8_t>(readBuffer[headerSize + 5]) << 8) |
                                            static_cast<uint8_t>(readBuffer[headerSize + 6]);
                }
                else {
                    ctx.header.timestamp = ts_flag;
                }
                ctx.header.messageLength = ((static_cast<uint8_t>(readBuffer[headerSize +exTimeLength+ 3]) << 16) |
                                            (static_cast<uint8_t>(readBuffer[headerSize +exTimeLength+ 4]) << 8) |
                                            static_cast<uint8_t>(readBuffer[headerSize  +exTimeLength+ 5]));
                ctx.header.messageTypeId = static_cast<uint8_t>(readBuffer[headerSize + exTimeLength + 6]);
                ctx.header.messageStreamId = static_cast<uint8_t>(readBuffer[headerSize + exTimeLength + 7]) |
                                            (static_cast<uint8_t>(readBuffer[headerSize + exTimeLength + 8]) << 8) |
                                            (static_cast<uint8_t>(readBuffer[headerSize + exTimeLength + 9]) << 16) |
                                            (static_cast<uint8_t>(readBuffer[headerSize + exTimeLength + 10]) << 24);
                ctx.timestampDelta = 0; // fmt=0 的 Chunk 是新消息，时间增量重置为 0
                // 清空缓冲区，准备迎接新数据
                ctx.payload.clear();
                ctx.bytesRead = 0;
                std::cout << "Parsed RTMP Message Header - timestamp: " << ctx.header.timestamp
                        << ", messageLength: " << ctx.header.messageLength
                        << ", messageTypeId: " << (int)ctx.header.messageTypeId
                        << ", messageStreamId: " << ctx.header.messageStreamId << std::endl;

            } else if (fmt == 1) {
                if (readBuffer.size() < headerSize + 7 + exTimeLength) {
                    return true; // 等待读取完整的 Message Header
                }
                // 解析 Message Header (7 字节)
                if(exTimeLength) {
                    ctx.timestampDelta = static_cast<uint32_t>((static_cast<uint8_t>(readBuffer[headerSize + 3]) << 24) |
                                                                (static_cast<uint8_t>(readBuffer[headerSize + 4]) << 16)) |
                                                                (static_cast<uint8_t>(readBuffer[headerSize + 5]) << 8) |
                                                                 static_cast<uint8_t>(readBuffer[headerSize+6]);
                } else {
                //// ！！！注意：这里解析出来的是 Delta (时间差) ！！!
                // 记住这个流的最新频率
                    ctx.timestampDelta = ts_flag;
                }
                ctx.header.timestamp += ctx.timestampDelta; // 【核心修复】累加到绝对时间戳上！
                ctx.header.messageLength = ((static_cast<uint8_t>(readBuffer[headerSize + exTimeLength +3]) << 16) |
                                            (static_cast<uint8_t>(readBuffer[headerSize + exTimeLength+ 4]) << 8) |
                                            static_cast<uint8_t>(readBuffer[headerSize + exTimeLength + 5]));
                ctx.header.messageTypeId = static_cast<uint8_t>(readBuffer[headerSize + exTimeLength +6]);
                // 清空缓冲区
                ctx.payload.clear();
                ctx.bytesRead = 0;
                // 注意：fmt=1 的 Chunk 没有 messageStreamId 字段，沿用上一个 Chunk 的值
                std::cout << "Parsed RTMP Message Header (fmt=1) - timestamp: " << ctx.header.timestamp
                        << ", messageLength: " << ctx.header.messageLength
                        << ", messageTypeId: " << (int)ctx.header.messageTypeId
                        << ", messageStreamId: " << ctx.header.messageStreamId << std::endl;
            } else if (fmt == 2) {
                if (readBuffer.size() < headerSize + 3 + exTimeLength) {
                    return true; // 等待读取完整的 Message Header
                }
                if(exTimeLength) {
                    ctx.timestampDelta = static_cast<uint32_t>(
                                    (static_cast<uint8_t>(readBuffer[headerSize + 3]) << 24) |
                                    (static_cast<uint8_t>(readBuffer[headerSize + 4]) << 16)) |
                                    static_cast<uint8_t>(readBuffer[headerSize + 5]) << 8 |
                                    static_cast<uint8_t>(readBuffer[headerSize + 6]);
                }
                else {
                    // 解析 Message Header (3 字节)
                    ctx.timestampDelta= ts_flag;
                }
                ctx.header.timestamp += ctx.timestampDelta; // 【核心修复】累加到绝对时间戳上！
                // fmt=2 的 Chunk 没有 messageLength、messageTypeId 和 messageStreamId 字段，沿用上一个 Chunk 的值
                // 清空缓冲区
                ctx.payload.clear();
                ctx.bytesRead = 0;
                std::cout << "Parsed RTMP Message Header (fmt=2) - timestamp: " << ctx.header.timestamp
                        << ", messageLength: " << ctx.header.messageLength
                        << ", messageTypeId: " << (int)ctx.header.messageTypeId
                        << ", messageStreamId: " << ctx.header.messageStreamId << std::endl;

            } else if (fmt == 3) {
                // 情况A：如果前一个包已经收满了，那这也是个新包（只是所有属性全抄上一个，常用于音视频连续帧）
                if (ctx.bytesRead >= ctx.header.messageLength) {
                    // 情况A：这是一个全新的音视频帧！它不仅复用上一个包的所有属性，还复用时间差！
                    ctx.header.timestamp += ctx.timestampDelta; // 【核心修复】即使没传时间，也要乖乖累加！
                    ctx.bytesRead = 0;
                    ctx.payload.clear();
                }
                // 情况B：如果前一个包没收满（bytesRead < messageLength），那它就是后续的分片！
                //  啥属性都不用改，直接往下走去读取数据。
            }

            // 4. 精确计算本次应该从 buffer 中挖取多长的数据
            uint32_t bytesLeft = ctx.header.messageLength - ctx.bytesRead;
            // 本次能读的数据，绝不能超过剩余没读完的数据，也不能超过 ChunkSize 上限
            uint32_t currentChunkPayloadSize = std::min(bytesLeft, inChunkSize_);
            // for(size_t i = 0; i < readBuffer.size(); ++i) {
            //     printf("%02X ", static_cast<uint8_t>(readBuffer[i]));
            //     if((i + 1) % 16 == 0)            printf("\n");
            // }
            // 5. 再次安全检查：头 + 本次该读的数据量，收齐了吗？
            if (readBuffer.size() < headerSize + msgHeaderSize + currentChunkPayloadSize) {
                return true; // 连这个小碎片的 payload 都没收齐，继续等网络！
            }

            // 6. 提取这段 Payload 并拼接到我们的状态机缓存中
            int payloadOffset = headerSize + msgHeaderSize;
            ctx.payload.append(readBuffer, payloadOffset, currentChunkPayloadSize);
            ctx.bytesRead += currentChunkPayloadSize;

            // 7. 从 buffer 中移除我们刚刚处理的这段数据
            readBuffer.erase(0, headerSize + msgHeaderSize + currentChunkPayloadSize);

            // 8.判断这个消息是否已经收齐了
            if (ctx.bytesRead == ctx.header.messageLength) {
                std::cout << "\n>>> [Success] Reassembled Full RTMP Message! CSID: " << (int)csid
                        << ", TypeID: " << (int)ctx.header.messageTypeId << ", TotalLength: " << ctx.payload.size()
                        << " bytes\n";
                // std::cout << "payload: " << ctx.payload << std::endl;
                if(processMessage) processMessage( ctx.header, ctx.payload);
            } else {
                std::cout << "--- [Partial] Chunk read. CSID: " << (int)csid << " Current Progress: " << ctx.bytesRead
                        << " / " << ctx.header.messageLength << "\n";
            }
        }
    return true;
}
