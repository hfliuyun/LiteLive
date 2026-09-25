#!/bin/bash
# Day 13 负载脚本：固定参数（N=8 路，T=10 秒），观测用，不断言
#
# 用法：./scripts/load_connections.sh（PORT 可覆盖，默认 19351，避开 E2E 的 19350）
# 流程：stage=server 起服务 → stage=load 并发 N 路 sender → 汇总行（数字+环境）
# sender 见 load_sender.py：连上后 T 秒内紧循环发 64B 小包，战果写 /tmp/load_$i.stat
# 发的是 b"x" 开头字节：服务端嗅探走 HTTP 分支、只缓冲不解析——
#   压的是 accept+read+缓冲路径，不测协议；数字只许纵向对比，不许写成生产容量
# 本脚本是观测脚本：跑完数字可信即 exit 0，只有搭架子失败（服务起不来）才非零退出

set -u
N=8
T=10
PORT="${PORT:-19351}"
BIN="./build/LiveLite"

cd "$(dirname "$0")/.." || exit 1
SERVER_PID=""

log() { echo "[load] stage=$1 ... $2"; }
fail() { echo "[load] FAIL stage=$1:$2"; exit "$3"; }

cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
    wait 2>/dev/null
    rm -f /tmp/load_*.stat
}

trap cleanup EXIT

wait_port() {
    for _ in $(seq 1 50); do
        python3 -c "import socket; s=socket.socket();s.settimeout(0.5);s.connect(('127.0.0.1',$PORT));s.close()" 2>/dev/null && return 0
        sleep 0.2
    done
    return 1
}

# ---- stage=server ----
[ -x "$BIN" ] || fail server "binary $BIN 不存在或不可执行" 1
"$BIN" "$PORT" > /tmp/load_connections_server.log 2>&1 &
SERVER_PID=$!
wait_port || fail server "端口 $PORT 10s 内不可连 (见/tmp/load_connections_server.log)" 1
kill -0 "$SERVER_PID" 2>/dev/null || fail server "服务进程已退出（见 /tmp/load_connections_server.log）" 1
log server OK

SECONDS=0
# ---- stage=load ----
PIDS=""
for i in $(seq 1 $N); do
    python3 scripts/load_sender.py "$PORT" "$T" "/tmp/load_$i.stat" &
    PIDS="$PIDS $!"
done
wait $PIDS
elapsed=$SECONDS


opened=$(grep -h '^ok=1$' /tmp/load_*.stat | wc -l | tr -d ' ')
bytes=$(grep -h '^bytes=' /tmp/load_*.stat | cut -d= -f2 | awk '{s+=$1} END {print s+0}')
packets=$(grep -h '^packets=' /tmp/load_*.stat | cut -d= -f2 | awk '{s+=$1} END {print s+0}')
failed=$(($N - $opened))
bps=$(awk "BEGIN {print int($bytes/$elapsed)}")

machine=$(uname -sm); cpu=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo n/a)
echo "[load] machine=$machine cpu=$cpu N=$N T=${T}s"
echo "[load] opened=$opened/$N failed=$failed bytes_out=$bytes packets=$packets elapsed=${elapsed}s Bps=$bps"
echo "[load] note=observation only, not production capacity"