#include "HttpFlvSession.h"
#include "TcpConnection.h"
#include <iostream>
#include <fstream>
#include <sstream>
void HttpFlvSession::onMessage(TcpConnection* conn, std::string& readBuffer) {
    while(true) {
        size_t headerEnd = readBuffer.find("\r\n\r\n");
        if(headerEnd == std::string::npos) {
            return;
        }
        std::string request = readBuffer.substr(0, headerEnd);
        HandleRequest(conn, request);
        readBuffer.erase(0, headerEnd + 4);
    }
}
void HttpFlvSession::HandleRequest(TcpConnection* conn, const std::string& request) {
    std::string requestLine = request.substr(0, request.find("\r\n"));
    std::string url = requestLine.substr(requestLine.find(" ") + 1, requestLine.rfind(" ") - requestLine.find(" ") - 1);
    if(url.find(".flv") != std::string::npos) {
        sendFlv(conn, url);
    } else {
       std::cout << "Unsupported request: " << url << std::endl;
    }
}
void HttpFlvSession::sendFlv(TcpConnection* conn, const std::string& path) {
    std::string fullPath = FLV_RESOURCE_DIR + path;
    std::cout << "Sending FLV: " << fullPath << std::endl;

    std::ifstream flvFile(fullPath, std::ios::binary);
    if(!flvFile.is_open()) {
        std::cerr   << "Failed to open FLV file: " << fullPath << std::endl;
        std::string body = "File not found";
        std::string header = makeHttpHeader(
            404, "Not Found",
            "application/octet-stream",
            body.size(),
            "no-cache, no-store, must-revalidate"
        );
        conn->send(header + body);
        return;
    }

    std::string content(
        (std::istreambuf_iterator<char>(flvFile)),
        std::istreambuf_iterator<char>()
    );

    std::string header = makeHttpHeader(
        200, "OK",
        "video/x-flv",
        content.size(),
        "max-age=3600"
    );

    conn->send(header);
    conn->send(content);
}
std::string HttpFlvSession::makeHttpHeader(int status,
                    const std::string& statusText,
                    const std::string& contentType,
                    size_t contentLength,
                    const std::string& cacheControl,
                    bool keepAlive) {
    std::ostringstream oss;                    
    oss << "HTTP/1.1 " << status << " " << statusText << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        //<< "Content-Length: " << contentLength << "\r\n"
        << "Cache-Control: " << cacheControl << "\r\n"
        << (keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n")
        << "\r\n";
    return oss.str();
}