#include "HttpHlsSession.h"
#include "TcpConnection.h"
#include "EpollServer.h"

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
    EpollServer server(port);
    server.run();
    return 0;

}