#!/usr/bin/env bash
set -euo pipefail

RUN_DIR="${1:-/home/lckfb/rk3576-rtsp-terminal/run}"
BOARD_SCRIPT="/home/lckfb/rk3576-rtsp-terminal/scripts/run_on_board.sh"

# shellcheck source=adb_env.sh
source "$(dirname "$0")/adb_env.sh"

echo "[loopback] adb=${ADB}"
echo "[loopback] run on-board script via adb"
"$ADB" shell "chmod +x ${BOARD_SCRIPT} && ${BOARD_SCRIPT} ${RUN_DIR}"
"$ADB" pull /tmp/ch0.yuv /tmp/rk3576-ch0.yuv 2>/dev/null || true
ls -la /tmp/rk3576-ch0.yuv 2>/dev/null || echo "pull ch0.yuv skipped"
