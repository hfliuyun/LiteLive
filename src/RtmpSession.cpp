#include "RtmpSession.h"
#include "FlvReader.h"
#include "LiveStream.h"
#include "TcpConnection.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

// Adobe RTMP 1.0 §5.2.2–5.2.4：C0/S0 各 1 字节，C1/S1/C2/S2 各 1536 字节
static constexpr size_t kC1Size = 1536;
static constexpr size_t kC0C1Size = 1537;                     // C0 + C1
static constexpr size_t kC2Size = 1536;
static constexpr size_t kFullSize = kC0C1Size + kC2Size;      // 3073
static constexpr size_t kS0S1S2Size = 1 + 1536 + 1536;        // 3073

// 长度是 off-by-one 高发区，编译期钉死，别等红灯才发现算错
static_assert(kC0C1Size == 1537, "C0+C1 必须恰好 1537 字节");
static_assert(kFullSize == 3073, "C0C1+C2 必须恰好 3073 字节，不是 3072");
static_assert(kS0S1S2Size == 3073, "S0+S1+S2 必须恰好 3073 字节");
void RtmpSession::onMessage(TcpConnection* conn, std::string& readBuffer) {
    bool continueParsing = true;
    while(continueParsing) {
        switch (handshakeState_) {
            case STATE_WAIT_C0C1: {
                if (readBuffer.size() < kC0C1Size) {
                    continueParsing = false;
                    break; // 等待读取完整的 C0+C1
                }
                // 处理 C0+C1
                char c0 = readBuffer[0];
                std::string c1 = readBuffer.substr(1, kC1Size);
                readBuffer.erase(0, kC0C1Size);
                // 生成 S0+S1+S2
                sendS0S1S2(conn, c1);
                handshakeState_ = STATE_WAIT_C2;
                break;
            }
            case STATE_WAIT_C2: {
                if (readBuffer.size() < kC2Size) {
                    continueParsing = false;
                    break; // 等待读取完整的 C2
                }
                // 处理 C2
                std::string c2 = readBuffer.substr(0, kC2Size);
                // TODO : 验证 C2 是否正确

                readBuffer.erase(0, kC2Size);
                handshakeState_ = STATE_HANDSHAKE_DONE;
                break;
            }
            case STATE_HANDSHAKE_DONE: {
                // 这里可以添加解析 RTMP Chunk 的逻辑
                handleRtmpChunk(conn, readBuffer);
                continueParsing = false;
                break;
            }
            default: {
                std::cerr << "Unknown state error!" << std::endl;
                continueParsing = false;
                break;
            }
        }
    }
}

void RtmpSession::sendS0S1S2(TcpConnection* conn, const std::string& c1) {
    std::string respones;
    // S0: 版本号，通常为 0x03
    respones.push_back(0x03);
    // S1: 1536 字节的随机数据，通常包含时间戳和随机数
    // TODO: 这里我们简单地使用全零的 S1，实际应用中应该使用随机数据
    std::string s1(1536, 0);
    respones.append(s1);
    // S2: 1536 字节的随机数据，通常是对 C1 的某种形式的响应
    respones.append(c1);
    // 发送 S0+S1+S2
    conn->send(respones);
}
bool RtmpSession::verifyC2(const std::string& c2, const std::string& s1) {
    return c2 == s1;
}

void RtmpSession::handleRtmpChunk(TcpConnection* conn, std::string& readBuffer) {
    while (!readBuffer.empty()) {
        // 1.解析Basic Header(假设只有一个字节)
        uint8_t basicHeader = readBuffer[0];
        uint8_t fmt = (basicHeader >> 6) & 0x03;
        uint8_t csid = basicHeader & 0x3F;
        std::cout << "Parsed RTMP Chunk - fmt: " << (int)fmt << ", csid: " << (int)csid << std::endl;

        int headerSize = 1; // Basic header 长度

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
            return;
        }
        // 2. 提取或创建该 CSID 的上下文状态
        RtmpChunkContext& ctx = chunkContexts_[csid];

        // 3. 根据 fmt 解析并更新上下文状态
        if (fmt == 0) {
            if (readBuffer.size() < headerSize + 11) {
                return; // 等待读取完整的 Message Header
            }
            // 解析 Message Header (11 字节)
            //// RTMP 是大端字节序 (Network Byte Order)
            ctx.header.timestamp = ((static_cast<uint8_t>(readBuffer[headerSize]) << 16) |
                                    (static_cast<uint8_t>(readBuffer[headerSize + 1]) << 8) |
                                    static_cast<uint8_t>(readBuffer[headerSize + 2]));
            ctx.header.messageLength = ((static_cast<uint8_t>(readBuffer[headerSize + 3]) << 16) |
                                        (static_cast<uint8_t>(readBuffer[headerSize + 4]) << 8) |
                                        static_cast<uint8_t>(readBuffer[headerSize + 5]));
            ctx.header.messageTypeId = static_cast<uint8_t>(readBuffer[headerSize + 6]);
            ctx.header.messageStreamId = ((static_cast<uint8_t>(readBuffer[headerSize + 7]) << 24) |
                                          (static_cast<uint8_t>(readBuffer[headerSize + 8]) << 16) |
                                          (static_cast<uint8_t>(readBuffer[headerSize + 9]) << 8) |
                                          static_cast<uint8_t>(readBuffer[headerSize + 10]));
            ctx.timestampDelta = 0; // fmt=0 的 Chunk 是新消息，时间增量重置为 0
            // 清空缓冲区，准备迎接新数据
            ctx.payload.clear();
            ctx.bytesRead = 0;
            std::cout << "Parsed RTMP Message Header - timestamp: " << ctx.header.timestamp
                      << ", messageLength: " << ctx.header.messageLength
                      << ", messageTypeId: " << (int)ctx.header.messageTypeId
                      << ", messageStreamId: " << ctx.header.messageStreamId << std::endl;

        } else if (fmt == 1) {
            if (readBuffer.size() < headerSize + 7) {
                return; // 等待读取完整的 Message Header
            }
            // 解析 Message Header (7 字节)
            //// ！！！注意：这里解析出来的是 Delta (时间差) ！！！
            uint32_t delta = ((static_cast<uint8_t>(readBuffer[headerSize]) << 16) |
                              (static_cast<uint8_t>(readBuffer[headerSize + 1]) << 8) |
                              static_cast<uint8_t>(readBuffer[headerSize + 2]));
            ctx.timestampDelta = delta;    // 记住这个流的最新频率
            ctx.header.timestamp += delta; // 【核心修复】累加到绝对时间戳上！
            ctx.header.messageLength = ((static_cast<uint8_t>(readBuffer[headerSize + 3]) << 16) |
                                        (static_cast<uint8_t>(readBuffer[headerSize + 4]) << 8) |
                                        static_cast<uint8_t>(readBuffer[headerSize + 5]));
            ctx.header.messageTypeId = static_cast<uint8_t>(readBuffer[headerSize + 6]);
            // 清空缓冲区
            ctx.payload.clear();
            ctx.bytesRead = 0;
            // 注意：fmt=1 的 Chunk 没有 messageStreamId 字段，沿用上一个 Chunk 的值
            std::cout << "Parsed RTMP Message Header (fmt=1) - timestamp: " << ctx.header.timestamp
                      << ", messageLength: " << ctx.header.messageLength
                      << ", messageTypeId: " << (int)ctx.header.messageTypeId
                      << ", messageStreamId: " << ctx.header.messageStreamId << std::endl;
        } else if (fmt == 2) {
            if (readBuffer.size() < headerSize + 3) {
                return; // 等待读取完整的 Message Header
            }
            // 解析 Message Header (3 字节)
            uint32_t delta = ((static_cast<uint8_t>(readBuffer[headerSize]) << 16) |
                              (static_cast<uint8_t>(readBuffer[headerSize + 1]) << 8) |
                              static_cast<uint8_t>(readBuffer[headerSize + 2]));
            ctx.timestampDelta = delta;
            ctx.header.timestamp += delta; // 【核心修复】累加到绝对时间戳上！
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
            return; // 连这个小碎片的 payload 都没收齐，继续等网络！
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
            processFullMessage(conn, ctx.header, ctx.payload);

        } else {
            std::cout << "--- [Partial] Chunk read. CSID: " << (int)csid << " Current Progress: " << ctx.bytesRead
                      << " / " << ctx.header.messageLength << "\n";
        }
    }
}

void RtmpSession::processFullMessage(TcpConnection* conn, const RtmpMessageHeader& header, const std::string& payload) {
    if (header.messageTypeId == 20) {
        if (payload.empty())
            return;

        int offset = 0; // 用一个游标来记录当前解析到 payload 的哪个位置了
        // 1. 解析第一个元素：命令名称 (AMF0 String)
        // 规范：0x02 (1字节) + 长度 (2字节大端) + 字符串内容
        if (payload[offset] != 0x02) {
            std::cerr << "Expected AMF0 String for command name, but got type: " << std::hex << (int)payload[offset]
                      << std::dec << std::endl;
            return;
        }
        uint16_t strlen = (static_cast<uint8_t>(payload[offset + 1]) << 8) | static_cast<uint8_t>(payload[offset + 2]);
        offset += 3; // 跳过类型和长度字段
        if (offset + strlen > payload.size())
            return;
        std::string commandName = payload.substr(offset, strlen);
        offset += strlen;

        // 2. 解析第二个元素：Transaction ID (AMF0 Number)
        // 规范：0x00 (1字节) + 8字节 IEEE 754 双精度浮点数
        double transactionId = 0.0;
        if (offset < payload.size() && payload[offset] != 0x00) {
            std::cerr << "Expected AMF0 Number for transaction ID, but got type: " << std::hex << (int)payload[offset]
                      << std::dec << std::endl;
            return;
        }
        offset += 1; // 跳过类型
        if (offset + 8 > payload.size())
            return;
        uint64_t val = 0;
        for (int i = 0; i < 8; i++) {
            val = (val << 8) | static_cast<uint8_t>(payload[offset + i]);
        }
        std::memcpy(&transactionId, &val, sizeof(transactionId));
        offset += 8;

        std::cout << "\n>>> 收到 AMF0 命令: [" << commandName << "], 事务编号: " << transactionId << std::endl;

        if (commandName == "connect") {
            std::cout << "Handling 'connect' command..." << std::endl;

            // 1. 发送 Window Acknowledgement Size (2500000)
            std::string ackPayload;
            writeUint32BE(ackPayload, 5000000);      // Window Acknowledgement
            sendRtmpMessage(conn, 2, 5, ackPayload); // CSID=2, TypeID=5 (Window Acknowledgement Size)

            // 2. 发送 Set Peer Bandwidth (2500000, =2动态)
            std::string bandwidthPayload;
            writeUint32BE(bandwidthPayload, 5000000); // Set Peer Bandwidth
            bandwidthPayload.push_back(0x02);
            sendRtmpMessage(conn, 2, 6, bandwidthPayload); // CSID=2, TypeID=6 (Set Peer Bandwidth)

            // 3. 发送 Set Chunk Size (128)

            std::string chunkSzPayload;
            uint32_t newChunkSize = 4096;
            writeUint32BE(chunkSzPayload, newChunkSize);
            sendRtmpMessage(conn, 2, 1, chunkSzPayload); // CSID=2, Type=1

            this->outChunkSize_ = newChunkSize;

            // 4. 发送 AMF0 "_result" 响应 (告诉客户端连接成功！)
            sendConnectResult(conn);

        } else if (commandName == "createStream") {
            std::cout << "Handling 'createStream' command..." << std::endl;
            // 直接返回一个 Stream ID 就好了，假设我们分配 1.0
            sendCreateStreamResult(conn, transactionId);
        } else if (commandName == "play") {
            std::cout << "Handling 'play' command..." << std::endl;

            // TODO: 可以通过解析 payload 提取出流的名字（"test"），用于后续业务校验。
            std::string streamName = "test"; // 同样先假设硬编码提取出了 "test"
            // 这里我们直接给客户端发送开始播放的响应包。
            // 1. 发送 User Control Message (Stream Begin, EventType=0, StreamID=1)
            std::string streamBeginPayload;
            streamBeginPayload.push_back(0x00);
            streamBeginPayload.push_back(0x00); // Event Type = 0 (Stream Begin)
            streamBeginPayload.push_back(0x00);
            streamBeginPayload.push_back(0x00);
            streamBeginPayload.push_back(0x00);
            streamBeginPayload.push_back(0x01);                    // Stream ID = 1
            sendRtmpMessage(conn, 2, 4, streamBeginPayload, 0, 0); // User Control 的 Stream ID 是 0
            // 2. 发送 onStatus 响应
            sendPlayResponse(conn);

            // 3.标记自己是观众，并把自己加入全局流表的观众列表
            this->isPublishing_ = false;
            this->streamName_ = streamName;
            auto found = liveServer_->g_liveStreams.find(streamName_);   // find，不要用 operator[]
            if (found == liveServer_->g_liveStreams.end()) return;
            LiveStream& stream = found->second;
            stream.subscribers.push_back(conn);

            // 4. ！！！极其重要：给新观众补发“三件套”！！！
            if (!stream.metadata.empty()) {
                sendRtmpMessage(conn, 3, 18, stream.metadata, 0, 1); // Metadata 包走 CSID 3
            }
            if (!stream.videoSequenceHeader.empty()) {
                sendRtmpMessage(conn, 5, 9, stream.videoSequenceHeader, 0, 1);
            }
            if (!stream.audioSequenceHeader.empty()) {
                sendRtmpMessage(conn, 4, 8, stream.audioSequenceHeader, 0, 1);
            }

            // 【新增：首屏秒开核心】
            // 5. 将目前服务器缓存的最新的 GOP 完整历史记录倾泻给新观众！
            if (!stream.gopCache.empty()) {
                std::cout << ">>> [GOP] 正在为新观众补发最近的 GOP 缓存，共 " << stream.gopCache.size()
                          << " 帧数据实现秒开！" << std::endl;
                for (const auto& packet : stream.gopCache) {
                    uint8_t csid = (packet.typeId == 8) ? 4 : 5; // 音频走 CSID 4，视频走 CSID 5
                    sendRtmpMessage(conn, csid, packet.typeId, packet.payload, packet.timestamp, 1);
                }
            }
            std::cout << ">>> 观众 [" << conn << "] 加入直播间，准备完毕！" << std::endl;
            // FlvReader reader;
            // if(reader.Open("../../data/test.flv")) {
            //     FlvTag tag;
            //     while(reader.readNextTag(tag)) {
            //         uint8_t csid = 4;

            //         // 1. 路由分配车道 (CSID)
            //         if (tag.tagType == 8) {
            //             csid = 4; // 音频
            //         } else if (tag.tagType == 9) {
            //             csid = 5; // 视频
            //         } else if (tag.tagType == 18) {
            //             csid = 3; // Metadata (AMF 数据，走 CSID 3)
            //         } else {
            //             continue; // 忽略不认识的奇葩 Tag
            //         }

            //         // 2. 正确的传参顺序：conn, csid, typeId, payload, timestamp, streamId
            //         sendRtmpMessage(conn, csid, tag.tagType, tag.data, tag.timestamp, 1);
            //     }
            //     std::cout << ">>> 所有 FLV 数据已全部塞入发送缓冲区！" << std::endl;
            // } else {
            //     std::cerr << "Failed to open FLV file for streaming!" << std::endl;
            // }
            // std::cout << "send flv sstream done, you can play it with ffplay now!" << std::endl;

            // 当收到 play 时，说明客户端已经准备好接收视频了。
            // 此时你的服务器要给它回复 "NetStream.Play.Start" 等状态，
            // 然后就可以疯狂给它发送音视频数据了！

        } else if (commandName == "publish") {
            std::cout << "Handling 'publish' command..." << std::endl;
            // 1. 从 payload 解析出流名字（比如 "test"）。
            // TODO(解析逻辑和提取 connect 类似，通常在 payload 的靠后位置，先写死 "test" 测试)
            std::string streamName = "test";
            // 2. 将这个连接标记为正在推流 (isPublishing_ = true)，并把 streamName 存起来
            this->isPublishing_ = true;
            streamName_ = streamName;

            // 3. 在全局流表中创建这个流
            auto [it, inserted] = liveServer_->g_liveStreams.try_emplace(streamName);
            LiveStream& stream = it->second;
            if (!inserted && stream.publisher) {
                std::cout << ">>> 警告：流 [" << streamName << "] 已有主播，覆盖" << std::endl;
            }
            stream.publisher = conn;

            // 4. 给主播回复 onStatus (NetStream.Publish.Start)，告诉他可以开始推数据了
            sendPublishResponse(conn, transactionId);

        } else if (commandName == "releaseStream" || commandName == "FCPublish") {
            std::cout << "Ignored " << commandName << std::endl;
        } else {
            std::cout << "Ignored AMF0 command: " << commandName << std::endl;
        }
    } else if (header.messageTypeId == 1) {
        if (payload.size() >= 4) {
            uint32_t clientChunkSize = static_cast<uint8_t>(payload[0] << 24) |
                                       (static_cast<uint8_t>(payload[1]) << 16) |
                                       (static_cast<uint8_t>(payload[2]) << 8) | static_cast<uint8_t>(payload[3]);
            this->inChunkSize_ = clientChunkSize;
        }

    } else if (header.messageTypeId == 8 || header.messageTypeId == 9 || header.messageTypeId == 18) {
        std::cout << "Handling 'cache metadata' command..." << std::endl;
        // 只有推流端发来的音视频包，我们才处理
        if (!this->isPublishing_)
            return;

        auto found = liveServer_->g_liveStreams.find(streamName_);  // find，不建流
        if (found == liveServer_->g_liveStreams.end()) {
            std::cerr << ">>> 错误：主播 [" << streamName_ << "] 的流不存在，丢弃数据包" << std::endl;
            return;
        }
        LiveStream& stream = found->second;
        // 1. 判断并缓存“三件套”
        if (header.messageTypeId == 18) {
            stream.metadata = payload;          // 缓存 Metadata
        } else if (header.messageTypeId == 9) { // 视频
            // RTMP/FLV 规范：VideoData 第1字节的高4位是 FrameType，低4位是 CodecID
            // 如果 CodecID == 7 (AVC/H.264)，第2个字节是 AVCPacketType。
            // AVCPacketType == 0 代表这是 Video Sequence Header (SPS/PPS)！
            // Payload[1] 是 AVCPacketType (0=配置头，1=普通帧)
            if (payload.size() >= 2 && (payload[0] & 0x0F) == 7) {
                if (payload[1] == 0) {
                    stream.videoSequenceHeader = payload; // 缓存视频序列头
                } else if (payload[1] == 1) {
                    // 这是普通视频帧数据！判断是否为 I 帧 (FrameType == 1)
                    if (((payload[0] >> 4) & 0x0F) == 1) {
                        // 【首屏秒开核心】：遇到新的 I 帧，说明上一个 GOP 结束，立刻清空缓存！
                        stream.gopCache.clear();
                        // std::cout << ">>> [GOP] 遇到视频 I 帧，已清空并开启新一轮 GOP 缓存！" << std::endl;
                    }
                    // 把当前的视频帧（无论是 I 帧还是 P 帧）放入缓存
                    stream.gopCache.push_back({header.messageTypeId, header.timestamp, payload});
                }
            }
        } else if (header.messageTypeId == 8) { // 音频
            // 音频规范：第1字节的高4位是 SoundFormat (10代表AAC)。
            // 如果是 AAC，第2个字节是 AACPacketType。等于 0 代表 Sequence Header！
            if (payload.size() >= 2 && ((payload[0] >> 4) & 0x0F) == 10) {
                if (payload[1] == 0) {
                    stream.audioSequenceHeader = payload;
                } else {
                    // 这是普通音频帧，直接放入缓存跟上视频的时间线
                    stream.gopCache.push_back({header.messageTypeId, header.timestamp, payload});
                }
            }
        }

        // 2. 【广播！】把这个包原封不动地发给所有 RTMP 在线观众
        auto rtmpSubs = stream.subscribers;
        for (TcpConnection* subConn : rtmpSubs) {
            // 直接复用你之前写好的 sendRtmpMessage 函数进行发送端分片下发！
            // TODO（如果在生产环境中，这里要把组装好的二进制 buffer 存下来发，避免每个观众都走一遍切片 CPU 计算。
            // 但目前为了跑通，直接调 sendRtmpMessage 是代码量最少的做法）
            uint8_t outCsid = 3;
            if (header.messageTypeId == 8)
                outCsid = 4; // 音频走 CSID 4
            else if (header.messageTypeId == 9)
                outCsid = 5; // 视频走 CSID 5
            // 第5个参数是原包的时间戳，第6个参数强制发到 StreamID=1
            sendRtmpMessage(subConn, outCsid, header.messageTypeId, payload, header.timestamp, 1);
        }

        // 3. 【新增广播！】封装成 FLV Tag，发给所有 HTTP 网页在线观众！
        auto flvSubs = stream.flvSubscribers;
        std::string flvTag = makeFlvTag(header.messageTypeId, header.timestamp, payload);
        for (TcpConnection* flvSubConn : flvSubs) {
            flvSubConn->send(flvTag);
        }

    } else {
        // 对于 Window Acknowledgement Size (5), Set Peer Bandwidth (6) 等等
        // 我们打印一下，然后什么都不做，因为它的 Payload 已经被安全消费掉了！
        std::cout << ">>> 收到并忽略底层控制包 TypeID: " << (int)header.messageTypeId << ", 长度: " << payload.size()
                  << std::endl;
        // std::cout << "payload: " << payload << std::endl;
    }
}

void RtmpSession::sendConnectResult(TcpConnection* conn) {
    std::string amf;
    // 1. 写入字符串 "_result" (响应方法名)
    amf.push_back(0x02); // 类型: String
    amf.push_back(0x00);
    amf.push_back(0x07); // 长度: 7
    amf.append("_result");

    // 2. 写入数字 1.0 (Transaction ID，对应 connect 请求的编号)
    amf.push_back(0x00); // 类型: Number
    double transactionId = 1.0;
    auto transactionIdBytes = doubleToBigEndian(transactionId);
    amf.append(reinterpret_cast<char*>(transactionIdBytes.data()), transactionIdBytes.size());

    // 3. 写入属性对象 (Object: FMS版本等信息)
    amf.push_back(0x03); // 类型: Object
    // 写入键值对 "fmsVer" : "FMS/3,0,1,123"
    amf.push_back(0x00);
    amf.push_back(0x06);
    amf.append("fmsVer"); // Key: 长度6 + 内容
    amf.push_back(0x02);
    amf.push_back(0x00);
    amf.push_back(0x0D);
    amf.append("FMS/3,0,1,123"); // Value

    // 写入键值对 "capabilities" : 31
    amf.push_back(0x00);
    amf.push_back(0x0C);
    amf.append("capabilities");
    amf.push_back(0x00); // 类型: Number
    auto capVal = doubleToBigEndian(31);
    amf.append(reinterpret_cast<char*>(capVal.data()), capVal.size());

    amf.push_back(0x00);
    amf.push_back(0x00);
    amf.push_back(0x09); // Object End 标志

    // 4. 写入信息对象 (Object: 描述连接成功)
    amf.push_back(0x03); // 类型: Object

    // "level" : "status"
    amf.push_back(0x00);
    amf.push_back(0x05);
    amf.append("level");
    amf.push_back(0x02);
    amf.push_back(0x00);
    amf.push_back(0x06);
    amf.append("status");

    // "code" : "NetConnection.Connect.Success"
    amf.push_back(0x00);
    amf.push_back(0x04);
    amf.append("code");
    amf.push_back(0x02);
    amf.push_back(0x00);
    amf.push_back(0x1D);
    amf.append("NetConnection.Connect.Success");

    amf.push_back(0x00);
    amf.push_back(0x00);
    amf.push_back(0x09); // Object End 标志

    // 5. 将这坨 AMF0 数据打包成 RTMP Message 发送 (CSID=3, Type=20)
    sendRtmpMessage(conn, 3, 20, amf);
    std::cout << ">>> Sent '_result' response for connect!" << std::endl;
}

void RtmpSession::sendCreateStreamResult(TcpConnection* conn, double transIdFromClient) {
    std::string amf;
    // 1. 写入字符串 "_result" (响应方法名)
    amf.push_back(0x02); // 类型: String
    amf.push_back(0x00);
    amf.push_back(0x07); // 长度: 7
    amf.append("_result");

    // 2. 写入 Transaction ID (对应 createStream 请求的编号)
    amf.push_back(0x00); // 类型: Number
    auto transIdBytes = doubleToBigEndian(transIdFromClient);
    amf.append(reinterpret_cast<char*>(transIdBytes.data()), transIdBytes.size());

    // 3. 写入 Null (表示没有附加命令对象)
    amf.push_back(0x05); // 类型: Null

    // 4. 写入我们分配的 Stream ID (作为函数返回值，分配 1.0)
    amf.push_back(0x00); // 类型: Number
    auto streamIdBytes = doubleToBigEndian(1);
    amf.append(reinterpret_cast<char*>(streamIdBytes.data()), streamIdBytes.size());

    // 5. 打包发送 (CSID=3, TypeID=20)
    sendRtmpMessage(conn, 3, 20, amf);
    std::cout << ">>> Sent '_result' for createStream! Assigned Stream ID: 1" << std::endl;
}
void RtmpSession::sendPlayResponse(TcpConnection* conn) {
    std::string amf;
    // 1. 命令名: "onStatus"
    amf.push_back(0x02); // 类型: String
    amf.push_back(0x00);
    amf.push_back(0x08); // 长度: 8
    amf.append("onStatus");

    // 2. Transaction ID: 0.0 (play 命令的回应通常事务编号都是 0)
    amf.push_back(0);
    auto transactionIdBytes = doubleToBigEndian(0.0);
    amf.append(reinterpret_cast<char*>(transactionIdBytes.data()), transactionIdBytes.size());

    // 3. Null
    amf.push_back(0x05); // 类型: Null

    // 4. Information Object (描述播放开始的状态)
    amf.push_back(0x03); // 类型: Object
    // "level" : "status"
    amf.push_back(0x00);
    amf.push_back(0x05);
    amf.append("level");
    amf.push_back(0x02);
    amf.push_back(0x00);
    amf.push_back(0x06);
    amf.append("status");

    // "code" : "NetStream.Play.Start" (核心！客户端看到这个才会黑屏转加载)
    amf.push_back(0x00);
    amf.push_back(0x04);
    amf.append("code");
    amf.push_back(0x02);
    amf.push_back(0x00);
    amf.push_back(0x14);
    amf.append("NetStream.Play.Start");

    // "description" : "Start playing."
    amf.push_back(0x00);
    amf.push_back(0x0B);
    amf.append("description");
    amf.push_back(0x02);
    amf.push_back(0x00);
    amf.push_back(0x0E);
    amf.append("Start playing.");

    amf.push_back(0x00);
    amf.push_back(0x00);
    amf.push_back(0x09); // Object End

    // ！！！极其重要：发送 onStatus 走的是 CSID=4，TypeID=20，且归属于刚才分配的 StreamID = 1
    sendRtmpMessage(conn, 3, 20, amf, 0, 1);

    std::cout << ">>> Sent 'onStatus' (NetStream.Play.Start) for play!" << std::endl;
}

void sendPublishResponse(TcpConnection* conn, double transIdFromClient);
void RtmpSession::sendPublishResponse(TcpConnection* conn, double transIdFromClient) {
    std::string amf;
    // 1. 命令名: "onStatus"
    amf.push_back(0x02); // 类型: String
    amf.push_back(0x00);
    amf.push_back(0x08); // 长度: 8
    amf.append("onStatus");

    // 2. Transaction ID: 0.0 (play 命令的回应通常事务编号都是 0)
    amf.push_back(0);
    auto transactionIdBytes = doubleToBigEndian(0.0);
    amf.append(reinterpret_cast<char*>(transactionIdBytes.data()), transactionIdBytes.size());

    // 3. Null
    amf.push_back(0x05); // 类型: Null

    // 4. Information Object (描述播放开始的状态)
    amf.push_back(0x03); // 类型: Object
    // "level" : "status"
    amf.push_back(0x00);
    amf.push_back(0x05);
    amf.append("level");
    amf.push_back(0x02);
    amf.push_back(0x00);
    amf.push_back(0x06);
    amf.append("status");

    // // "code" : "NetStream.Publish.Start" (核心！告诉主播可以推流了)
    amf.push_back(0x00);
    amf.push_back(0x04);
    amf.append("code");
    amf.push_back(0x02);
    amf.push_back(0x00);
    amf.push_back(0x17);
    amf.append("NetStream.Publish.Start");

    // "description" : "Start publishing."
    amf.push_back(0x00);
    amf.push_back(0x0B);
    amf.append("description");
    amf.push_back(0x02);
    amf.push_back(0x00);
    amf.push_back(0x11);
    amf.append("Start publishing.");

    amf.push_back(0x00);
    amf.push_back(0x00);
    amf.push_back(0x09); // Object End

    // 发送 onStatus (CSID=3或4, TypeID=20, msgStreamId=1)
    sendRtmpMessage(conn, 3, 20, amf, 0, 1);
    std::cout << ">>> Sent 'onStatus' (NetStream.Publish.Start) for publish!" << std::endl;
}

void RtmpSession::writeUint32BE(std::string& buffer, uint32_t value) {
    buffer.push_back((value >> 24) & 0xFF);
    buffer.push_back((value >> 16) & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
    buffer.push_back(value & 0xFF);
    // 16进制打印
    //  std::cout << "writeUint32BE: " << value << " -> ";
    //  for(size_t i = buffer.size() - 4; i < buffer.size(); ++i) {
    //      printf("%02X ", static_cast<uint8_t>(buffer[i]));
    //  }
}
// 辅助函数：将大端序的 uint24 写入字符串 (RTMP 长度专用)
void RtmpSession::writeUint24BE(std::string& buf, uint32_t val) {
    buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back(val & 0xFF);
    // 调试打印
    //  std::cout << "writeUint24BE: " << val << " -> ";
    //  for(size_t i = buf.size() - 3; i < buf.size(); ++i) {
    //      printf("%02X ", static_cast<uint8_t>(buf[i]));
    //  }
    //  printf("\n");
}

// TcpConnection* conn, uint8_t csid, uint8_t typeId,
//  const std::string& payload, uint32_t timestamp, uint32_t msgStreamId
void RtmpSession::sendRtmpMessage(TcpConnection* conn, uint8_t csid, uint8_t typeId, const std::string& payload,
                                  uint32_t timestamp, uint32_t msgStreamId) {
    uint32_t payloadSize = payload.size();
    uint32_t bytesSend = 0;
    std::string outBuffer;

    //--- 发送第一个 Chunk (fmt = 0) ---
    // 1.Basic Header (fmt=0,新包)
    outBuffer.push_back(csid & 0x3F); // fmt=0, csid 直接放在低6位
    // 2.Message Header (11字节)
    writeUint24BE(outBuffer, timestamp);   // timestamp
    writeUint24BE(outBuffer, payloadSize); // messageLength
    outBuffer.push_back(typeId);           // messageTypeId
    // RTMP 协议中 Message Stream ID 是少见的 小端序(Little-Endian)!
    outBuffer.push_back(msgStreamId & 0xFF);
    outBuffer.push_back((msgStreamId >> 8) & 0xFF);
    outBuffer.push_back((msgStreamId >> 16) & 0xFF);
    outBuffer.push_back((msgStreamId >> 24) & 0xFF);

    uint32_t firstChunkSize = std::min(payloadSize, outChunkSize_);
    outBuffer.append(payload, 0, firstChunkSize);
    bytesSend += firstChunkSize;

    // --- 发送后续的 Chunk (fmt = 3) ---
    while (bytesSend < payloadSize) {
        // 1.Basic Header (fmt=3,后续包)
        uint8_t basicHeader_fmt3 = (0x03 << 6) | (csid & 0x3F);
        outBuffer.push_back(basicHeader_fmt3);

        uint32_t currentChunkSize = std::min(payloadSize - bytesSend, outChunkSize_);
        outBuffer.append(payload, bytesSend, currentChunkSize);
        bytesSend += currentChunkSize;
    }
    // 组装完毕，一次性交给底层发送
    conn->send(outBuffer);
}

/**
 * 将双精度浮点数转换为大端序字节数组
 * @param value 要转换的浮点数
 * @return 包含8字节的大端序数组
 */
std::array<uint8_t, 8> doubleToBigEndian(double value) {
    std::array<uint8_t, 8> result;

    // 使用 memcpy 避免类型双关问题
    uint64_t int_value;
    std::memcpy(&int_value, &value, sizeof(value));

    // 转换为大端序（网络字节序）
    result[0] = static_cast<uint8_t>((int_value >> 56) & 0xFF);
    result[1] = static_cast<uint8_t>((int_value >> 48) & 0xFF);
    result[2] = static_cast<uint8_t>((int_value >> 40) & 0xFF);
    result[3] = static_cast<uint8_t>((int_value >> 32) & 0xFF);
    result[4] = static_cast<uint8_t>((int_value >> 24) & 0xFF);
    result[5] = static_cast<uint8_t>((int_value >> 16) & 0xFF);
    result[6] = static_cast<uint8_t>((int_value >> 8) & 0xFF);
    result[7] = static_cast<uint8_t>(int_value & 0xFF);

    return result;
}
void RtmpSession::onDisconnect(TcpConnection* conn) {
    if (streamName_.empty())
        return; // 还没建立流就断开了
    auto found = liveServer_->g_liveStreams.find(streamName_);   // find，不要用 operator[]
    if (found == liveServer_->g_liveStreams.end()) return;
    LiveStream& stream = found->second;
    if (this->isPublishing_) {
        std::cout << ">>> 主播 [" << streamName_ << "] 断开连接，直播结束！" << std::endl;

        std::vector<TcpConnection*> rtmpSubs = std::move(stream.subscribers);
        std::vector<TcpConnection*> flvSubs = std::move(stream.flvSubscribers);

        liveServer_->g_liveStreams.erase(found);
        for(auto &sub:rtmpSubs) {
            sub->CloseConnection();
        }
        for(auto &sub:flvSubs) {
            sub->CloseConnection();
        }
    } else {
        std::cout << ">>> RTMP 观众离开直播间..." << std::endl;
        // 从 subscribers 数组中移除这个连接
        auto& subs = stream.subscribers;
        subs.erase(std::remove(subs.begin(), subs.end(), conn),subs.end());
    }
}