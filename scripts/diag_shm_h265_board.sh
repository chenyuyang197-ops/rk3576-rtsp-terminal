#!/usr/bin/env bash
set -euo pipefail
RUN=/home/lckfb/rk3576-rtsp-terminal/run
pkill -f encoder || true
sleep 1
"$RUN/encoder" 720 1280 25 10 testpattern >/dev/null 2>&1 &
ENC=$!
sleep 2
"$RUN/shm_dump" video 0 /tmp/shm_video.h265 5 || true
kill $ENC 2>/dev/null || true
wait $ENC 2>/dev/null || true
ls -la /tmp/shm_video.h265
xxd /tmp/shm_video.h265 | head -2
gst-launch-1.0 -q filesrc location=/tmp/shm_video.h265 ! h265parse ! fakesink 2>&1 | tail -3 || true
