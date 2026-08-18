#!/usr/bin/env bash
# 板端：IMX415 实采 + RTSP + MIPI 480×800 轮巡
set -euo pipefail

RUN="${1:-/home/lckfb/rk3576-rtsp-terminal/run}"
SCRIPTS="$(dirname "$0")"

if [ -x "${SCRIPTS}/stop_all_board.sh" ]; then
  "${SCRIPTS}/stop_all_board.sh"
else
  pkill -f publisher_supervisor || true
  pkill -f preview_supervisor || true
  pkill -f rtsp_server || true
  pkill -f encoder || true
  pkill -f rtsp_channel || true
  pkill -f player || true
  killall gst-launch-1.0 2>/dev/null || true
  sleep 3
fi
for _i in $(seq 1 10); do
  if ! ss -ltnp 2>/dev/null | grep -q ':554'; then
    break
  fi
  pkill -9 -f "${RUN}/rtsp_server" 2>/dev/null || true
  sleep 1
done

for _iface in end0 eth0; do
  if [ -d "/sys/class/net/${_iface}" ]; then
    ip addr flush dev "${_iface}" 2>/dev/null || true
    ip addr add 192.168.1.100/24 dev "${_iface}" 2>/dev/null || true
    ip link set "${_iface}" up 2>/dev/null || true
    break
  fi
done
sleep 2

systemctl restart rkaiq_3A.service 2>/dev/null || true
sleep 3

rm -f /tmp/ch0.yuv /tmp/ch1.yuv /tmp/display_480x800.yuv /tmp/pub.log /tmp/preview.log /tmp/cam_smoke.nv12

echo "[mipi-hw] publisher: camera -> H.265 RTSP :554"
"${RUN}/publisher_supervisor" "${RUN}/config/publisher.ini" "${RUN}" 554 0 > /tmp/pub.log 2>&1 &
for _i in $(seq 1 45); do
  if ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server'; then
    echo "[mipi-hw] RTSP IPv4 :554 ready (${_i}s)"
    break
  fi
  sleep 1
done
if ! ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server'; then
  echo "[mipi-hw] ERROR: RTSP not listening on 0.0.0.0:554" >&2
  ss -ltnp 2>/dev/null | grep 554 || true
  grep -E 'RTSP|bind|failed' /tmp/pub.log 2>/dev/null | tail -10 || true
  exit 1
fi
sleep 8

echo "[mipi-hw] preview: 2ch + RGA 480x800 MIPI"
"${RUN}/preview_supervisor" "${RUN}/config/loopback.ini" "${RUN}" > /tmp/preview.log 2>&1 &
PREVIEW_PID=$!
sleep 90
kill -TERM "${PREVIEW_PID}" 2>/dev/null || true
sleep 3

echo "[mipi-hw] outputs"
ls -la /tmp/ch0.yuv /tmp/ch1.yuv /tmp/display_480x800.yuv /tmp/cam_smoke.nv12 2>&1 || true
grep -iE 'camera|v4l2|video push|rtsp_channel|display_output|DRM MIPI|rotate active|dump /tmp|pushed|encoder|Failed|ERROR' /tmp/pub.log /tmp/preview.log /tmp/cam_smoke.log 2>/dev/null | tail -50 || true

CH0_FRAMES=$(grep -o 'dump /tmp/ch0.yuv frame=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
CH1_FRAMES=$(grep -o 'dump /tmp/ch1.yuv frame=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
CH0=0
CH1=0
DISP=0
[ -f /tmp/ch0.yuv ] && CH0=$(wc -c < /tmp/ch0.yuv)
[ -f /tmp/ch1.yuv ] && CH1=$(wc -c < /tmp/ch1.yuv)
[ -f /tmp/display_480x800.yuv ] && DISP=$(wc -c < /tmp/display_480x800.yuv)
DRM_OK=0
grep -q 'display_output: DRM MIPI' /tmp/preview.log 2>/dev/null && DRM_OK=1
PUSH=$(grep -o 'video push seq=[0-9]*' /tmp/pub.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
CAM_BYTES=0
grep -q 'MIPI camera' /tmp/pub.log 2>/dev/null && CAM_BYTES=1
echo "[mipi-hw] camera_ok=${CAM_BYTES} push_max=${PUSH} decoded ch0=${CH0_FRAMES} ch1=${CH1_FRAMES}"
echo "[mipi-hw] ch0=${CH0} ch1=${CH1} display=${DISP} drm_mipi=${DRM_OK} expect_ch=1382400 expect_disp=576000"
