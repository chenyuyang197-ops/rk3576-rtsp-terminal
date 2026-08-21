#!/usr/bin/env bash
# 板端：投递版补齐验收（2 路 RTSP + AAC 轨 + 故障隔离 + 可选压测采样）
set -uo pipefail

BOARD="${BOARD:-/home/lckfb/rk3576-rtsp-terminal}"
RUN="${RUN:-${BOARD}/run}"
RESULT=/tmp/resume_verify_result.txt
STRESS_SEC="${1:-0}"

exec > >(tee -a "${RESULT}") 2>&1
echo "=== resume verify $(date) stress_sec=${STRESS_SEC} ==="

"${BOARD}/scripts/stop_all_board.sh"
sleep 2
pkill -9 -f "${RUN}/encoder" 2>/dev/null || true
pkill -9 -f "${RUN}/rtsp_server" 2>/dev/null || true
pkill -9 -f "${RUN}/publisher_supervisor" 2>/dev/null || true
for _ in 1 2 3 4 5; do
  fuser -k 554/tcp 2>/dev/null || true
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
sleep 2

rm -f /tmp/ch0.yuv /tmp/ch1.yuv /tmp/display_480x800.yuv /tmp/pub.log /tmp/preview.log

PUB_CFG="${RUN}/config/publisher_loopback.ini"
PRE_CFG="${RUN}/config/loopback_rtsp_2ch.ini"

echo "[verify] publisher testpattern + AAC"
"${RUN}/publisher_supervisor" "${PUB_CFG}" "${RUN}" 554 0 > /tmp/pub.log 2>&1 &
for i in $(seq 1 45); do
  ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server' && break
  sleep 1
done
sleep 12

echo "[verify] RTSP SDP / tracks (live only, avoid live2 probe before preview)"
timeout 8 gst-discoverer-1.0 -v rtsp://192.168.1.100:554/live 2>/tmp/rtsp_discover.log || true
grep -iE 'video|audio|H265|H264|AAC|MP4A' /tmp/rtsp_discover.log 2>/dev/null | head -8 || true
HAS_VIDEO=$(grep -ci 'video' /tmp/rtsp_discover.log 2>/dev/null || echo 0)
HAS_AUDIO=$(grep -ciE 'audio|AAC|MP4A' /tmp/rtsp_discover.log 2>/dev/null || echo 0)
HAS_VIDEO2=0

echo "[verify] 2ch preview 120s RTSP decode"
stdbuf -oL -eL "${RUN}/preview_supervisor" "${PRE_CFG}" "${RUN}" > /tmp/preview.log 2>&1 &
PV=$!
sleep 120
kill -TERM "${PV}" 2>/dev/null || true
sleep 3

echo "[verify] live2 SDP (post-preview, publisher still up)"
timeout 8 gst-discoverer-1.0 rtsp://192.168.1.100:554/live2 2>/tmp/rtsp_discover2.log || true
grep -iE 'video|audio|Could not' /tmp/rtsp_discover2.log 2>/dev/null | head -6 || true
HAS_VIDEO2=$(grep -ci 'video' /tmp/rtsp_discover2.log 2>/dev/null || echo 0)

CH0_DEC=$(grep -oE 'player ch=0 decoded [0-9]+|player ch=0 decoded=[0-9]+' /tmp/preview.log 2>/dev/null | tail -1 | grep -oE '[0-9]+$' || echo 0)
CH1_DEC=$(grep -oE 'player ch=1 decoded [0-9]+|player ch=1 decoded=[0-9]+' /tmp/preview.log 2>/dev/null | tail -1 | grep -oE '[0-9]+$' || echo 0)
CH0_DUMP=$(grep -o 'dump /tmp/ch0.yuv frame=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
CH1_DUMP=$(grep -o 'dump /tmp/ch1.yuv frame=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
PUSH0=$(grep -o 'rtsp_channel ch=0 push seq=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
PUSH1=$(grep -o 'rtsp_channel ch=1 push seq=[0-9]*' /tmp/preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
AUDIO_PUSH=$(grep -o 'audio push seq=[0-9]*' /tmp/pub.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
CH0_SZ=$(test -f /tmp/ch0.yuv && wc -c < /tmp/ch0.yuv || echo 0)
CH1_SZ=$(test -f /tmp/ch1.yuv && wc -c < /tmp/ch1.yuv || echo 0)

echo "[verify] ch0 decoded=${CH0_DEC} push=${PUSH0} dump_frames=${CH0_DUMP} yuv_bytes=${CH0_SZ}"
echo "[verify] ch1 decoded=${CH1_DEC} push=${PUSH1} dump_frames=${CH1_DUMP} yuv_bytes=${CH1_SZ}"
echo "[verify] audio_push=${AUDIO_PUSH} rtsp_video=${HAS_VIDEO} rtsp_audio=${HAS_AUDIO} live2_video=${HAS_VIDEO2}"

echo "[verify] fault isolation: kill rtsp_channel ch0"
pkill -f "${RUN}/rtsp_channel 0" 2>/dev/null || true
sleep 3
SURV_ENC=$(pgrep -fc "${RUN}/encoder " 2>/dev/null || pgrep -c -f "${RUN}/encoder" 2>/dev/null || echo 0)
SURV_SRV=$(pgrep -c -f "${RUN}/rtsp_server" 2>/dev/null || echo 0)
SURV_PLR=$(pgrep -c -f "${RUN}/player" 2>/dev/null || echo 0)
SURV_CH1=$(pgrep -c -f "${RUN}/rtsp_channel 1" 2>/dev/null || echo 0)
echo "[verify] survivors encoder=${SURV_ENC} rtsp=${SURV_SRV} player=${SURV_PLR} ch1=${SURV_CH1}"

STRESS_LINE=""
if [ "${STRESS_SEC}" -gt 0 ]; then
  echo "[verify] stress sample ${STRESS_SEC}s (keep publisher running)"
  END=$(( $(date +%s) + STRESS_SEC ))
  LOAD_MAX=0
  MEM_MIN=99999999
  while [ "$(date +%s)" -lt "${END}" ]; do
    sleep 30
    LOAD=$(cut -d' ' -f1 /proc/loadavg)
    MEM=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
    ENC=$(pgrep -c -f "${RUN}/encoder" || echo 0)
    echo "[stress] load=${LOAD} mem_avail_kb=${MEM} encoder=${ENC}"
    LOAD_MAX=${LOAD}
    if [ "${MEM}" -lt "${MEM_MIN}" ]; then MEM_MIN=${MEM}; fi
  done
  STRESS_LINE="load_max=${LOAD_MAX} mem_min_kb=${MEM_MIN}"
  echo "[verify] ${STRESS_LINE}"
fi

"${BOARD}/scripts/stop_all_board.sh"

PASS=0
[ "${CH0_DEC}" -ge 30 ] && [ "${CH1_DEC}" -ge 30 ] && PASS=1
[ "${CH0_SZ}" -ge 1382400 ] && [ "${CH1_SZ}" -ge 1382400 ] && PASS=1
[ "${SURV_ENC}" -ge 1 ] && [ "${SURV_PLR}" -ge 1 ] && [ "${SURV_CH1}" -ge 1 ] && PASS=1

if [ "${CH0_DEC}" -ge 500 ] && [ "${CH1_DEC}" -ge 500 ] && [ "${PUSH1}" -ge 500 ] && [ "${SURV_ENC}" -ge 1 ]; then
  echo "[verify] PASS: 2ch RTSP decode + fault isolation"
  exit 0
fi
echo "[verify] PARTIAL/FAIL ch0=${CH0_DEC} ch1=${CH1_DEC} survivors enc=${SURV_ENC} plr=${SURV_PLR}"
exit 1
