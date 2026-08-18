#!/usr/bin/env bash
# 板端：简历对齐自环（720×1280 编码 / 480×800 MIPI 显示 / 554 / 2 路 / 故障隔离）
set -euo pipefail

RUN="${1:-/home/lckfb/rk3576-rtsp-terminal/run}"
CFG="${RUN}/config/loopback.ini"

pkill -f publisher_supervisor || true
pkill -f preview_supervisor || true
pkill -f rtsp_server || true
pkill -f encoder || true
pkill -f rtsp_channel || true
pkill -f player || true
killall gst-launch-1.0 2>/dev/null || true
sleep 3
rm -f /tmp/ch0.yuv /tmp/ch1.yuv /tmp/display_480x800.yuv /tmp/pub.log /tmp/preview.log

# Live555 需要 IPv4 地址才能稳定监听 0.0.0.0:554
for _iface in end0 eth0; do
  if [ -d "/sys/class/net/${_iface}" ]; then
    ip addr flush dev "${_iface}" 2>/dev/null || true
    ip addr add 192.168.1.100/24 dev "${_iface}" 2>/dev/null || true
    ip link set "${_iface}" up 2>/dev/null || true
    break
  fi
done
sleep 2

PUB_CFG="${RUN}/config/publisher_loopback.ini"
[ -f "${PUB_CFG}" ] || PUB_CFG="${RUN}/config/publisher.ini"
echo "[resume] start publisher (554) cfg=${PUB_CFG}"
"${RUN}/publisher_supervisor" "${PUB_CFG}" "${RUN}" 554 0 > /tmp/pub.log 2>&1 &
for _i in $(seq 1 30); do
  if ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554'; then
    echo "[resume] RTSP port 554 ready (${_i}s)"
    break
  fi
  sleep 1
done
sleep 8

echo "[resume] start 2-channel preview + RGA 480x800 MIPI/dump"
"${RUN}/preview_supervisor" "${CFG}" "${RUN}" > /tmp/preview.log 2>&1 &
PREVIEW_PID=$!
sleep 90
kill -TERM "${PREVIEW_PID}" 2>/dev/null || true
sleep 3

echo "[resume] outputs"
ls -la /tmp/ch0.yuv /tmp/ch1.yuv /tmp/display_480x800.yuv 2>&1 || true
grep -E 'RTSP URL|rotate active|runtime=|exit code|video push|rtsp_channel ch=|display_output|DRM MIPI|pushed [0-9]+ frames' /tmp/pub.log /tmp/preview.log 2>/dev/null | tail -40 || true
CH0_FRAMES=$(grep -o 'dump /tmp/ch0.yuv frame=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
CH1_FRAMES=$(grep -o 'dump /tmp/ch1.yuv frame=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
echo "[resume] decoded frames ch0=${CH0_FRAMES} ch1=${CH1_FRAMES}"

CH0=$(wc -c < /tmp/ch0.yuv 2>/dev/null || echo 0)
CH1=$(wc -c < /tmp/ch1.yuv 2>/dev/null || echo 0)
DISP=0
if [ -f /tmp/display_480x800.yuv ]; then
  DISP=$(wc -c < /tmp/display_480x800.yuv)
fi
EXP_CH=$((720 * 1280 * 3 / 2))
EXP_DISP=$((480 * 800 * 3 / 2))
DRM_OK=0
grep -q 'display_output: DRM MIPI' /tmp/preview.log 2>/dev/null && DRM_OK=1
echo "[resume] ch0=${CH0} ch1=${CH1} display=${DISP} drm_mipi=${DRM_OK} expect_ch=${EXP_CH} expect_disp=${EXP_DISP}"

echo "[resume] fault isolation: kill rtsp_channel 0"
pkill -f "rtsp_channel 0" || true
sleep 5
echo "[resume] survivors:"
ps aux | grep -E 'player|rtsp_channel|encoder|rtsp_server' | grep -v grep || true
