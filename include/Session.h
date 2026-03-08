#ifndef __SESSION_H__
#define __SESSION_H__
#include <memory>
#include <string>
class EpollServer;
class TcpConnection;

class Session {
public:
    virtual void onMessage(TcpConnection* conn, std::string& readBuffer) = 0;
    virtual void setEpollServer(EpollServer* epollServer) = 0;
    virtual ~Session() = default;
};
#endif // __SESSION_H__