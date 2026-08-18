#!/usr/bin/env bash
# WSL/主机：推送 bringup + 部署 + 跑 MIPI 硬件 demo
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUN_DIR="${1:-/home/lckfb/rk3576-rtsp-terminal/run}"
BOARD_DIR="${BOARD_DIR:-/home/lckfb/rk3576-rtsp-terminal}"

# shellcheck source=adb_env.sh
source "$(dirname "$0")/adb_env.sh"

adb_ok() {
  "$ADB" get-state 2>/dev/null | grep -q device
}

echo "[mipi-hw] adb=${ADB}"
if ! adb_ok; then
  echo "[mipi-hw] ERROR: adb 未连接，请确认 Windows 上 adb devices 正常"
  exit 1
fi

echo "[mipi-hw] push + deploy"
bash "${ROOT}/scripts/deploy.sh"

echo "[mipi-hw] MIPI bringup on board (enable overlay if needed)"
"$ADB" shell "chmod +x ${BOARD_DIR}/scripts/mipi_bringup.sh && ${BOARD_DIR}/scripts/mipi_bringup.sh" | tee /tmp/rk3576-mipi-bringup.log

if grep -q "请执行 DO_REBOOT=1" /tmp/rk3576-mipi-bringup.log 2>/dev/null; then
  echo "[mipi-hw] 首次启用 MIPI overlay，正在 reboot..."
  "$ADB" shell "DO_REBOOT=1 ${BOARD_DIR}/scripts/mipi_bringup.sh" || true
  echo "[mipi-hw] 等待板子重启..."
  sleep 25
  for _ in $(seq 1 30); do
    adb_ok && break
    sleep 2
  done
  adb_ok || { echo "[mipi-hw] reboot 后 adb 未恢复"; exit 1; }
  "$ADB" shell "${BOARD_DIR}/scripts/mipi_bringup.sh" | tee -a /tmp/rk3576-mipi-bringup.log
fi

echo "[mipi-hw] run RTSP + camera + MIPI preview"
"$ADB" shell "chmod +x ${BOARD_DIR}/scripts/run_mipi_hw_demo_board.sh && ${BOARD_DIR}/scripts/run_mipi_hw_demo_board.sh ${RUN_DIR}" | tee /tmp/rk3576-mipi-hw-demo.log

for f in cam_smoke.nv12 ch0.yuv display_480x800.yuv; do
  "$ADB" pull "/tmp/${f}" "/tmp/rk3576-${f}" 2>/dev/null || true
done

bash "${ROOT}/scripts/log_session.sh" "mipi-hw-demo"
