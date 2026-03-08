#include "HttpHlsSession.h"
#include "TcpConnection.h"
#include <fstream>
#include <iostream>
#include <istream>
#include <iterator>
#include <sstream>
#include <string>

// 当连接收到新数据时，由 TcpConnection 调用
void HttpHlsSession::onMessage(TcpConnection* conn, std::string& readBuffer) {

    // 循环目的支持pipelining，即客户端在收到第一个响应之前就发送了多个请求
    //  例如，客户端可能会发送以下两个请求：
    // GET /index.m3u8 HTTP/1.1\r\n\r\n
    // GET /seg0.ts HTTP/1.1\r\n\r\n
    while (true) {
        // 1. 检查是否收到了完整的 HTTP 头部 (以 \r\n\r\n 结尾)
        size_t headerEnd = readBuffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            return; // 数据不够，继续等下一次 EPOLLIN
        }

        // 2. 解析 HTTP 请求 (这里省略具体的字符串解析逻辑)
        std::string request = readBuffer.substr(0, headerEnd);
        // 假设解析出客户端请求的是 /live/stream.m3u8
        HandleRequest(conn, request);

        readBuffer.erase(0, headerEnd + 4);
    }
}

void HttpHlsSession::HandleRequest(TcpConnection* conn, const std::string& request) {
    std::string requestLine = request.substr(0, request.find("\r\n"));
    std::string url = requestLine.substr(requestLine.find(" ") + 1, requestLine.rfind(" ") - requestLine.find(" ") - 1);
    if (url == "/index.m3u8") {
        sendM3u8(conn, url);
    } else if (url.find(".ts") != std::string::npos) {
        sendTs(conn, url);
    } else {
        std::cout << "Unsupported request: " << url << std::endl;
    }
}
void HttpHlsSession::sendM3u8(TcpConnection* conn, const std::string& path) {
    std::string fullPath = HLS_RESOURCE_DIR + path;
    std::cout << "Sending M3U8: " << fullPath << std::endl;

    std::ifstream m3u8File(fullPath);
    if (!m3u8File.is_open()) {
        std::cerr << "Failed to open M3U8 file: " << fullPath << std::endl;
        std::string body = "#EXTM3U\n";
        std::string header = makeHttpHeader(404, "Not Found", "application/vnd.apple.mpegurl", body.size(),
                                            "no-cache, no-store, must-revalidate");
        conn->send(header + body);
        return;
    }
    std::string content((std::istreambuf_iterator<char>(m3u8File)), std::istreambuf_iterator<char>());
    std::string header = makeHttpHeader(200, "OK", "application/vnd.apple.mpegurl", content.size(),
                                        "no-cache, no-store, must-revalidate");
    conn->send(header + content);
}
void HttpHlsSession::sendTs(TcpConnection* conn, const std::string& path) {
    std::string fullPath = HLS_RESOURCE_DIR + path;
    // In a real application, you would read the file and send it over the connection
    std::cout << "Sending TS: " << fullPath << std::endl;
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        std::string body = "File not found";
        std::string header = makeHttpHeader(404, "Not Found", "application/octet-stream", body.size(),
                                            "no-cache, no-store, must-revalidate");
        conn->send(header + body);
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::string header = makeHttpHeader(200, "OK", "video/mp2t", content.size(), "max-age=3600");

    conn->send(header);
    conn->send(content);
}

std::string HttpHlsSession::makeHttpHeader(int status, const std::string& statusText, const std::string& contentType,
                                           size_t contentLength, const std::string& cacheControl, bool keepAlive) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << statusText << "\r\n"
        << "Server: MyHLSServer/1.0\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Cache-Control: " << cacheControl << "\r\n"
        << "Content-Length: " << contentLength << "\r\n";
    if (keepAlive) {
        oss << "Connection: keep-alive\r\n";
    } else {
        oss << "Connection: close\r\n";
    }
    oss << "\r\n";
    return oss.str();
}
