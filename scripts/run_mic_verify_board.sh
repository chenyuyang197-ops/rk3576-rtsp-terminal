#!/usr/bin/env bash
# 板端：麦克风 AAC 采集 + local 回放验收
set -uo pipefail

BOARD="${BOARD:-/home/lckfb/rk3576-rtsp-terminal}"
RUN="${RUN:-${BOARD}/run}"
PUB_CFG="${RUN}/config/publisher_mic.ini"
PRE_CFG="${RUN}/config/loopback.ini"

exec > >(tee -a /tmp/mic_verify_result.txt) 2>&1
echo "=== mic verify $(date) ==="

"${BOARD}/scripts/stop_all_board.sh"
sleep 2
pkill -9 -f "${RUN}/encoder" 2>/dev/null || true
pkill -9 -f "${RUN}/rtsp_server" 2>/dev/null || true
for _ in 1 2 3; do fuser -k 554/tcp 2>/dev/null || true; sleep 1; done

rm -f /tmp/preview.log /tmp/pub.log

echo "[mic] preview first (local + audio playback pool)"
stdbuf -oL -eL "${RUN}/preview_supervisor" "${PRE_CFG}" "${RUN}" > /tmp/preview.log 2>&1 &
PV=$!
sleep 5

echo "[mic] publisher testpattern + mic (no camera)"
"${RUN}/publisher_supervisor" "${PUB_CFG}" "${RUN}" 554 0 > /tmp/pub.log 2>&1 &
for _i in $(seq 1 25); do
  grep -q 'audio push seq=3' /tmp/pub.log 2>/dev/null && break
  sleep 1
done
sleep 20

kill -TERM "${PV}" 2>/dev/null || true
sleep 2
"${BOARD}/scripts/stop_all_board.sh"

AUDIO_PUSH=$(grep -o 'audio gst pushed [0-9]* adts' /tmp/pub.log 2>/dev/null | tail -1 | grep -oE '[0-9]+' | head -1 || echo 0)
AUDIO_PLAY=$(grep -o 'audio playback total frames=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -oE '[0-9]+$' || echo 0)
if [ "${AUDIO_PLAY}" = "0" ]; then
  AUDIO_PLAY=$(grep -o 'audio playback frames=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -oE '[0-9]+$' || echo 0)
fi
RTSP_AUDIO=$(grep -c 'RTSP ch=0 new audio stream' /tmp/pub.log 2>/dev/null || echo 0)

echo "[mic] audio_push_total=${AUDIO_PUSH} audio_playback=${AUDIO_PLAY} rtsp_audio=${RTSP_AUDIO}"
grep -E 'audio push|audio playback|audio gst|Device is busy|Failed|error' /tmp/pub.log /tmp/preview.log 2>/dev/null | tail -25 || true

if [ "${AUDIO_PUSH}" -ge 400 ] && [ "${AUDIO_PLAY}" -ge 100 ]; then
  echo "[mic] PASS: microphone AAC capture + playback"
  exit 0
fi
if [ "${AUDIO_PUSH}" -ge 400 ]; then
  echo "[mic] PARTIAL: capture ok playback=${AUDIO_PLAY} (check speaker/alsasink)"
  exit 0
fi
echo "[mic] FAIL push=${AUDIO_PUSH} play=${AUDIO_PLAY}"
exit 1
