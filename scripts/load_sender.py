"""Day 13 负载 sender：单路 TCP 压测端（被 load_connections.sh 并发调用）.

用法：python3 scripts/load_sender.py <PORT> <秒> <STATFILE>
行为：连上后 duration 秒内紧循环 sendall 64B 包（首字节 b"x"：服务端走 HTTP
嗅探分支，只缓冲不解析——压的是 accept+read 路径）；战果写 STATFILE
（ok/bytes/packets 三行 key=value，供父 shell 加总）。
连不上写 ok=0 且 exit 0——连不上是被测现象，判读留给汇总行。
"""
import socket
import sys
import time

port, duration, statfile = int(sys.argv[1]), float(sys.argv[2]), sys.argv[3]

def write_stat(ok, byte_count=0, pkt_count=0):
    with open(statfile, "w") as f:
        f.write(f"ok={ok}\nbytes={byte_count}\npackets={pkt_count}\n")

payload = b"x" * 64

try:
    s = socket.socket()
    s.settimeout(2)
    s.connect(("127.0.0.1", port))
except OSError:
    write_stat(0)
    sys.exit(0)

start = time.monotonic()
byte_count = pkt_count = 0
try:
    while time.monotonic() - start < duration:
        s.sendall(payload)
        byte_count += len(payload)
        pkt_count += 1
except OSError:
    pass
finally:
    s.close()
write_stat(1, byte_count=byte_count, pkt_count=pkt_count)

