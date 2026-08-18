#!/usr/bin/env bash
set -euo pipefail
RUN=/home/lckfb/rk3576-rtsp-terminal/run
pkill -f publisher_supervisor || true
pkill -f rtsp_server || true
pkill -f encoder || true
sleep 2
"$RUN/publisher_supervisor" "$RUN/config/publisher_loopback.ini" "$RUN" 554 0 >/tmp/t4.log 2>&1 &
sleep 18
for url in rtsp://127.0.0.1:554/live rtsp://192.168.1.100/live; do
  echo "=== pull $url ==="
  rm -f /tmp/rtsp_depay.h265
  timeout 8 gst-launch-1.0 -e rtspsrc location="$url" latency=300 protocols=tcp ! rtph265depay ! filesink location=/tmp/rtsp_depay.h265 2>&1 | tail -6 || true
  wc -c /tmp/rtsp_depay.h265 2>/dev/null || true
  xxd /tmp/rtsp_depay.h265 2>/dev/null | head -2 || true
  gst-launch-1.0 -q filesrc location=/tmp/rtsp_depay.h265 ! h265parse ! fakesink 2>&1 | tail -2 || true
  echo "rtsp_server alive:" $(pgrep -c rtsp_server || echo 0)
  sleep 2
done
