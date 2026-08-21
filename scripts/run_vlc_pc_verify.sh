#!/usr/bin/env bash
# 主机侧：adb forward 后 PC/WSL 拉 RTSP（无需网线，等价外网 VLC 验证）
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "${ROOT}/scripts/adb_env.sh"

LOCAL_PORT="${1:-8554}"
BOARD_PORT="${2:-554}"
URL="rtsp://127.0.0.1:${LOCAL_PORT}/live"
SEC="${3:-15}"

echo "=== PC/VLC verify via adb forward $(date) ==="
echo "forward tcp:${LOCAL_PORT} -> board:${BOARD_PORT}"

"${ADB}" forward "tcp:${LOCAL_PORT}" "tcp:${BOARD_PORT}"

echo "[pc] ensure publisher running on board first:"
echo "  adb shell /home/lckfb/rk3576-rtsp-terminal/scripts/run_rtsp_audio_verify_board.sh  # or publisher only"

if command -v ffprobe >/dev/null 2>&1; then
  echo "[pc] ffprobe ${URL}"
  timeout 10 ffprobe -v error -show_streams "${URL}" 2>&1 | grep -E 'codec_name|codec_type|width|height|sample_rate' || true
fi

if command -v gst-discoverer-1.0 >/dev/null 2>&1; then
  echo "[pc] gst-discoverer ${URL}"
  timeout 12 gst-discoverer-1.0 -v "${URL}" 2>&1 | grep -iE 'video|audio|H265|AAC|duration' | head -12 || true
fi

if command -v ffmpeg >/dev/null 2>&1; then
  echo "[pc] ffmpeg pull ${SEC}s H265"
  rm -f /tmp/pc_vlc_verify.h265
  timeout "$((SEC + 5))" ffmpeg -y -rtsp_transport tcp -i "${URL}" -t "${SEC}" -c copy -an /tmp/pc_vlc_verify.h265 2>/tmp/pc_vlc_verify.log || true
  BYTES=$(wc -c < /tmp/pc_vlc_verify.h265 2>/dev/null || echo 0)
  echo "[pc] h265_bytes=${BYTES}"
  tail -5 /tmp/pc_vlc_verify.log 2>/dev/null || true
  if [ "${BYTES}" -ge 50000 ]; then
    echo "[pc] PASS: host pull via adb forward"
    echo "VLC: Media -> Open Network -> ${URL}"
    exit 0
  fi
fi

echo "[pc] MANUAL: open VLC -> ${URL} (adb forward active)"
echo "[pc] or install ffmpeg/ffprobe in WSL for auto check"
exit 1
