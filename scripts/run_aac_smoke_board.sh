#!/usr/bin/env bash
# 板端：AAC 发布端 10s 冒烟，dump ADTS 到 /tmp/aac_smoke.adts
set -euo pipefail

RUN="${1:-/home/lckfb/rk3576-rtsp-terminal/run}"
CFG="${RUN}/config/publisher_loopback.ini"

pkill -f publisher_supervisor || true
pkill -f encoder || true
killall gst-launch-1.0 2>/dev/null || true
sleep 2
rm -f /tmp/aac_smoke.adts

echo "[aac-smoke] encoder 10s + shm_dump audio"
"${RUN}/encoder" 720 1280 25 10 testpattern > /tmp/aac_enc.log 2>&1 &
ENC_PID=$!
sleep 2
"${RUN}/shm_dump" audio 0 /tmp/aac_smoke.adts 20 &
DUMP_PID=$!
wait "${ENC_PID}" || true
wait "${DUMP_PID}" || true

SZ=$(wc -c < /tmp/aac_smoke.adts 2>/dev/null || echo 0)
echo "[aac-smoke] /tmp/aac_smoke.adts bytes=${SZ}"
grep -iE 'audio|aac|alsasrc|fork' /tmp/aac_enc.log 2>/dev/null | tail -10 || true
