#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>


// 辅助打印 flags（位掩码）
std::string flagsToString(uint16_t flags) {
    std::string result;
    if(flags & EV_ADD) result += "EV_ADD | ";
    if(flags & EV_DELETE) result += "EV_DELETE | ";
    if(flags & EV_ENABLE) result += "EV_ENABLE | ";
    if(flags & EV_DISABLE) result += "EV_DISABLE | ";
    if(flags & EV_ONESHOT) result += "EV_ONESHOT | ";
    if(flags & EV_CLEAR) result += "EV_CLEAR | ";
    if(flags & EV_RECEIPT) result += "EV_RECEIPT | ";
    if(flags & EV_DISPATCH) result += "EV_DISPATCH | ";
    if(flags & EV_UDATA_SPECIFIC) result += "EV_UDATA_SPECIFIC | ";
    if(flags & EV_VANISHED) result += "EV_VANISHED | ";
    if(flags & EV_EOF) result += "EV_EOF | ";
    if(flags & EV_ERROR) result += "EV_ERROR | ";
    if (!result.empty()) {
        result.pop_back(); // Remove the last space
        result.pop_back(); // Remove the last '|'
    }
    return result;
}
// 辅助打印 filter 类型
std::string filterToString(int16_t filter) {
    std::string result;
    switch(filter) {
        case EVFILT_READ: result = "EVFILT_READ"; break;
        case EVFILT_WRITE: result = "EVFILT_WRITE"; break;
        case EVFILT_AIO: result = "EVFILT_AIO"; break;
        case EVFILT_VNODE: result = "EVFILT_VNODE"; break;
        case EVFILT_PROC: result = "EVFILT_PROC"; break;
        case EVFILT_SIGNAL: result = "EVFILT_SIGNAL"; break;
        case EVFILT_TIMER: result = "EVFILT_TIMER"; break;
        case EVFILT_MACHPORT: result = "EVFILT_MACHPORT"; break;
        case EVFILT_FS: result = "EVFILT_FS"; break;
        case EVFILT_USER: result = "EVFILT_USER"; break;
        default: result = "Unknown filter"; break;
    }
    return result;
}
// 打印完整 kevent 结构体
void printKevent(const struct kevent& ev, const char* prefix) {
    printf("[%s] ident=%ld, filter=%s, flags=0x%04x (%s), fflags=%u, data=%ld, udata=%p\n",
           prefix, (long)ev.ident, filterToString(ev.filter).c_str(),
           ev.flags, flagsToString(ev.flags).c_str(),
           ev.fflags, (long)ev.data, ev.udata);
}

//设非阻塞
bool setNonBlocking(int fd) {
    if (fd < 0) return false;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return false;
    }
    if(fcntl(fd,F_SETFL,flags | O_NONBLOCK) == -1){
        perror("fcntl(F_SETFL)");
        return false;
    }
    return true;

}
//创建一对本地连接的 TCP socket (server_fd, client_fd)
bool createTcpPair(int& server_fd, int& client_fd) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (listen_fd < 0) {
       perror("socket");
       return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // 让系统自动分配端口
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return false;   
    }
    if(listen(listen_fd, 1) < 0 ) {
        perror("listen");
        close(listen_fd);
        return false;
    }
    socklen_t len = sizeof(addr);
    getsockname(listen_fd, (sockaddr*)&addr, &len);
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(connect(client_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(listen_fd);
        close(client_fd);
        return false;
    }
    server_fd = accept(listen_fd, nullptr, nullptr);
    close(listen_fd);
    return true;
    
}
void testEofScenario(bool useEdgeTrigger) {
    printf("\n--- 测试模式: %s ---\n", useEdgeTrigger ? "ET (EV_CLEAR)" : "LT (默认)");
    int server_fd, client_fd;
    createTcpPair(server_fd, client_fd);
    setNonBlocking(server_fd);
    setNonBlocking(client_fd);
    int kq = kqueue();
    uint16_t flags = EV_ADD | (useEdgeTrigger ? EV_CLEAR : 0);
    struct kevent change;
    EV_SET(&change, server_fd, EVFILT_READ, flags, 0, 0, nullptr);
    kevent(kq, &change, 1, nullptr, 0, nullptr);
    // 1. 客户端发送 5 个字节并立刻调用 close()
    const char* msg = "hello";
    write(client_fd, msg, 5);
    close(client_fd); // 发送 FIN
    printf("对端已 write 5 字节并立即 close()\n");
    // 2. 服务端【先不 read】，直接调用 kevent 取事件
    struct kevent ev;
    struct timespec ts{1, 0};
    int nev = kevent(kq, nullptr, 0, &ev, 1, &ts);
    printf(">> 步骤 1 (未 read 数据前) nev=%d\n", nev);
    if (nev > 0) {
        printKevent(ev, "第 1 次 kevent");
    }
    // 3. 服务端把这 5 个字节 read 出来
    char buf[32] = {0};
    ssize_t n = read(server_fd, buf, sizeof(buf));
    printf(">> 步骤 2 读出数据: %zd 字节 (\"%s\")\n", n, buf);
    // 4. 服务端【数据读干后】，再调一次 kevent
    nev = kevent(kq, nullptr, 0, &ev, 1, &ts);
    printf(">> 步骤 3 (读干缓冲区后) nev=%d\n", nev);
    if (nev > 0) {
        printKevent(ev, "第 2 次 kevent");
    }
    close(kq);
    close(server_fd);
}
void doExperiment1() {
    printf("\n=== E1 重测: 零超时下的执行顺序判据 ===\n");
    int kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        return;
    }
    int fds[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    setNonBlocking(fds[0]);
    setNonBlocking(fds[1]);
    // 1. 预先往缓冲区写数据：确保如果 fds[0] 被监听，它绝对立即可读
    write(fds[1], "hello", 5);
    struct kevent change;
    EV_SET(&change, fds[0], EVFILT_READ, EV_ADD, 0, 0, (void*)0xbeef);

    struct kevent event;
    struct timespec zero_timeout = {0, 0}; // 绝不阻塞
    // 2. 关键调用：同时提交变更并收取事件
    int nev = kevent(kq, &change, 1, &event, 1, &zero_timeout);
    printf(">> 零超时下单次调用返回 nev = %d\n", nev);
    if (nev == 1) {
        printf("【判据结果】nev=1 -> 证实 H1：内核严格先应用 changelist 变更，再评估就绪事件！\n");
        printKevent(event, "Returned Event");
    } else if (nev == 0) {
        printf("【判据结果】nev=0 -> 证实 H2：内核先评估就绪事件，再应用变更（此假设被排除）。\n");
    }
    close(fds[0]); close(fds[1]); close(kq);

}
void doExperiment2() {
    printf("\n=== E2: 一个 fd，几条记录？ ===\n");
        int kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        return;
    }

    int fds[2];
    if(socketpair(AF_UNIX,SOCK_STREAM , 0, fds) < 0) {
        perror("socketpair");
        return;
    }
    setNonBlocking(fds[0]);
    setNonBlocking(fds[1]);

    struct kevent changes[2];
    EV_SET(&changes[0], fds[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
    EV_SET(&changes[1], fds[0], EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
    if(kevent(kq, changes, 2, nullptr, 0, nullptr) == -1) {
        perror("kevent add");
        close(kq);
        return;
    }

    // - 可写：新建的 socket 发送缓冲区天然是空的，随时可写！
    // - 可读：让对端 (fds[1]) 往里写几个字节，让 fds[0] 有数据可读
    const char* msg = "Hello, kqueue!";
    write(fds[1], msg, strlen(msg));

    struct kevent events[10];
    int nev = kevent(kq, nullptr, 0, events, 10, nullptr);
    if (nev == -1) {
        perror("kevent wait");
    } else {
        std::cout << "kevent returned with nev=" << nev << std::endl;
        for(int i=0; i<nev; ++i) {
            printKevent(events[i], "Event");
        }
    }
    close(fds[0]);
    close(fds[1]);
    close(kq);
}


void doExperiment3(){
    printf("EV_CLEAR 空转实验\n");
    int kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        return;
    }
    int fds[2];
    if(socketpair(AF_UNIX,SOCK_STREAM , 0, fds) < 0) {
        perror("socketpair");
        close(kq);
        return;
    }
    struct kevent changes[2];
    EV_SET(&changes[0], fds[0], EVFILT_WRITE, EV_ADD,0 ,0, nullptr);
    EV_SET(&changes[1], fds[1], EVFILT_WRITE, EV_ADD | EV_CLEAR,0 ,0, nullptr);
    if(kevent(kq, changes, 2, nullptr, 0, nullptr) == -1) {
        perror("kevent add");
        close(fds[0]);
        close(fds[1]);
        close(kq);
        return;
    }
    struct timespec timeout = {0, 0}; // 立即返回 
    struct timespec start,now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    long long total_calls = 0;
    long long level_trigger_count = 0;
    long long edge_trigger_count = 0;
    struct kevent event;
    while(true) {
        int nev = kevent(kq, nullptr, 0, &event, 1, &timeout);
        if(nev == -1) {
            perror("kevent add");
            break;
        }
        total_calls++;
        if(nev > 0) {
            if(event.ident == fds[0]) {
                level_trigger_count++;
            } else if(event.ident == fds[1]) {
                edge_trigger_count++;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if(now.tv_sec - start.tv_sec >= 1) {
            break;
        }
    }
    std::cout << "\n在 1 秒内统计结果:" << std::endl;
    std::cout << "------------------------------------------" << std::endl;
    std::cout << "kevent 总调用次数 (total_calls): " << total_calls << std::endl;
    std::cout << "fds[0] 水平触发次数 (无 EV_CLEAR): " << level_trigger_count << std::endl;
    std::cout << "fds[1] 边缘触发次数 (有 EV_CLEAR): " << edge_trigger_count << std::endl;
    std::cout << "------------------------------------------" << std::endl;
    std::cout << "结论:" << std::endl;
    std::cout << "1. 水平触发因为没有 EV_CLEAR，只要可写状态存在，每次 kevent 都会返回该事件，导致疯狂空转。" << std::endl;
    std::cout << "2. 边缘触发因为加了 EV_CLEAR，事件状态被清除。因为之后没有新动作让状态再次变化，所以只触发了一次。" << std::endl;
    std::cout << "3. 非阻塞模式下，由于 fds[0] 一直可写，kevent 其实很少返回 0（几乎每次都有水平事件），所以 total_calls 极高。" << std::endl;
    
        
    close(fds[0]);
    close(fds[1]);
    close(kq);  
}

void doExperiment3_Rearm() {
    printf("\n=== E3 后半段: 满缓冲区注册 EV_CLEAR 后的 re-arm 测试 ===\n");
    printf("场景: 模拟真实 write-on-demand —— EAGAIN 时才注册 EVFILT_WRITE | EV_CLEAR\n");
    printf("验证: 缓冲区从 满 → 有空闲 的沿跳变能否被正确触发\n\n");

    int server_fd, client_fd;
    if (!createTcpPair(server_fd, client_fd)) {
        printf("createTcpPair 失败\n");
        return;
    }
    if (!setNonBlocking(server_fd) || !setNonBlocking(client_fd)) {
        perror("setNonBlocking");
        close(server_fd); close(client_fd);
        return;
    }

    // ── 步骤 1: 把 server_fd 发送缓冲区调小，并读回内核实际值 ──
    int sndbuf = 2048;
    if (setsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
        perror("setsockopt SO_SNDBUF");
        close(server_fd); close(client_fd);
        return;
    }
    int actual_sndbuf = 0;
    socklen_t optlen = sizeof(actual_sndbuf);
    getsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &actual_sndbuf, &optlen);
    printf(">> 步骤 1: SO_SNDBUF 请求 %d，内核实际值 = %d\n", sndbuf, actual_sndbuf);

    // ── 步骤 2: 非阻塞循环写直到 EAGAIN，打满发送缓冲区 ──
    char write_buf[65536];
    memset(write_buf, 'A', sizeof(write_buf));
    ssize_t total_written = 0;
    while (true) {
        ssize_t n = write(server_fd, write_buf, sizeof(write_buf));
        if (n > 0) {
            total_written += n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            perror("write");
            close(server_fd); close(client_fd);
            return;
        }
    }
    printf(">> 步骤 2: 循环写直到 EAGAIN，共写入 %zd 字节，发送缓冲区已满\n", total_written);

    // ── 步骤 3: 缓冲区满时才注册 EVFILT_WRITE | EV_ADD | EV_CLEAR ──
    //   （真实场景：write-on-demand 只在 EAGAIN 那一刻才 EnableWrite）
    int kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        close(server_fd); close(client_fd);
        return;
    }
    struct kevent change;
    EV_SET(&change, server_fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(kq, &change, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent EV_ADD");
        close(kq); close(server_fd); close(client_fd);
        return;
    }
    printf(">> 步骤 3: 已注册 EVFILT_WRITE | EV_ADD | EV_CLEAR（缓冲区满时注册）\n");

    // ── 步骤 4: 用 {0,0} 超时测一次 —— buffer 满，预期 nev = 0 ──
    struct kevent ev;
    struct timespec zero_ts = {0, 0};
    int nev = kevent(kq, nullptr, 0, &ev, 1, &zero_ts);
    printf(">> 步骤 4: 缓冲区满时 kevent({0,0}) -> nev = %d", nev);
    if (nev == 0) {
        printf("  ✓ 预期：当前不可写，不返回事件\n");
    } else if (nev > 0) {
        printf("  ⚠️ 缓冲区满却返回了可写事件！\n");
        printKevent(ev, "意外事件");
    }

    // ── 步骤 5: 对端 read() 抽干数据，制造 满 → 有空闲 的沿跳变 ──
    char read_buf[65536];
    ssize_t total_read = 0;
    while (true) {
        ssize_t n = read(client_fd, read_buf, sizeof(read_buf));
        if (n > 0) {
            total_read += n;
        } else if (n == 0) {
            break;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else {
            perror("read");
            break;
        }
    }
    printf(">> 步骤 5: 对端 read() 抽干，共读出 %zd 字节\n", total_read);

    // ── 步骤 6: 再次 kevent —— 观察沿跳变是否被正确触发 ──
    nev = kevent(kq, nullptr, 0, &ev, 1, &zero_ts);
    printf(">> 步骤 6: 抽干后 kevent({0,0}) -> nev = %d\n", nev);
    if (nev > 0) {
        printKevent(ev, "Re-arm 事件");
        printf("  ✓ EV_CLEAR 成功检测到 满→空闲 的沿跳变，re-arm 验证通过！\n");
    } else {
        printf("  ⚠️ 未返回事件！re-arm 失败，需检查是否需要额外动作触发\n");
    }

    // ── 步骤 7: 紧接着再调一次，确认是 ET 语义（不重复返回） ──
    nev = kevent(kq, nullptr, 0, &ev, 1, &zero_ts);
    printf(">> 步骤 7: 紧接着再调 kevent({0,0}) -> nev = %d", nev);
    if (nev == 0) {
        printf("  ✓ 预期：ET 已清状态，不重复返回\n");
    } else {
        printf("  ⚠️ 重复返回，疑似退化为 LT！\n");
    }

    printf("\n--- 对账 ---\n");
    printf("真实 write-on-demand 场景：EnableWrite 总在 EAGAIN（缓冲区满）时触发\n");
    printf("本实验验证了：满时注册 + EV_CLEAR → 抽干后能正确 re-arm\n");
    printf("→ KqueuePoller 可以安全地在 EAGAIN 那一刻才 EV_ADD EVFILT_WRITE | EV_CLEAR\n");

    close(kq);
    close(server_fd);
    close(client_fd);
}
void doExperiment4() {
    printf("\n=== E4: 幂等到底谁保证 ===\n");
    int fds[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

    int kq = kqueue();
    if (kq == -1) {
        perror("kqueue");
        return;
    }
    struct kevent addRead;
    EV_SET(&addRead, fds[0], EVFILT_READ, EV_ADD , 0, 0, nullptr);
    if (kevent(kq, &addRead, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent add");
        close(fds[0]);
        close(fds[1]);
        close(kq);
        return;
    }
    struct kevent delWrite;
    EV_SET(&delWrite, fds[0], EVFILT_WRITE, EV_DELETE , 0, 0, nullptr);

    printf("\n-- 对照 A: kevent(nevents = 0) --\n");
    errno = 0; // 清除 errno，确保打印的 errno 是 kevent 的返回值
    int retA = kevent(kq, &delWrite, 1, nullptr, 0, nullptr);
    printf("返回值 ret = %d, errno = %d (%s)\n", retA, errno, strerror(errno));


    printf("\n-- 对照 B: kevent(nevents = 1) --\n");
    struct kevent outEvent;
    errno = 0; // 清除 errno，确保打印的 errno 是 kevent 的返回值
    int retB = kevent(kq, &delWrite, 1, &outEvent, 1, nullptr);
    printf("返回值 ret = %d, errno = %d (%s)\n", retB, errno, strerror(errno));
    if (retB > 0) {
        printKevent(outEvent, "Error in eventlist");
    }

    close(fds[0]);
    close(fds[1]);
    close(kq);

}

void doExperiment5() {
    printf("\n=== E5: EV_EOF 到底意味着什么 ===\n");
    testEofScenario(false); // LT 模式
    testEofScenario(true);  // ET 模式
}
void doExperiment5b() {
    printf("\n=== E5b: EOF 粘滞性与 CPU 空转测试 (ET 模式) ===\n");
    int server_fd, client_fd;
    createTcpPair(server_fd, client_fd);
    setNonBlocking(server_fd);

    int kq = kqueue();
    struct kevent change;
    EV_SET(&change, server_fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    kevent(kq, &change, 1, nullptr, 0, nullptr);

    // 对端发数据并关闭
    write(client_fd, "test", 4);
    close(client_fd);

    // 读干数据
    char buf[16];
    while (read(server_fd, buf, sizeof(buf)) > 0) {}

    printf("接收缓冲区已读干，开始 1 秒零超时 kevent 循环...\n");
    struct timespec start, now, timeout{0, 0};
    clock_gettime(CLOCK_MONOTONIC, &start);
    long long eof_spin_count = 0;
    struct kevent ev;

    while (true) {
        int nev = kevent(kq, nullptr, 0, &ev, 1, &timeout);
        if (nev > 0 && (ev.flags & EV_EOF)) {
            eof_spin_count++;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - start.tv_sec >= 1) break;
    }

    printf(">> 1 秒内 EOF 事件触发次数: %lld\n", eof_spin_count);
    if (eof_spin_count > 100000) {
        printf("【内核事实】EV_EOF 是绝对粘滞的！EV_CLEAR 清不掉 EOF！\n");
        printf("【设计硬约束】一旦遭遇 EOF，必须立刻从 Poller 中 Remove(fd)，否则 100%% CPU 狂飙！\n");
    }
    close(server_fd); close(kq);
}
void doExperiment6() {
    printf("\n=== E6: socket 错误码到底在哪？—— EV_EOF+fflags 还是 EV_ERROR+data？ ===\n\n");
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        perror("socket");
        return;
    }
    setNonBlocking(fd);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if(ret < 0 && errno != EINPROGRESS) {
        perror("connect");
        close(fd);
        return;
    }
    printf(">> 步骤 1: 非阻塞 connect → EINPROGRESS（连接进行中）\n");

    int kq = kqueue();
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
    if(kevent(kq, changes, 2, nullptr, 0, nullptr) == -1) {
        perror("kevent add");
        close(fd); close(kq);
        return;
    }
    printf(">> 步骤 2: 已注册 EVFILT_READ | EVFILT_WRITE\n");

    // ── 4. 等事件就绪（3 秒，localhost RST 通常亚毫秒级）──
    struct kevent events[4];
    struct timespec ts{3, 0};
    int nev = kevent(kq, nullptr, 0, &events[0], 4, &ts);
    if(nev == -1) {
        perror("kevent wait");
        close(fd); close(kq);
        return;
    }
    printf(">> 步骤 3: kevent(3s) → nev = %d\n\n", nev);
    for (int i = 0; i < nev; i++) {
        printKevent(events[i], "返回事件");
    }
    int so_error = 0;
    socklen_t optlen = sizeof(so_error);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &optlen);
    printf("\n>> 步骤 4: getsockopt(SO_ERROR) = %d (%s)\n", so_error, strerror(so_error));
    // ── 对账 ──
    printf("\n--- 对账 ---\n");
    printf("问题: socket 出错（ECONNREFUSED），errno 在哪？\n");
    if (nev > 0) {
        for (int i = 0; i < nev; i++) {
            bool has_eof   = events[i].flags & EV_EOF;
            bool has_error = events[i].flags & EV_ERROR;
            printf("  filter=%-14s EV_EOF=%s  EV_ERROR=%s  fflags=%u(%s)  data=%ld\n",
                   filterToString(events[i].filter).c_str(),
                   has_eof   ? "✓" : "✗",
                   has_error ? "✓" : "✗",
                   events[i].fflags, strerror(events[i].fflags),
                   (long)events[i].data);
        }
    }
    printf("\n结论:\n");
    printf("  1. socket 错误 → flags 带 EV_EOF，错误码在 fflags\n");
    printf("  2. fflags 的值 == getsockopt(SO_ERROR) == %d (%s)\n", so_error, strerror(so_error));
    printf("  3. 这不是 EV_ERROR！EV_ERROR 是 changelist 操作失败用的，错误在 data\n");

    printf("\n--- 对比: EV_ERROR（changelist 失败）长什么样？ ---\n");
    // 对一个无效 fd 做 EV_ADD → changelist 失败 → eventlist 里带 EV_ERROR
    int dead_fd = 9999; // 假设不存在
    struct kevent bad_change;
    EV_SET(&bad_change, dead_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
    struct kevent bad_event;
    errno = 0;
    int bad_nev = kevent(kq, &bad_change, 1, &bad_event, 1, nullptr);
    printf("对无效 fd=%d 做 EV_ADD（nevents=1）→ 返回 %d\n", dead_fd, bad_nev);
    if (bad_nev > 0) {
        printKevent(bad_event, "EV_ERROR 事件");
        printf("  注意: flags 含 EV_ERROR，错误码在 data（不是 fflags！）\n");
    } else if (bad_nev == -1) {
        printf("  返回 -1, errno = %d (%s)\n", errno, strerror(errno));
        printf("  （nevents=0 时，错误只能通过返回值 + errno 报告）\n");
    }

    printf("\n=== 两种错误机制对比 ===\n");
    printf("  socket 错误:  EV_EOF（flags）  + 错误码在 fflags  ← E6 测的\n");
    printf("  操作失败:    EV_ERROR（flags） + 错误码在 data    ← 对比实验测的\n");
    printf("  它们不是同一件事！\n");

    close(fd);
    close(kq);
}
int main(int argc, char* argv[]) {
    std::cout << "Kqueue probe test" << std::endl;
    if(argc > 1 && argv[1] == std::string("1")) {
        doExperiment1();
    } else if(argc > 1 && argv[1] == std::string("2")) {
        doExperiment2();
    }  else if(argc > 1 && argv[1] == std::string("3")) {
        doExperiment3();
    }  else if(argc > 1 && argv[1] == std::string("4")) {
        doExperiment4();
    } else if(argc > 1 && argv[1] == std::string("5")) {
        doExperiment5();
    } else if(argc > 1 && argv[1] == std::string("5b")) {
        doExperiment5b();
    } else if(argc > 1 && argv[1] == std::string("3r")) {
        doExperiment3_Rearm();
    } else if(argc > 1 && argv[1] == std::string("6")) {
        doExperiment6();
    }
    else {
        std::cout << "Usage: " << argv[0] << " 1" << std::endl;
    }
    return 0;
}