#!/usr/bin/env bash
# 板端：长时间压测（默认 30 min，可传秒数）
set -euo pipefail

RUN="${1:-/home/lckfb/rk3576-rtsp-terminal/run}"
SECONDS="${2:-1800}"
PUB_CFG="${RUN}/config/publisher_loopback.ini"
PRE_CFG="${RUN}/config/loopback.ini"

pkill -f publisher_supervisor || true
pkill -f preview_supervisor || true
pkill -f rtsp_server || true
pkill -f encoder || true
pkill -f rtsp_channel || true
pkill -f player || true
killall gst-launch-1.0 2>/dev/null || true
sleep 2

echo "[stress] duration=${SECONDS}s publisher=${PUB_CFG}"
"${RUN}/publisher_supervisor" "${PUB_CFG}" "${RUN}" 554 0 > /tmp/stress_pub.log 2>&1 &
sleep 12
"${RUN}/preview_supervisor" "${PRE_CFG}" "${RUN}" > /tmp/stress_preview.log 2>&1 &
PREVIEW_PID=$!

END=$((SECONDS + $(date +%s)))
while [ "$(date +%s)" -lt "${END}" ]; do
  sleep 30
  ELAPSED=$(( $(date +%s) - (END - SECONDS) ))
  ENC=$(pgrep -c encoder || echo 0)
  SRV=$(pgrep -c rtsp_server || echo 0)
  PLR=$(pgrep -c player || echo 0)
  CH0=$(pgrep -fc 'rtsp_channel 0' || echo 0)
  CH1=$(pgrep -fc 'rtsp_channel 1' || echo 0)
  MEM=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
  LOAD=$(cut -d' ' -f1 /proc/loadavg)
  echo "[stress] t=${ELAPSED}s encoder=${ENC} rtsp=${SRV} player=${PLR} ch0=${CH0} ch1=${CH1} load=${LOAD} mem_avail_kb=${MEM}"
done

kill -TERM "${PREVIEW_PID}" 2>/dev/null || true
pkill -f publisher_supervisor || true
sleep 3

echo "[stress] summary"
grep -E 'video push seq=|rtsp_channel ch=.*push seq=|runtime=|exit code' /tmp/stress_pub.log /tmp/stress_preview.log 2>/dev/null | tail -20 || true
ls -la /tmp/ch0.yuv /tmp/ch1.yuv /tmp/display_480x800.yuv 2>&1 || true
