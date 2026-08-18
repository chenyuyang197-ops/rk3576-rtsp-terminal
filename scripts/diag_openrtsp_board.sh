#!/usr/bin/env bash
set -euo pipefail
ROOT=/home/lckfb/rk3576-rtsp-terminal
RUN=$ROOT/run
pkill -f publisher_supervisor || true
pkill -f rtsp_server || true
pkill -f encoder || true
sleep 2
"$RUN/publisher_supervisor" "$RUN/config/publisher_loopback.ini" "$RUN" 554 0 >/tmp/t5.log 2>&1 &
sleep 18
cd "$ROOT/third_party/live555/testProgs"
make openRTSP 2>&1 | tail -5
timeout 6 ./openRTSP rtsp://127.0.0.1:554/live -d 5 2>&1 | tail -20 || true
