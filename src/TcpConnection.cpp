#include "TcpConnection.h"
#include "LiveServer.h"
#include "HttpFlvSession.h"
#include "RtmpSession.h"
#include "Session.h"
#include <iostream>
void TcpConnection::handldRead() {
    if(isClose()) return;
    char buf[4096];
    while (true) {
        ssize_t n = read(fd_, buf, sizeof(buf));
        if (n > 0) {
            readBuffer_.append(buf, n);
        } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else if (n == 0) {
            CloseConnection();
            return;
        } else {
            CloseConnection();
            std::cout << " read error" << std::endl;
            return;
        }
    }
    if (!readBuffer_.empty() && session_ == nullptr) {
        if (readBuffer_[0] == 0x03) {
            // RTMP 握手协议第一个字节永远是 0x03
            std::cout << ">>> 嗅探到 RTMP 协议连接！" << std::endl;
            session_ = std::make_shared<RtmpSession>();
        } else {
            // 否则大概率是 HTTP 的 GET 或 OPTIONS (首字母 G 或 O)
            std::cout << ">>> 嗅探到 HTTP 协议连接！" << std::endl;
            session_ = std::make_shared<HttpFlvSession>();
        }
        session_->setLiveServer(liveServer_);
    }
    if(session_) session_->onMessage(this, readBuffer_);
}

void TcpConnection::send(const std::string& data) {
    if(isClose()) return;
    writeBuffer_.append(data);
    handleWrite();
}

void TcpConnection::handleWrite() {
    if(isClose()) return;
    if (writeBuffer_.empty())
        return;

    while (!writeBuffer_.empty()) {
        ssize_t n = write(fd_, writeBuffer_.data(), writeBuffer_.size());
        if (n > 0) {
            // 成功发送了 n 个字节，从缓冲区中移除
            writeBuffer_.erase(0, n);
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (writeBuffer_.size() > kWriteQueueLimit) {
                CloseConnection();
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 内核缓冲区满，等下次 EPOLLOUT
                liveServer_->poller()->EnableWrite(fd_);
                return;
            }
            std::perror("write error");
            // 发送错误
            CloseConnection();
            return;
        }
    }
    if (writeBuffer_.empty()) {
        liveServer_->poller()->DisableWrite(fd_);
    }
}

void TcpConnection::CloseConnection() {
    if(close_) return;
    close_ = true;
    // 通知上层业务进行清理
    if (session_) {
        session_->onDisconnect(this);
    }
    onCloseCb(fd_);
}