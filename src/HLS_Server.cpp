#include "EpollServer.h"
#include "HttpHlsSession.h"
#include "TcpConnection.h"
#include <signal.h> 

int main(int argc, char* argv[]) {
    std::cout << "Starting HLS Server..." << std::endl;
#if 0 
    int port = 8080; // 默认端口
#else
    int port = 1935;
#endif
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    signal(SIGPIPE, SIG_IGN); // 忽略 SIGPIPE 信号，防止写入关闭的 socket 导致进程终止
    EpollServer server(port);
    server.run();
    return 0;
}