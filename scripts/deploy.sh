#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOARD_DIR="${BOARD_DIR:-/home/lckfb/rk3576-rtsp-terminal}"
BUILD_DIR="${BUILD_DIR:-build-board}"

# shellcheck source=adb_env.sh
source "$(dirname "$0")/adb_env.sh"

echo "[deploy] adb=${ADB}"
echo "[deploy] push sources to board:${BOARD_DIR}"
"$ADB" shell "mkdir -p ${BOARD_DIR}"
"$ADB" push "$ROOT/." "${BOARD_DIR}/"

echo "[deploy] build on board"
"$ADB" shell "cd ${BOARD_DIR} && chmod +x third_party/live555/genMakefiles 2>/dev/null || true && rm -rf ${BUILD_DIR} && (cd third_party/live555 && make clean 2>/dev/null || true) && mkdir -p ${BUILD_DIR} && cd ${BUILD_DIR} && cmake .. && make -j4"

echo "[deploy] install runtime to ${BOARD_DIR}/run"
"$ADB" push "${ROOT}/scripts/stop_all_board.sh" "${BOARD_DIR}/scripts/stop_all_board.sh"
"$ADB" push "${ROOT}/scripts/board_install_bins.sh" "${BOARD_DIR}/scripts/board_install_bins.sh"
"$ADB" shell "chmod +x ${BOARD_DIR}/scripts/stop_all_board.sh ${BOARD_DIR}/scripts/board_install_bins.sh && ${BOARD_DIR}/scripts/board_install_bins.sh ${BOARD_DIR}"

echo "[deploy] setcap for RTSP port 554"
"$ADB" shell "setcap cap_net_bind_service=+ep ${BOARD_DIR}/run/rtsp_server 2>/dev/null || echo 'setcap skipped (run rtsp as root or use port>1024)'"

echo "[deploy] done. binaries in ${BOARD_DIR}/run"
