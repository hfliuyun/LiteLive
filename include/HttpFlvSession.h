#ifndef __HTTPFLVSESSION_H__
#define __HTTPFLVSESSION_H__

#include "LiveServer.h"
#include "Session.h"
#include <string>
#include <vector>

#define FLV_RESOURCE_DIR "../../data"
class TcpConnection;

class HttpFlvSession : public Session {
private:
    void sendFlv(TcpConnection* conn, const std::string& path);
    std::string makeHttpHeader(int status, const std::string& statusText, const std::string& contentType,
                               size_t contentLength, const std::string& cacheControl, bool keepAlive = true);

public:
    void onMessage(TcpConnection* conn, std::string& readBuffer) override;
    void HandleRequest(TcpConnection* conn, const std::string& request);
    void setLiveServer(LiveServer* liveServer) override { liveServer_ = liveServer; }
    void onDisconnect(TcpConnection* conn) override;

private:
    LiveServer* liveServer_ = nullptr;
};

#endif // __HTTPFLVSESSION_H__