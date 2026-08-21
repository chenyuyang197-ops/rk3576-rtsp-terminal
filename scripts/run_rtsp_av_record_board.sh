#!/usr/bin/env bash
# 板端：IMX415 实采 + 板载麦 → RTSP → 录制 TS（v2 参数基线）
set -uo pipefail

BOARD="${BOARD:-/home/lckfb/rk3576-rtsp-terminal}"
RUN="${RUN:-${BOARD}/run}"
MODE="${MODE:-camera}"
PUB_CFG="${PUB_CFG:-${RUN}/config/publisher.ini}"
PRE_CFG="${PRE_CFG:-${RUN}/config/loopback.ini}"
URL="${1:-rtsp://192.168.1.100:554/live}"
SEC="${2:-30}"
OUT_DIR="${3:-/userdata/rtsp_record}"
WARMUP="${4:-45}"
MIN_ISP_SEC="${MIN_ISP_SEC:-35}"
STAMP=$(date +%Y%m%d_%H%M%S)
OUT_MKV="${OUT_DIR}/rtsp_camera_av_${SEC}s_${STAMP}.ts"
OUT_LOG="${OUT_DIR}/record_${STAMP}.log"

if [ "${MODE}" = "testpattern" ]; then
  PUB_CFG="${RUN}/config/publisher_mic.ini"
  PRE_CFG=""
fi

mkdir -p "${OUT_DIR}"
exec > >(tee -a "${OUT_LOG}") 2>&1
echo "=== rtsp av record $(date) mode=${MODE} url=${URL} sec=${SEC} out=${OUT_MKV} ==="

"${BOARD}/scripts/stop_all_board.sh"
sleep 3
pkill -9 -f "${RUN}/encoder" 2>/dev/null || true
pkill -9 -f "${RUN}/rtsp_server" 2>/dev/null || true
pkill -9 -f "${RUN}/preview_supervisor" 2>/dev/null || true
pkill -9 -f 'gst-launch.*v4l2src' 2>/dev/null || true
fuser -k /dev/video-camera0 2>/dev/null || true
for _ in 1 2 3 4 5; do fuser -k 554/tcp 2>/dev/null || true; sleep 1; done

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
sleep 12
amixer -c 0 sset 'Capture' 80% 2>/dev/null || true
amixer -c 0 sset 'ADC PGA Gain' 3 2>/dev/null || true

export ENC_AUDIO_GAIN="${ENC_AUDIO_GAIN:-4.0}"
export ENC_AUDIO_BITRATE="${ENC_AUDIO_BITRATE:-128000}"
export ENC_VIDEO_BPS="${ENC_VIDEO_BPS:-2500000}"
echo "[record] enc v2: video_bps=${ENC_VIDEO_BPS} aac=${ENC_AUDIO_BITRATE} mic_gain=${ENC_AUDIO_GAIN}"

PREVIEW_PID=""
if [ -n "${PRE_CFG}" ] && [ -f "${PRE_CFG}" ]; then
  echo "[record] preview first (local NV12 pool + audio playback pool)"
  stdbuf -oL -eL "${RUN}/preview_supervisor" "${PRE_CFG}" "${RUN}" > /tmp/rtsp_record_preview.log 2>&1 &
  PREVIEW_PID=$!
  sleep 8
fi

echo "[record] publisher: ${PUB_CFG}"
echo "[record] >>> 请面对 IMX415，并对着泰山派板载麦克风说话（预热 ${WARMUP}s + 录 ${SEC}s）<<<"
"${RUN}/publisher_supervisor" "${PUB_CFG}" "${RUN}" 554 0 > /tmp/rtsp_record_pub.log 2>&1 &
for _i in $(seq 1 60); do
  ss -ltnp 2>/dev/null | grep -q '0.0.0.0:554.*rtsp_server' && break
  sleep 1
done

echo "[record] warm-up ${WARMUP}s (ISP min ${MIN_ISP_SEC}s, wait video>=200 + audio>=100)"
OK=0
PUB_START=$(date +%s)
for _i in $(seq 1 "${WARMUP}"); do
  VP=$(grep -o 'video push seq=[0-9]*' /tmp/rtsp_record_pub.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
  AP=$(grep -o 'audio push seq=[0-9]*' /tmp/rtsp_record_pub.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
  NV=$(grep -o 'nv12 tap push seq=[0-9]*' /tmp/rtsp_record_pub.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
  ELAPSED=$(( $(date +%s) - PUB_START ))
  if [ $((_i % 5)) -eq 0 ]; then
    echo "[record] warm ${_i}s(elapsed=${ELAPSED}) video=${VP} nv12=${NV} audio=${AP}"
  fi
  if [ "${ELAPSED}" -ge "${MIN_ISP_SEC}" ] && [ "${VP}" -ge 200 ] 2>/dev/null && [ "${AP}" -ge 100 ] 2>/dev/null; then
    OK=1
    echo "[record] streams ready at ${_i}s (elapsed=${ELAPSED}s)"
    break
  fi
  sleep 1
done

VP=$(grep -o 'video push seq=[0-9]*' /tmp/rtsp_record_pub.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
AP=$(grep -o 'audio push seq=[0-9]*' /tmp/rtsp_record_pub.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
NV=$(grep -o 'nv12 tap push seq=[0-9]*' /tmp/rtsp_record_pub.log 2>/dev/null | tail -1 | grep -o '[0-9]*$' || echo 0)
echo "[record] pre-record video=${VP} nv12=${NV} audio=${AP} ready=${OK}"
grep -iE 'MIPI camera|nv12 tap|audio push|v4l2|Failed|ERROR|Device is busy' \
  /tmp/rtsp_record_pub.log 2>/dev/null | tail -20 || true

if [ "${OK}" != 1 ] && [ "${MODE}" = "camera" ]; then
  echo "[record] FAIL: camera/mic not ready — 勿继续录制"
  [ -n "${PREVIEW_PID}" ] && kill -TERM "${PREVIEW_PID}" 2>/dev/null || true
  "${BOARD}/scripts/stop_all_board.sh"
  exit 1
fi

echo "[record] SDP"
timeout 8 gst-discoverer-1.0 -v "${URL}" 2>&1 | grep -iE 'video codec|audio codec|Width|Height|Sample rate' | head -8 || true

echo "[record] recording ${SEC}s -> ${OUT_MKV}"
rm -f "${OUT_MKV}"
timeout "$((SEC + 25))" gst-launch-1.0 -e \
  rtspsrc location="${URL}" latency=500 protocols=tcp name=src \
  src. ! application/x-rtp,media=video ! rtpjitterbuffer ! rtph265depay ! h265parse \
       ! video/x-h265,stream-format=byte-stream,alignment=au ! queue ! mux. \
  src. ! application/x-rtp,media=audio ! rtpjitterbuffer ! rtpmp4gdepay ! aacparse ! queue ! mux. \
  mpegtsmux name=mux ! filesink location="${OUT_MKV}" sync=false \
  2>/tmp/rtsp_record_gst.log || true

BYTES=$(wc -c < "${OUT_MKV}" 2>/dev/null || echo 0)
echo "[record] file_bytes=${BYTES}"
ls -lh "${OUT_MKV}" 2>/dev/null || true
if [ "${BYTES}" -gt 0 ]; then
  gst-discoverer-1.0 "${OUT_MKV}" 2>&1 | grep -iE 'Duration|video codec|audio codec|Width|Height|Sample rate|Channels' | head -10 || true
fi

[ -n "${PREVIEW_PID}" ] && kill -TERM "${PREVIEW_PID}" 2>/dev/null || true
sleep 2
"${BOARD}/scripts/stop_all_board.sh"

ln -sf "${OUT_MKV}" "${OUT_DIR}/latest_camera.ts"
echo "[record] pull: adb pull ${OUT_MKV}"

if [ "${BYTES}" -ge 500000 ] && [ "${VP}" -ge 200 ] 2>/dev/null; then
  echo "[record] PASS: camera RTSP A/V ts"
  exit 0
fi
echo "[record] FAIL bytes=${BYTES} video_push=${VP}"
exit 1
