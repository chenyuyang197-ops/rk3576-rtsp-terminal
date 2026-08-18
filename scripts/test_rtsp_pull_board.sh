#!/usr/bin/env bash
set -euo pipefail
RUN=/home/lckfb/rk3576-rtsp-terminal/run
pkill -f publisher_supervisor || true
pkill -f rtsp_server || true
pkill -f encoder || true
sleep 2
"$RUN/publisher_supervisor" "$RUN/config/publisher_loopback.ini" "$RUN" 554 0 >/tmp/t2.log 2>&1 &
sleep 16
echo "rtsp_server:" $(pgrep -c rtsp_server || echo 0)
rm -f /tmp/rtsp_test.h265
gst-launch-1.0 -q rtspsrc location=rtsp://127.0.0.1:554/live latency=300 protocols=tcp ! rtph265depay ! h265parse ! filesink location=/tmp/rtsp_test.h265 num-buffers=30 2>&1 || true
ls -la /tmp/rtsp_test.h265 2>&1 || true
wc -c /tmp/rtsp_test.h265 2>&1 || true
tail -5 /tmp/t2.log
