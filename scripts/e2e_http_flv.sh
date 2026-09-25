#!/bin/bash
# E2E：RTMP 推流 -> HTTP-FLV 拉流（真 ffmpeg + 真服务，无 mock）
#
# 用法：
#   ./scripts/e2e_http_flv.sh                           # 成功路径，全绿 exit 0
#   ./scripts/e2e_http_flv.sh --fail-stage <server|publish|pull|verify>
#   PORT=19351 ./scripts/e2e_http_flv.sh                 # 默认 19350，避开 1935
# 前置：build/LiveLite 已编译；ffmpeg（含 libx264）、curl、python3 在 PATH
# 四阶段与退出码：server=1 publish=2 pull=3 verify=4；阶段名即诊断
# 时序：publish 转后台，服务端日志出现 publish 落表（就绪门）+ 保底 sleep 3 后再 pull；
#   pull 必须在 ffmpeg 退出前完成，否则流被擦除，只能拉到 13 字节 FLV 空头
# 判据：curl 跑满 --max-time 退出码 28 属正常（服务端无 Content-Length 不主动关连接），
#   须再加 body 非空；verify 验 FLV 魔数 + Tag 数（≥5），防“退出 0 但 0 字节”假绿
# 清理：trap EXIT 杀服务进程、删拉流文件；终端输出即证据

set -u

PORT="${PORT:-19350}"
STREAM="e2e"
BIN="./build/LiveLite"
OUT="data/e2e_pull.flv"

FAIL_STAGE="ok"
if [ "${1:-}" = "--fail-stage" ]; then
    FAIL_STAGE="${2:?need stage name:server|publish|pull|verify}"
fi

# 脚本在哪执行都行：先切到仓库根（scripts 的上一级）
cd "$(dirname "$0")/.." || exit 1

SERVER_PID=""
FF_PID=""

log() { echo "[e2e] stage=$1 ... $2"; }
fail() { echo "[e2e] FAIL stage=$1:$2"; exit "$3"; }

cleanup() {
    [ -n "$FF_PID" ] && kill "$FF_PID" 2>/dev/null
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
    wait 2>/dev/null
    rm -f "$OUT"
}

trap cleanup EXIT

wait_port() {
    for _ in $(seq 1 50); do
        python3 -c "import socket; s=socket.socket();s.settimeout(0.5);s.connect(('127.0.0.1',$PORT));s.close()" 2>/dev/null && return 0
        sleep 0.2
    done
    return 1
}


[ "$FAIL_STAGE" = "server" ] && BIN="./build/NoSuchBinary"

# ---- stage=server ----
[ -x "$BIN" ] || fail server "binary $BIN 不存在或不可执行" 1
"$BIN" "$PORT" > /tmp/e2e_server.log 2>&1 &
SERVER_PID=$!
wait_port || fail server "端口 $PORT 10s 内不可连 (见/tmp/e2e_server.log)" 1
kill -0 "$SERVER_PID" 2>/dev/null || fail server "服务进程已退出（见 /tmp/e2e_server.log）" 1
log server OK

# ---- stage=publish ----
PUBLISH_URL="rtmp://127.0.0.1:$PORT/live/$STREAM"
[ "$FAIL_STAGE" = "publish" ] && PUBLISH_URL="rtmp://127.0.0.1:1/live/$STREAM"

ffmpeg -hide_banner -loglevel error -nostdin -re \
       -f lavfi -i "testsrc=size=320x240:rate=15:duration=8" \
       -f lavfi -i "sine=frequency=440:duration=8" \
       -t 8 -c:v libx264 -pix_fmt yuv420p -preset ultrafast -tune zerolatency \
       -c:a aac -ar 44100 -ac 2 -f flv "$PUBLISH_URL" > /tmp/e2e_ffmpeg.log 2>&1 &
FF_PID=$!
sleep 1
kill -0 "$FF_PID" 2>/dev/null || fail publish "ffmpeg 启动即退出（见 /tmp/e2e_ffmpeg.log）" 2
log publish STARTED

# 给推流 3 秒建流+走媒体（play 要求流已存在，见头注释）
sleep 3

# pull 之前加就绪门：轮询服务端日志，看到 publish 落表再走（sleep 3 留着当保底）
for _ in $(seq 1 30); do
    grep -a -q "Handling 'publish' command" /tmp/e2e_server.log 2>/dev/null && break
    sleep 0.5
done

# ---- stage=pull ----
PULL_URL="http://127.0.0.1:$PORT/live/$STREAM.flv"
[ "$FAIL_STAGE" = "pull" ] && PULL_URL="http://127.0.0.1:1/live/$STREAM.flv"

curl -s --max-time 5 -o "$OUT" "$PULL_URL"
CURL_CODE=$?
if [ "$CURL_CODE" -ne 0 ] && [ "$CURL_CODE" -ne 28 ]; then
    fail pull "curl 退出码 ${CURL_CODE}（28=跑满时限属正常）" 3
fi

[ -s "$OUT" ] || fail pull "body 0 字节" 3
log pull OK


# 等推流自然结束，确认 ffmpeg 退出码
wait "$FF_PID"
FF_CODE=$?
FF_PID=""
[ "$FF_CODE" -eq 0 ] || fail publish "ffmpeg 退出码 ${FF_CODE}（见 /tmp/e2e_ffmpeg.log）" 2
log publish OK

# ---- stage=verify ----
[ "$FAIL_STAGE" = "verify" ] && head -c 13 "$OUT" > "$OUT.bad" && mv "$OUT.bad" "$OUT"
MIN_TAGS=5
python3 - "$OUT" "$MIN_TAGS" << 'EOF'
import sys
path, min_tags = sys.argv[1], int(sys.argv[2])
data = open(path, 'rb').read()
assert data[:3] == b'FLV', f"bad magic {data[:3]!r}"
assert data[3] == 0x01,f"bad version {data[3]}"
assert len(data) > 13,f"body only {len(data)}B, no tags"
n,off = 0, 13
while off + 11 <= len(data):
    size = int.from_bytes(data[off+1:off+4], 'big')
    off += 11 + size + 4
    if off > len(data):
        break
    n += 1
print(f"[e2e] file={len(data)}B tags={n}")
assert n >= min_tags, f"tags {n} < {min_tags}"
EOF
[ $? -eq 0 ] || fail verify "FLV 校验失败" 4
log verify OK

echo "[e2e] ALL GREEN"
