#!/usr/bin/env bash
# 板端：停止所有 rk3576-rtsp-terminal 媒体进程
# 注意：pkill 模式必须匹配具体二进制路径，勿用项目目录名（会误杀 adb shell）
set -euo pipefail

RUN="${RUN:-/home/lckfb/rk3576-rtsp-terminal/run}"

stop_one() {
  local name="$1"
  pkill -f "${RUN}/${name}" 2>/dev/null || true
}

kill_one() {
  local name="$1"
  pkill -9 -f "${RUN}/${name}" 2>/dev/null || true
}

stop_one publisher_supervisor
stop_one preview_supervisor
stop_one rtsp_server
stop_one encoder
stop_one rtsp_channel
stop_one player
killall gst-launch-1.0 2>/dev/null || true
sleep 2

kill_one publisher_supervisor
kill_one preview_supervisor
kill_one rtsp_server
kill_one encoder
kill_one rtsp_channel
kill_one player
killall -9 gst-launch-1.0 2>/dev/null || true
for _ in 1 2 3; do
  fuser -k 554/tcp 2>/dev/null || true
  sleep 1
done
sleep 1

# 清理 publisher/preview 遗留 shm（slot 尺寸变更或多路通道后 attach 会失败）
for key in 0x5200 0x5201 0x5210 0x5211 0x5300 0x5301 0x5310 0x5311; do
  id=$(ipcs -m 2>/dev/null | awk -v k="$key" '$1 == k {print $2; exit}')
  if [ -n "${id:-}" ]; then
    ipcrm -m "$id" 2>/dev/null || true
  fi
done

echo "[stop_all] done"
