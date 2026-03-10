# ZebraStream 🦓

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++](https://img.shields.io/badge/C++-11-blue.svg)](https://en.cppreference.com/w/cpp/11)
[![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)](https://www.linux.org/)

**ZebraStream** 是一款基于 C++11 开发的高性能流媒体服务器。它采用 Linux **Epoll** 异步网络 IO 模型，旨在提供轻量、快速且易于扩展的直播分发服务。

目前支持从 **RTMP** 推流端接收音视频数据，并实时转封装为 **HTTP-FLV** 以及 **HLS** 格式供网页端和移动端拉流播放。

---

## 🚀 核心特性

- **高性能架构**：基于非阻塞 IO 和 Epoll 事件驱动模型，单线程即可支持高并发连接。
- **协议嗅探 (Protocol Sniffing)**：支持在同一端口上自动识别并分发 RTMP 与 HTTP 协议，极大简化了防火墙配置。
- **实时转封装**：核心支持 **RTMP -> HTTP-FLV** 的秒级延迟转发，以及 **HLS** 分片下发。
- **极致秒开体验**：内置 **GOP 缓存** 机制，确保观众进入直播间时能够立即解码出画面，告白长时间黑屏。

---

## 🛠️ 技术栈

- **语言标准**：C++11
- **网络模型**：Linux Epoll (ET 模式)
- **协议实现**：RTMP, HTTP, FLV, HLS (M3U8/TS)
- **构建工具**：CMake

---

## 📦 项目结构

```text
.
├── CMakeLists.txt        # 项目构建配置
├── include/              # 头文件目录
│   ├── EpollServer.h     # 网络核心组件
│   ├── RtmpSession.h     # RTMP 协议解析与状态机
│   ├── HttpFlvSession.h  # HTTP-FLV 转发逻辑
│   └── LiveStream.h      # 全局流管理与 GOP 缓存
├── src/                  # 源代码目录
├── doc/                  # 详细的技术细节文档 (RTMP/HLS/HTTP-FLV)
└── build/                # 编译生成目录
```

---

## 🔨 快速开始

### 1. 环境准备
确保你的 Linux 系统已安装：
- GCC 4.8+ 或 Clang
- CMake 3.10+
- Ninja 或 Make

### 2. 编译项目
```bash
git clone https://github.com/your-username/ZebraStream.git
cd ZebraStream
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 3. 运行服务器
```bash
# 启动服务器，默认监听 1935 端口
./HLS_Server 1935
```

### 4. 推流测试 (使用 FFmpeg 或 OBS)
```bash
flv  ffmpeg -re -i .\test.flv -vcodec h264 -acodec aac -f flv rtmp://127.0.0.1/live/test
```



### 5. 播放测试
- **HTTP-FLV**: `http://localhost:1935/live/test.flv` (推荐使用 `flv.js` 播放)
- **RTMP**: `rtmp://localhost:1935/live/test` (推荐使用 `ffplay` 或 `VLC`)

---
```bash
#ffplay 拉流(flv)
ffplay -i http://127.0.0.1:1935/live/test.flv

#rtmp
ffplay -i rtmp://127.0.0.1/live/test
```

## 📖 深度解析

如果你对协议的底层实现感兴趣，请查阅 `doc/` 目录下的详细文档：
- [RTMP 协议实现细节](./doc/RTMP.md)
- [HTTP-FLV 流式传输原理](./doc/HTTP_FLV.md)
- [HLS 分片技术手册](./doc/HLS.md)

---

## 📜 开源协议

本项目采用 [MIT](./LICENSE) 协议开源。
