#ifndef  __HTTPFLVSESSION_H__
#define __HTTPFLVSESSION_H__

#include <string>
#include <vector>
#include "Session.h"

#define FLV_RESOURCE_DIR "../../data"
class TcpConnection;

class HttpFlvSession  : public Session {
public:
    void onMessage(TcpConnection* conn, std::string& readBuffer) override;
    void HandleRequest(TcpConnection* conn, const std::string& request);
    void sendFlv(TcpConnection* conn, const std::string& path);
    std::string makeHttpHeader(int status,
                        const std::string& statusText,
                        const std::string& contentType,
                        size_t contentLength,
                        const std::string& cacheControl,
                        bool keepAlive = true);
};

#endif // __HTTPFLVSESSION_H__