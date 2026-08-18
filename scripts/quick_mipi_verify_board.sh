#!/usr/bin/env bash
set -euo pipefail
RUN=/home/lckfb/rk3576-rtsp-terminal/run
for _iface in end0 eth0; do
  [ -d "/sys/class/net/${_iface}" ] || continue
  ip addr add 192.168.1.100/24 dev "${_iface}" 2>/dev/null || true
  ip link set "${_iface}" up 2>/dev/null || true
  break
done
ip addr add 192.168.1.100/32 dev lo 2>/dev/null || true
systemctl restart rkaiq_3A.service 2>/dev/null || true
sleep 3
rm -f /tmp/pub.log /tmp/preview.log
"${RUN}/publisher_supervisor" "${RUN}/config/publisher.ini" "${RUN}" 554 0 >/tmp/pub.log 2>&1 &
for i in $(seq 1 45); do
  ss -ltnp4 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server' && echo "rtsp_ready_${i}s" && break
  sleep 1
done
sleep 10
grep 'video gst' /tmp/pub.log | tail -1 || true
grep 'push seq=' /tmp/pub.log | tail -3 || true
"${RUN}/preview_supervisor" "${RUN}/config/loopback.ini" "${RUN}" >/tmp/preview.log 2>&1 &
sleep 60
grep -E 'stretch|DRM|mode=|display_ch|push seq=' /tmp/preview.log /tmp/pub.log 2>/dev/null | tail -20
grep -c 'imresize error' /tmp/preview.log 2>/dev/null || echo 0
