# HTTP-FLV 协议与流式传输详解

HTTP-FLV 是一种将音视频数据封装成 FLV (Flash Video) 格式，并通过 HTTP 协议进行流式传输的方案。它结合了 HTTP 协议的通用性与 FLV 格式的低延迟特性，是目前国内直播行业最主流的网页端直播方案。

---

## 1. 核心原理：边下边播

HTTP-FLV 的核心在于利用了 HTTP 的 **持久连接 (Keep-Alive)** 特性。
- **与 HLS 的区别**：HLS 是通过不断请求新的切片文件（.ts）实现播放，而 HTTP-FLV 是**发起一次 HTTP GET 请求后，服务端永远不主动关闭连接**，而是像管道一样，源源不断地将实时生成的 FLV Tag 数据推送到客户端。
- **低延迟优势**：由于省去了 HLS 频繁切片和请求索引文件的开销，HTTP-FLV 的延迟通常可以控制在 **1s - 3s** 以内。

---

## 2. FLV 报文结构

一个标准的 HTTP-FLV 流由三部分组成：

### 2.1 FLV Header (9 字节)
标识文件格式、版本以及是否存在音视频流。
- `Signature`: "FLV" (3 bytes)
- `Version`: 0x01 (1 byte)
- `TypeFlags`: 0x05 (音频+视频) 或 0x01 (仅视频) (1 byte)
- `DataOffset`: 0x00000009 (Header 长度) (4 bytes)

### 2.2 PreviousTagSize0 (4 字节)
在 Header 之后必须紧跟一个 4 字节的全 0 字段，表示前一个 Tag 的长度为 0。

### 2.3 FLV Tags (循环叠加)
后续所有的音视频数据都封装在一个个 Tag 中，每个 Tag 包含：
- **Tag Header (11 字节)**：包含 Tag 类型（音频/视频/脚本）、Data 大小、时间戳。
- **Tag Data**：实际的媒体数据。
- **PreviousTagSize (4 字节)**：紧跟在 Data 后面，记录当前这个 Tag 的总长度，方便播放器向后跳跃解析。

---

## 3. 服务端实现要点

### 3.1 关键响应头 (Response Header)
对于直播场景，必须注意以下字段：

- **`Content-Type: video/x-flv`**：这是播放器识别 FLV 流的唯一标识。
- **禁止 `Content-Length`**：直播流是无限长度的，绝对不能发送这个字段，否则浏览器接收完指定长度后会关闭连接。
- **`Access-Control-Allow-Origin: *`**：必须开启，否则网页端的 `flv.js` 无法跨域拉流。
- **`Cache-Control: no-cache`**：防止中间代理服务器缓存直播数据导致画面卡死。

### 3.2 示例响应报文

```http
HTTP/1.1 200 OK
Content-Type: video/x-flv
Access-Control-Allow-Origin: *
Connection: keep-alive
Cache-Control: no-cache
Expires: -1

[FLV Header] [PrevTagSize0]
[Script Tag (Metadata)]
[Video Sequence Header (AVC Header)]
[Audio Sequence Header (AAC Header)]
[Video Tag (Keyframe)]
... (持续推送后续 Tag)
```

---

## 4. 秒开与 GOP 缓存

为了让观众点击直播间时能“秒开”画面，服务端通常需要实现 **GOP 缓存**：
1. **缓存关键信息**：服务端在内存中缓存最近的一套 Metadata 和 Sequence Header（音视频解码参数）。
2. **缓存最近一个 GOP**：从最近的一个关键帧（I 帧）开始，缓存后续的所有数据包。
3. **瞬间下发**：当新观众连接时，服务端不等待下一个新包，而是立刻把缓存的“三件套 + GOP”全部通过 Socket 发给客户端，播放器拿到后可以立即解码出画面。
