#!/usr/bin/env bash
# 板端：回退版本 MIPI 验收（清理 554 → publisher → preview 90s → 统计）
set -uo pipefail

BOARD="${BOARD:-/home/lckfb/rk3576-rtsp-terminal}"
RUN="${RUN:-${BOARD}/run}"
RESULT=/tmp/mipi_rollback_result.txt

exec > >(tee -a "${RESULT}") 2>&1
echo "=== mipi rollback verify $(date) ==="

"${BOARD}/scripts/stop_all_board.sh"
sleep 3
for _ in 1 2 3 4 5 6; do
  fuser -k 554/tcp 2>/dev/null || true
  pkill -9 -f "${RUN}/rtsp_server" 2>/dev/null || true
  sleep 1
  ss -ltnp 2>/dev/null | grep -q ':554' || break
done

for _iface in end0 eth0; do
  if [ -d "/sys/class/net/${_iface}" ]; then
    ip addr flush dev "${_iface}" 2>/dev/null || true
    ip addr add 192.168.1.100/24 dev "${_iface}" 2>/dev/null || true
    ip link set "${_iface}" up 2>/dev/null || true
    break
  fi
done
ip addr add 192.168.1.100/32 dev lo 2>/dev/null || true
systemctl restart rkaiq_3A.service 2>/dev/null || true
sleep 3

rm -f /tmp/ch0.yuv /tmp/ch1.yuv /tmp/pub.log /tmp/preview.log

echo "[verify] publisher"
"${RUN}/publisher_supervisor" "${RUN}/config/publisher.ini" "${RUN}" 554 0 > /tmp/pub.log 2>&1 &
for i in $(seq 1 60); do
  if ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server'; then
    echo "[verify] RTSP ready ${i}s"
    break
  fi
  sleep 1
done
if ! ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server'; then
  echo "[verify] FAIL: RTSP not on 554"
  grep -E 'RTSP|bind|failed' /tmp/pub.log | tail -10 || true
  exit 1
fi
sleep 10

echo "[verify] preview 90s"
stdbuf -oL -eL "${RUN}/preview_supervisor" "${RUN}/config/loopback.ini" "${RUN}" \
  > /tmp/preview.log 2>&1 &
PV=$!
sleep 90
kill -TERM "${PV}" 2>/dev/null || true
sleep 3
"${BOARD}/scripts/stop_all_board.sh"

CH0=0
CH1=0
[ -f /tmp/ch0.yuv ] && CH0=$(wc -c < /tmp/ch0.yuv)
[ -f /tmp/ch1.yuv ] && CH1=$(wc -c < /tmp/ch1.yuv)
echo "[verify] ch0_bytes=${CH0} ch1_bytes=${CH1} expect=1382400"

if [ -f /tmp/ch0.yuv ] && [ "${CH0}" -ge 720000 ]; then
  python3 -c "d=open('/tmp/ch0.yuv','rb').read(720*1280); print('[verify] y_mean', round(sum(d)/len(d),1), 'y0', d[0])"
else
  echo "[verify] WARN: no ch0.yuv dump"
fi

DRM=$(grep -c 'display_output: DRM MIPI' /tmp/preview.log 2>/dev/null || echo 0)
echo "[verify] drm_mipi_lines=${DRM}"
grep -E 'decoded|display_rga|Failed|y0=|push seq=' /tmp/preview.log 2>/dev/null | tail -25 || true
grep 'video push' /tmp/pub.log 2>/dev/null | tail -3 || true

if [ "${CH0}" -ge 1382400 ]; then
  YMEAN=$(python3 -c "d=open('/tmp/ch0.yuv','rb').read(720*1280); print(round(sum(d)/len(d),1))")
  if python3 -c "y=${YMEAN}; exit(0 if y>50 else 1)"; then
    echo "[verify] PASS: decode ok, no green-screen (y_mean=${YMEAN})"
    exit 0
  fi
fi
echo "[verify] FAIL: check logs above"
exit 1
