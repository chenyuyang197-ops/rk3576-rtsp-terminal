#!/usr/bin/env bash
set -uo pipefail
BOARD="${BOARD:-/home/lckfb/rk3576-rtsp-terminal}"
RUN="${RUN:-${BOARD}/run}"
"${BOARD}/scripts/stop_all_board.sh"
sleep 2
pkill -9 -f "${RUN}/encoder" 2>/dev/null || true
pkill -9 -f "${RUN}/rtsp_server" 2>/dev/null || true
sleep 1
ip addr add 192.168.1.100/32 dev lo 2>/dev/null || true
"${RUN}/publisher_supervisor" "${RUN}/config/publisher_loopback.ini" "${RUN}" 554 0 > /tmp/diag_pub.log 2>&1 &
sleep 15
"${RUN}/preview_supervisor" "${RUN}/config/loopback_rtsp_2ch.ini" "${RUN}" > /tmp/diag_pre.log 2>&1 &
sleep 35
P0=$(grep -o 'rtsp_channel ch=0 push seq=[0-9]*' /tmp/diag_pre.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
P1=$(grep -o 'rtsp_channel ch=1 push seq=[0-9]*' /tmp/diag_pre.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
echo "push0=${P0} push1=${P1} enc=$(pgrep -c -f "${RUN}/encoder" || echo 0)"
grep -E 'started rtsp_channel|connect failed|ch=1 push' /tmp/diag_pre.log | tail -12
"${BOARD}/scripts/stop_all_board.sh"
