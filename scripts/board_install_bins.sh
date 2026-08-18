#!/usr/bin/env bash
# 板端：stop_all 后安装 build-board 二进制到 run/ 并校验 md5
set -euo pipefail

ROOT="${1:-/home/lckfb/rk3576-rtsp-terminal}"
BUILD="${ROOT}/build-board"
RUN="${ROOT}/run"

"${ROOT}/scripts/stop_all_board.sh"
sleep 1

mkdir -p "${RUN}/config"
for b in test_pattern shm_dump rtsp_channel player preview_supervisor encoder rtsp_server publisher_supervisor; do
  rm -f "${RUN}/${b}"
  cp -f "${BUILD}/${b}" "${RUN}/${b}"
  chmod +x "${RUN}/${b}"
done
cp -r "${ROOT}/config/"* "${RUN}/config/"
sync

run_md5="$(md5sum "${RUN}/rtsp_server" | awk '{print $1}')"
build_md5="$(md5sum "${BUILD}/rtsp_server" | awk '{print $1}')"
echo "[install] rtsp_server run=${run_md5} build=${build_md5}"
if [ "${run_md5}" != "${build_md5}" ]; then
  echo "[install] ERROR: rtsp_server md5 mismatch" >&2
  exit 1
fi
echo "[install] ok"
