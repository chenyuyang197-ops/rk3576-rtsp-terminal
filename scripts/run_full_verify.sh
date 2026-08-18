#!/usr/bin/env bash
# WSL：部署 + 简历自环 + MIPI 实采 demo + 可选压测
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUN_DIR="${1:-/home/lckfb/rk3576-rtsp-terminal/run}"
STRESS_SEC="${STRESS_SEC:-0}"

# shellcheck source=adb_env.sh
source "$(dirname "$0")/adb_env.sh"

echo "[full-verify] deploy"
bash "${ROOT}/scripts/deploy.sh"

echo "[full-verify] resume loopback (testpattern)"
bash "${ROOT}/scripts/run_resume_demo.sh" "${RUN_DIR}"

echo "[full-verify] MIPI camera demo"
bash "${ROOT}/scripts/run_mipi_hw_demo.sh" || echo "[full-verify] mipi demo skipped/failed"

if [ "${STRESS_SEC}" -gt 0 ]; then
  echo "[full-verify] stress ${STRESS_SEC}s"
  BOARD_STRESS="/home/lckfb/rk3576-rtsp-terminal/scripts/run_stress_test_board.sh"
  "$ADB" shell "chmod +x ${BOARD_STRESS} && ${BOARD_STRESS} ${RUN_DIR} ${STRESS_SEC}" | tee /tmp/rk3576-stress.log
fi

bash "${ROOT}/scripts/log_session.sh" "full-resume-verify"
