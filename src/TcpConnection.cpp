#include "TcpConnection.h"
#include "EpollServer.h"
#include "HttpFlvSession.h"
#include "RtmpSession.h"
#include "Session.h"
#include <sys/epoll.h>
void TcpConnection::handldRead() {
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
        session_->setEpollServer(epollServer_);
    }
    session_->onMessage(this, readBuffer_);
}

void TcpConnection::send(const std::string& data) {
    writeBuffer_.append(data);
    handleWrite();
}

void TcpConnection::handleWrite() {
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
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 内核缓冲区满，等下次 EPOLLOUT
                epoll_event event{};
                event.events = EPOLLOUT | EPOLLIN;
                event.data.fd = fd_;
                epoll_ctl(epollServer_->getEpollFd(), EPOLL_CTL_MOD, fd_, &event);
                return;
            }
            std::perror("write error");
            // 发送错误
            CloseConnection();
            return;
        }
    }
    if (writeBuffer_.empty()) {
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = fd_;
        epoll_ctl(epollServer_->getEpollFd(), EPOLL_CTL_MOD, fd_, &event);
    }
}

void TcpConnection::CloseConnection() {
    if (fd_ < 0)
        return;
    close(fd_);
    epoll_ctl(epollServer_->getEpollFd(), EPOLL_CTL_DEL, fd_, nullptr);
    fd_ = -1;
    readBuffer_.clear();
    writeBuffer_.clear();
}