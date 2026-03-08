#ifndef _HTTPHLSSESSION_H__
#define _HTTPHLSSESSION_H__
#include <string>
#include <vector>
#include "Session.h"

#define HLS_RESOURCE_DIR "../../data/test"
class TcpConnection;

class HttpHlsSession  : public Session {

public:
    void onMessage(TcpConnection* conn, std::string& readBuffer) override;
    void HandleRequest(TcpConnection* conn, const std::string& request);
    void sendM3u8(TcpConnection* conn, const std::string& path);
    void sendTs(TcpConnection* conn, const std::string& path);
    std::string makeHttpHeader(int status,
                        const std::string& statusText,
                        const std::string& contentType,
                        size_t contentLength,
                        const std::string& cacheControl,
                        bool keepAlive = true);
};




#endif //_HTTPHLSSESSION_H__

