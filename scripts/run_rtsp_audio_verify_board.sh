#!/usr/bin/env bash
# 板端：从 RTSP 路径拉 AAC 并计数（非 encoder local tap）
set -uo pipefail

BOARD="${BOARD:-/home/lckfb/rk3576-rtsp-terminal}"
RUN="${RUN:-${BOARD}/run}"
PUB_CFG="${RUN}/config/publisher_mic.ini"
URL="${1:-rtsp://192.168.1.100:554/live}"
SEC="${2:-25}"

exec > >(tee -a /tmp/rtsp_audio_verify_result.txt) 2>&1
echo "=== rtsp audio verify $(date) url=${URL} sec=${SEC} ==="

"${BOARD}/scripts/stop_all_board.sh"
sleep 2
pkill -9 -f "${RUN}/encoder" 2>/dev/null || true
pkill -9 -f "${RUN}/rtsp_server" 2>/dev/null || true
for _ in 1 2 3; do fuser -k 554/tcp 2>/dev/null || true; sleep 1; done

for _iface in end0 eth0; do
  if [ -d "/sys/class/net/${_iface}" ]; then
    ip addr add 192.168.1.100/24 dev "${_iface}" 2>/dev/null || true
    ip link set "${_iface}" up 2>/dev/null || true
    break
  fi
done
ip addr add 192.168.1.100/32 dev lo 2>/dev/null || true

echo "[rtsp-audio] publisher mic + testpattern"
"${RUN}/publisher_supervisor" "${PUB_CFG}" "${RUN}" 554 0 > /tmp/rtsp_audio_pub.log 2>&1 &
for _i in $(seq 1 45); do
  ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server' && break
  sleep 1
done
sleep 12

echo "[rtsp-audio] SDP tracks"
timeout 8 gst-discoverer-1.0 -v "${URL}" > /tmp/rtsp_audio_discover.log 2>&1 || true
grep -iE 'audio|AAC|MP4A|video|Could not' /tmp/rtsp_audio_discover.log 2>/dev/null | head -10 || true
HAS_AUDIO=$(grep -ciE 'audio codec|MPEG-4 AAC' /tmp/rtsp_audio_discover.log 2>/dev/null || true)
HAS_AUDIO=${HAS_AUDIO:-0}

echo "[rtsp-audio] pull AAC ${SEC}s from RTSP (audio pad only)"
rm -f /tmp/rtsp_audio.aac /tmp/rtsp_audio_play.err /tmp/rtsp_audio_gst.log
timeout "${SEC}" gst-launch-1.0 -e \
  rtspsrc location="${URL}" latency=200 protocols=tcp name=src \
  src. ! application/x-rtp,media=audio ! rtpjitterbuffer ! rtpmp4gdepay ! aacparse \
  ! filesink location=/tmp/rtsp_audio.aac sync=false \
  2>/tmp/rtsp_audio_gst.log || true

if [ ! -s /tmp/rtsp_audio.aac ]; then
  echo "[rtsp-audio] fallback: decodebin audio -> filesink"
  timeout "${SEC}" gst-launch-1.0 -e \
    rtspsrc location="${URL}" latency=200 protocols=tcp \
    ! decodebin ! audioconvert ! audioresample ! voaacenc ! filesink location=/tmp/rtsp_audio.aac sync=false \
    2>>/tmp/rtsp_audio_gst.log || true
fi

AAC_BYTES=$(wc -c < /tmp/rtsp_audio.aac 2>/dev/null || echo 0)
# ADTS 帧头 sync 0xFFF 粗计数
AAC_FRAMES=$(grep -ao $'\xff\xf1\|\xff\xf9' /tmp/rtsp_audio.aac 2>/dev/null | wc -l || echo 0)

echo "[rtsp-audio] optional speaker playback 5s"
timeout 5 gst-launch-1.0 -q \
  rtspsrc location="${URL}" latency=200 protocols=tcp \
  ! decodebin ! audioconvert ! audioresample ! autoaudiosink sync=false \
  2>/tmp/rtsp_audio_play.err || true

echo "[rtsp-audio] aac_bytes=${AAC_BYTES} adts_sync_hits=${AAC_FRAMES} sdp_audio=${HAS_AUDIO}"
tail -8 /tmp/rtsp_audio_gst.log 2>/dev/null || true
tail -5 /tmp/rtsp_audio_play.err 2>/dev/null || true

"${BOARD}/scripts/stop_all_board.sh"

if [ "${HAS_AUDIO}" -ge 1 ] 2>/dev/null && [ "${AAC_BYTES}" -ge 8000 ] 2>/dev/null; then
  echo "[rtsp-audio] PASS: RTSP AAC pull ok"
  exit 0
fi
echo "[rtsp-audio] PARTIAL/FAIL bytes=${AAC_BYTES} sdp_audio=${HAS_AUDIO}"
exit 1
