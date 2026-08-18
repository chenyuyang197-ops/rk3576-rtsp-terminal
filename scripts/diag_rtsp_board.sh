#!/usr/bin/env bash
set -euo pipefail
RUN=/home/lckfb/rk3576-rtsp-terminal/run
pkill -f publisher_supervisor || true
pkill -f rtsp_server || true
pkill -f encoder || true
sleep 2
"$RUN/publisher_supervisor" "$RUN/config/publisher_loopback.ini" "$RUN" 554 0 >/tmp/t3.log 2>&1 &
sleep 18
echo "=== processes ==="
ps aux | grep -E 'rtsp_server|encoder' | grep -v grep || true
echo "=== port ==="
ss -ltnp 2>/dev/null | grep 554 || netstat -ltnp 2>/dev/null | grep 554 || true
echo "=== log head ==="
head -20 /tmp/t3.log
echo "=== signal ==="
grep -i signal /tmp/t3.log || echo none
rm -f /tmp/rtsp_test3.h265 /tmp/rtsp_test3b.h265
timeout 10 gst-launch-1.0 -e rtspsrc location=rtsp://127.0.0.1:554/live latency=300 protocols=tcp ! rtph265depay ! h265parse ! filesink location=/tmp/rtsp_test3.h265 2>&1 | tail -10 || true
timeout 10 gst-launch-1.0 -e rtspsrc location=rtsp://127.0.0.1:554/live latency=300 protocols=tcp ! rtph265depay ! "video/x-h265,stream-format=byte-stream,alignment=au" ! filesink location=/tmp/rtsp_test3b.h265 2>&1 | tail -10 || true
ls -la /tmp/rtsp_test3*.h265 2>&1 || true
xxd /tmp/rtsp_test3b.h265 2>/dev/null | head -2 || true
gst-launch-1.0 -q filesrc location=/tmp/rtsp_test3b.h265 ! h265parse ! fakesink 2>&1 | tail -3 || true
