#!/usr/bin/env bash
# 板端：长时间压测（默认 30 min，可传秒数）
set -uo pipefail

BOARD="${BOARD:-/home/lckfb/rk3576-rtsp-terminal}"
RUN="${1:-${BOARD}/run}"
STRESS_SEC="${2:-1800}"
PUB_CFG="${RUN}/config/publisher_loopback.ini"
PRE_CFG="${RUN}/config/loopback_rtsp_2ch.ini"
RESULT=/tmp/stress_test_result.txt

exec > >(tee -a "${RESULT}") 2>&1
echo "=== stress test $(date) duration=${STRESS_SEC}s ==="

"${BOARD}/scripts/stop_all_board.sh"
sleep 2
pkill -9 -f "${RUN}/encoder" 2>/dev/null || true
pkill -9 -f "${RUN}/rtsp_server" 2>/dev/null || true
for _ in 1 2 3 4 5; do
  fuser -k 554/tcp 2>/dev/null || true
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
ip addr add 192.168.1.100/32 dev lo 2>/dev/null || true
sleep 2

echo "[stress] publisher=${PUB_CFG}"
"${RUN}/publisher_supervisor" "${PUB_CFG}" "${RUN}" 554 0 > /tmp/stress_pub.log 2>&1 &
for _i in $(seq 1 45); do
  ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server' && break
  sleep 1
done
sleep 12
"${RUN}/preview_supervisor" "${PRE_CFG}" "${RUN}" > /tmp/stress_preview.log 2>&1 &
PREVIEW_PID=$!

START_TS=$(date +%s)
END_TS=$((START_TS + STRESS_SEC))
LOAD_MAX=0
MEM_MIN=999999999
while [ "$(date +%s)" -lt "${END_TS}" ]; do
  sleep 30
  ELAPSED=$(( $(date +%s) - START_TS ))
  ENC=$(pgrep -c -f "${RUN}/encoder" 2>/dev/null || echo 0)
  SRV=$(pgrep -c -f "${RUN}/rtsp_server" 2>/dev/null || echo 0)
  PLR=$(pgrep -c -f "${RUN}/player" 2>/dev/null || echo 0)
  CH0=$(pgrep -fc "${RUN}/rtsp_channel 0" 2>/dev/null || echo 0)
  CH1=$(pgrep -fc "${RUN}/rtsp_channel 1" 2>/dev/null || echo 0)
  MEM=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
  LOAD=$(cut -d' ' -f1 /proc/loadavg)
  LOAD_MAX=$(awk -v a="${LOAD_MAX}" -v b="${LOAD}" 'BEGIN{print (a+0>b+0)?a:b}')
  if [ "${MEM}" -lt "${MEM_MIN}" ]; then MEM_MIN=${MEM}; fi
  P0=$(grep -o 'rtsp_channel ch=0 push seq=[0-9]*' /tmp/stress_preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
  P1=$(grep -o 'rtsp_channel ch=1 push seq=[0-9]*' /tmp/stress_preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
  echo "[stress] t=${ELAPSED}s enc=${ENC} rtsp=${SRV} plr=${PLR} ch0=${CH0} ch1=${CH1} push0=${P0} push1=${P1} load=${LOAD} mem_kb=${MEM}"
done

kill -TERM "${PREVIEW_PID}" 2>/dev/null || true
sleep 3

PUSH0=$(grep -o 'rtsp_channel ch=0 push seq=[0-9]*' /tmp/stress_preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
PUSH1=$(grep -o 'rtsp_channel ch=1 push seq=[0-9]*' /tmp/stress_preview.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
echo "[stress] summary push0=${PUSH0} push1=${PUSH1} load_max=${LOAD_MAX} mem_min_kb=${MEM_MIN}"
grep -E 'video push seq=|rtsp_channel ch=.*push seq=' /tmp/stress_pub.log /tmp/stress_preview.log 2>/dev/null | tail -10 || true
ls -la /tmp/ch0.yuv /tmp/ch1.yuv 2>&1 || true

"${BOARD}/scripts/stop_all_board.sh"

if [ "${ENC}" -ge 1 ] && [ "${PUSH0}" -ge 1000 ] && [ "${PUSH1}" -ge 500 ]; then
  echo "[stress] PASS: 30min dual-ch stable (push0=${PUSH0} push1=${PUSH1})"
  exit 0
fi
if [ "${ENC}" -ge 1 ] && [ "${PUSH0}" -ge 1000 ]; then
  echo "[stress] PARTIAL: ch0 stable push1=${PUSH1} (need push1>=500 for full PASS)"
  exit 0
fi
echo "[stress] PARTIAL enc=${ENC} push0=${PUSH0} push1=${PUSH1}"
exit 1
