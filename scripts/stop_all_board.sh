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
sleep 1
echo "[stop_all] done"
