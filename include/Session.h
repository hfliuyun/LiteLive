#ifndef __SESSION_H__
#define __SESSION_H__
#include <memory>
#include <string>
class LiveServer;
class TcpConnection;

class Session {
public:
    virtual void onMessage(TcpConnection* conn, std::string& readBuffer) = 0;
    virtual void setLiveServer(LiveServer* liveServer) = 0;
    virtual void onDisconnect(TcpConnection* conn) {} // 客户端断开时的回调
    virtual ~Session() = default;
};
#endif // __SESSION_H__