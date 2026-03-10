# HLS (HTTP Live Streaming) 协议详解

HLS 是由 Apple 公司提出的基于 HTTP 的流媒体网络传输协议。它是目前移动端支持最广泛、也是互联网上最主流的流媒体传输协议之一。

---

## 1. 核心机制：分片与索引

HLS 的核心思想是将连续的媒体流切分成若干个**小的媒体片段**，并使用一个**索引文件**来管理这些片段的播放顺序。

### 核心组件
- **TS 切片 (Transport Stream)**：
  - 承载实际音视频数据的容器（通常以 `.ts` 结尾）。
  - 每个切片是一个独立的媒体文件，包含若干秒（如 2-10s）的完整数据。
- **M3U8 索引 (Playlist)**：
  - 基于文本的播放列表文件。
  - 记录了切片的下载地址、播放时长、序号等元数据。
  - 客户端通过不断解析 M3U8 文件来获取新的分片地址。

---

## 2. M3U8 文件结构剖析

M3U8 存在两种级别：**一级主播放列表 (Master Playlist)** 和 **二级媒体播放列表 (Media Playlist)**。

### 2.1 媒体播放列表 (Media Playlist)
这是最常用的索引文件，直接指向具体的 TS 切片。

```m3u8
#EXTM3U                      // 必须：标识文件起始
#EXT-X-VERSION:3             // 协议版本
#EXT-X-TARGETDURATION:6      // 强制：切片最大时长上限 (秒)
#EXT-X-MEDIA-SEQUENCE:100    // 直播必选：当前列表中第一个切片的序号
#EXT-X-PLAYLIST-TYPE:EVENT   // 可选：VOD (点播) 或 EVENT (回放)

#EXTINF:5.000,               // 当前切片的实际时长
segment_100.ts               // 切片的相对路径或绝对 URL
#EXTINF:5.012,
segment_101.ts

#EXT-X-ENDLIST               // 点播必选：标识流已结束，若无此标签则视为直播
```

### 2.2 主播放列表 (Master Playlist)
用于实现 **多码率自适应 (ABR)**。它不包含切片，而是指向多个不同码率的二级 M3U8。

```m3u8
#EXTM3U
#EXT-X-STREAM-INF:BANDWIDTH=1280000,RESOLUTION=1280x720
720p.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=2560000,RESOLUTION=1920x1080
1080p.m3u8
```

---

## 3. HLS 交互流程

1. **握手与加载**：客户端通过 HTTP GET 请求 M3U8 文件。
2. **解析与缓冲**：客户端解析索引，按序下载前几个 TS 切片。为了保证流畅性，播放器通常会缓冲 3 个以上的切片后才开始渲染。
3. **滚动更新 (直播模式)**：
   - 客户端定时（通常为 `TargetDuration` 的一半时间）重新请求 M3U8。
   - 服务器在 M3U8 尾部追加新切片，并移除头部过期的旧切片。
   - 客户端通过对比 `MEDIA-SEQUENCE` 识别新内容。

---

## 4. 服务端实现要点 (HTTP Response)

实现 HLS 服务端时，必须严格遵守以下 HTTP 规范，否则会导致播放器无法解析或跨域失败。

### 4.1 关键 MIME 类型
| 扩展名 | Content-Type |
| :--- | :--- |
| `.m3u8` | `application/vnd.apple.mpegurl` |
| `.ts` | `video/MP2T` |

### 4.2 跨域处理 (CORS)
由于 HLS 常在 Web 端通过 `hls.js` 播放，必须允许跨域：
- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Headers: Range`
- `Access-Control-Expose-Headers: Content-Length`

### 4.3 缓存策略 (Cache-Control)
- **M3U8 (直播)**：**严禁缓存**。客户端必须每次拿到最新的索引。
  - `Cache-Control: no-cache, no-store, must-revalidate`
- **TS 切片**：**建议长时间缓存**。切片内容不可变，缓存可极大地减轻服务器负载。
  - `Cache-Control: max-age=3600`
