#!/usr/bin/env bash
# 板端：MIPI 验收（local NV12 旁路 或 RTSP 回环）
set -uo pipefail

BOARD="${BOARD:-/home/lckfb/rk3576-rtsp-terminal}"
RUN="${RUN:-${BOARD}/run}"
RESULT=/tmp/mipi_rollback_result.txt
LOCAL=0
grep -qE 'previewMode\s*=\s*local' "${RUN}/config/loopback.ini" 2>/dev/null && LOCAL=1

exec > >(tee -a "${RESULT}") 2>&1
echo "=== mipi verify mode=$([ "$LOCAL" = 1 ] && echo local || echo rtsp) $(date) ==="

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

rm -f /tmp/ch0.yuv /tmp/pub.log /tmp/preview.log

if [ "${LOCAL}" = 1 ]; then
  echo "[verify] preview first (local NV12 pool)"
  stdbuf -oL -eL "${RUN}/preview_supervisor" "${RUN}/config/loopback.ini" "${RUN}" \
    > /tmp/preview.log 2>&1 &
  PV=$!
  sleep 5
  echo "[verify] publisher + nv12 tap"
  "${RUN}/publisher_supervisor" "${RUN}/config/publisher.ini" "${RUN}" 554 0 > /tmp/pub.log 2>&1 &
else
  echo "[verify] publisher (RTSP)"
  "${RUN}/publisher_supervisor" "${RUN}/config/publisher.ini" "${RUN}" 554 0 > /tmp/pub.log 2>&1 &
  for i in $(seq 1 60); do
    if ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server'; then
      echo "[verify] RTSP ready ${i}s"
      break
    fi
    sleep 1
  done
  sleep 20
  echo "[verify] preview 90s"
  stdbuf -oL -eL "${RUN}/preview_supervisor" "${RUN}/config/loopback.ini" "${RUN}" \
    > /tmp/preview.log 2>&1 &
  PV=$!
fi

sleep 90
kill -TERM "${PV}" 2>/dev/null || true
sleep 3
"${BOARD}/scripts/stop_all_board.sh"

DRM=$(grep -c 'display_output: DRM MIPI' /tmp/preview.log 2>/dev/null || echo 0)
LOCAL_FRAMES=$(grep -o 'local_nv12=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
if [ "${LOCAL_FRAMES}" = 0 ]; then
  LOCAL_FRAMES=$(grep -o 'player ch=0 decoded [0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
fi
NV12_TAP=$(grep -o 'nv12 tap push seq=[0-9]*' /tmp/pub.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
PUSHED=$(grep -o 'rtsp_channel ch=0 pushed [0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)

echo "[verify] drm=${DRM} local_frames=${LOCAL_FRAMES} nv12_tap=${NV12_TAP} rtsp_push=${PUSHED}"
grep -E 'local_nv12|nv12 tap|local NV12|decoded|display_rga' /tmp/preview.log /tmp/pub.log 2>/dev/null | tail -20 || true

if [ "${LOCAL}" = 1 ] && [ "${LOCAL_FRAMES}" -ge 900 ] && [ "${NV12_TAP}" -ge 900 ] && [ "${DRM}" -ge 1 ]; then
  echo "[verify] PASS: local NV12 MIPI (~$((LOCAL_FRAMES / 90)) fps display)"
  exit 0
fi
if [ "${LOCAL}" = 1 ] && [ "${LOCAL_FRAMES}" -ge 500 ] && [ "${DRM}" -ge 1 ]; then
  echo "[verify] PASS: local NV12 MIPI (partial tap local_frames=${LOCAL_FRAMES})"
  exit 0
fi
if [ "${LOCAL_FRAMES}" -ge 30 ] && [ "${DRM}" -ge 1 ]; then
  echo "[verify] PASS: display ok (frames=${LOCAL_FRAMES})"
  exit 0
fi
echo "[verify] FAIL: check logs above"
exit 1
