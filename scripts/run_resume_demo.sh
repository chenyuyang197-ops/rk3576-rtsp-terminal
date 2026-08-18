#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUN_DIR="${1:-/home/lckfb/rk3576-rtsp-terminal/run}"
BOARD_SCRIPT="/home/lckfb/rk3576-rtsp-terminal/scripts/run_on_board.sh"

# shellcheck source=adb_env.sh
source "$(dirname "$0")/adb_env.sh"

echo "[resume-demo] adb=${ADB}"
echo "[resume-demo] deploy skipped — run deploy.sh first if code changed"
echo "[resume-demo] run board script"
"$ADB" shell "chmod +x ${BOARD_SCRIPT} && ${BOARD_SCRIPT} ${RUN_DIR}" | tee /tmp/rk3576-resume-demo.log

for f in ch0.yuv ch1.yuv display_480x800.yuv; do
  "$ADB" pull "/tmp/${f}" "/tmp/rk3576-${f}" 2>/dev/null || true
done
ls -la /tmp/rk3576-ch0.yuv /tmp/rk3576-ch1.yuv /tmp/rk3576-display_480x800.yuv 2>/dev/null || true

bash "${ROOT}/scripts/log_session.sh" "resume-demo"
