#include "HttpFlvSession.h"
#include "LiveStream.h"
#include "TcpConnection.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
void HttpFlvSession::onMessage(TcpConnection* conn, std::string& readBuffer) {
    while (true) {
        size_t headerEnd = readBuffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            return;
        }
        std::string request = readBuffer.substr(0, headerEnd);
        HandleRequest(conn, request);
        readBuffer.erase(0, headerEnd + 4);
    }
}
void HttpFlvSession::HandleRequest(TcpConnection* conn, const std::string& request) {
    std::string requestLine = request.substr(0, request.find("\r\n"));

    // 浏览器跨域预检请求，直接放行
    if (requestLine.find("OPTIONS") == 0) {
        conn->send(makeHttpHeader(200, "OK", "text/plain", 0, "no-cache", false));
    }

    std::string url = requestLine.substr(requestLine.find(" ") + 1, requestLine.rfind(" ") - requestLine.find(" ") - 1);

    // 识别实时直播流请求！
    if (url.find("/live/") == 0 && url.find(".flv") != std::string::npos) {
        // 截取出流名，比如从 "/live/test.flv" 截取出 "test"
        std::string streamName = url.substr(6, url.find(".flv") - 6);
        std::cout << ">>> 收到网页端 HTTP-FLV 实时拉流请求: " << streamName << std::endl;

        // 1. 发送 HTTP 响应头 (不要传 Content-Length，让连接一直保活！)
        conn->send(makeHttpHeader(200, "OK", "video/x-flv", 0, "no-cache", true));
        // 2. 拼装并发纯正的 FLV 文件头 (9字节) + PreviousTagSize0 (4字节)
        std::string flvHeader;
        flvHeader.push_back('F');
        flvHeader.push_back('L');
        flvHeader.push_back('V');  // signature
        flvHeader.push_back(0x01); // version
        flvHeader.push_back(0x05); // typeID包含音视频 tyepflagrevered:5bit typeflagaudio:1bit
                                   // typeflagsreserved:1bit typeflagsVideo:1
        // DataOffset(HeaderSize)
        flvHeader.push_back(0x00);
        flvHeader.push_back(0x00);
        flvHeader.push_back(0x00);
        flvHeader.push_back(0x09); // 头长度
        flvHeader.push_back(0x00);
        flvHeader.push_back(0x00);
        flvHeader.push_back(0x00);
        flvHeader.push_back(0x00); // PrevTagSize0
        conn->send(flvHeader);
        // 3. 把这个 HTTP 连接作为观众加入直播间！
        LiveStream& stream = epollServer_->g_liveStreams[streamName];
        stream.flvSubscribers.push_back(conn);

        // 4. 秒开机制：瞬间下发“三件套”和“GOP缓存”！全部用 makeFlvTag 打包！
        if (!stream.metadata.empty())
            conn->send(makeFlvTag(18, 0, stream.metadata));
        if (!stream.videoSequenceHeader.empty())
            conn->send(makeFlvTag(9, 0, stream.videoSequenceHeader));
        if (!stream.audioSequenceHeader.empty())
            conn->send(makeFlvTag(8, 0, stream.audioSequenceHeader));
        for (const auto& packet : stream.gopCache) {
            conn->send(makeFlvTag(packet.typeId, packet.timestamp, packet.payload));
        }
        std::cout << ">>> 已向网页端下发秒开缓冲，进入实时转发模式！" << std::endl;
        return;
    }

    // 发送静态flv文件
    if (url.find(".flv") != std::string::npos) {
        sendFlv(conn, url);
    } else {
        std::cout << "Unsupported request: " << url << std::endl;
    }
}
void HttpFlvSession::sendFlv(TcpConnection* conn, const std::string& path) {
    std::string fullPath = FLV_RESOURCE_DIR + path;
    std::cout << "Sending FLV: " << fullPath << std::endl;

    std::ifstream flvFile(fullPath, std::ios::binary);
    if (!flvFile.is_open()) {
        std::cerr << "Failed to open FLV file: " << fullPath << std::endl;
        std::string body = "File not found";
        std::string header = makeHttpHeader(404, "Not Found", "application/octet-stream", body.size(),
                                            "no-cache, no-store, must-revalidate");
        conn->send(header + body);
        return;
    }

    std::string content((std::istreambuf_iterator<char>(flvFile)), std::istreambuf_iterator<char>());

    std::string header = makeHttpHeader(200, "OK", "video/x-flv", content.size(), "max-age=3600");

    conn->send(header);
    conn->send(content);
}
std::string HttpFlvSession::makeHttpHeader(int status, const std::string& statusText, const std::string& contentType,
                                           size_t contentLength, const std::string& cacheControl, bool keepAlive) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << statusText << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"  // 允许跨域播放
        << "Access-Control-Allow-Headers: *\r\n" //
        << "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
        //<< "Content-Length: " << contentLength << "\r\n"
        << "Cache-Control: " << cacheControl << "\r\n"
        << (keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n") << "\r\n";
    return oss.str();
}