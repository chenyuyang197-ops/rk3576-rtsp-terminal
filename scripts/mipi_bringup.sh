#!/usr/bin/env bash
# 板端：MIPI 屏（C42388916）+ IMX415 摄像头 调通脚本
# 参考：
#   https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-usage/ubuntu24-usage/mipi-display.html
#   https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/system-usage/ubuntu24-usage/mipi-camera.html
#   https://wiki.lckfb.com/zh-hans/tspi-3-rk3576/sys-tool/ovtree.html
set -euo pipefail

CAM_DEV="${CAM_DEV:-/dev/video-camera0}"
SCREEN_ID="${SCREEN_ID:-c42388916}"
DO_REBOOT="${DO_REBOOT:-0}"

log() { echo "[mipi-bringup] $*"; }

need_root() {
  if [ "$(id -u)" -ne 0 ]; then
    log "需要 root，尝试 sudo 重新执行..."
    exec sudo -E "$0" "$@"
  fi
}

section() {
  echo
  echo "========== $* =========="
}

find_screen_overlay() {
  local overlay=""
  if command -v ovtree >/dev/null 2>&1; then
    overlay=$(ovtree list 2>/dev/null | grep -ioE '"[^"]*'"${SCREEN_ID}"'[^"]*\.dtbo"' | tr -d '"' | head -1 || true)
  fi
  if [ -z "${overlay}" ] && [ -d /boot/overlays ]; then
    overlay=$(ls /boot/overlays/*"${SCREEN_ID}"*.dtbo 2>/dev/null | xargs -n1 basename | head -1 || true)
  fi
  echo "${overlay}"
}

enable_mipi_screen() {
  section "1. MIPI 屏 overlay（${SCREEN_ID}）"
  if ! command -v ovtree >/dev/null 2>&1; then
    log "WARN: 未找到 ovtree，请确认 Ubuntu24 官方镜像；或手动改内核 dtsi"
    return 0
  fi
  local overlay
  overlay=$(find_screen_overlay)
  if [ -z "${overlay}" ]; then
    log "ERROR: 未找到 ${SCREEN_ID} 对应 .dtbo，执行: ovtree list"
    ovtree list 2>/dev/null | head -80 || true
    return 1
  fi
  log "找到 overlay: ${overlay}"
  if ovtree status 2>/dev/null | grep -qi "${overlay}"; then
    log "ovtree 状态: 已启用 ${overlay}"
  else
    ovtree disable-all 2>/dev/null || true
    ovtree enable "${overlay}"
    log "已启用 ${overlay}"
    NEED_REBOOT=1
  fi
  if ! verify_dsi_in_dtb; then
    log "ERROR: 设备树中无 DSI/MIPI 节点，overlay 未生效"
    log "  1) 确认 31P 接在 C42388916 扩展板 ↔ 泰山派 MIPI 座"
    log "  2) 执行: sudo ovtree enable ${overlay} && sync"
    log "  3) **断电重启**（拔电源，不要只用 adb reboot）"
    log "  4) 重启后本脚本应看到 /proc/device-tree 含 dsi"
    NEED_REBOOT=1
  else
    log "OK: DSI 设备树节点已加载"
  fi
}

verify_dsi_in_dtb() {
  find /proc/device-tree -iname '*dsi*' 2>/dev/null | grep -q .
}

check_display() {
  section "2. 显示节点"
  ls -la /dev/dri/ 2>/dev/null || log "WARN: 无 /dev/dri"
  dmesg | grep -iE 'dsi|panel|drm|vop' | tail -15 || true
  if [ -d /sys/class/drm ]; then
    for s in /sys/class/drm/card*-*/status; do
      [ -f "$s" ] && echo "$(basename "$(dirname "$s")"): $(cat "$s")"
    done
  fi
}

check_camera() {
  section "3. 摄像头节点"
  ls -la /dev/video* 2>/dev/null || true
  if [ -e "${CAM_DEV}" ]; then
    log "OK: ${CAM_DEV} 存在"
  else
    log "WARN: ${CAM_DEV} 不存在，尝试列出 v4l2-ctl"
  fi
  if command -v v4l2-ctl >/dev/null 2>&1; then
    v4l2-ctl --list-devices 2>/dev/null || true
    if [ -e "${CAM_DEV}" ]; then
      v4l2-ctl -d "${CAM_DEV}" --all 2>/dev/null | head -40 || true
    fi
  fi
  if command -v media-ctl >/dev/null 2>&1; then
    media-ctl -p 2>/dev/null | head -50 || true
  fi
}

smoke_camera_capture() {
  section "4. 摄像头冒烟（NV12 → /tmp/cam_smoke.nv12）"
  if [ ! -e "${CAM_DEV}" ]; then
    log "SKIP: 无摄像头设备"
    return 0
  fi
  pkill -f "gst-launch.*${CAM_DEV}" 2>/dev/null || true
  killall encoder 2>/dev/null || true
  sleep 2
  systemctl restart rkaiq_3A.service 2>/dev/null || true
  sleep 2
  if ! v4l2-ctl -d "${CAM_DEV}" \
      --set-fmt-video=width=1920,height=1080,pixelformat=NV12 \
      --stream-mmap=3 --stream-count=3 --stream-to=/tmp/cam_smoke.nv12 2>/tmp/cam_gst.log; then
    log "ERROR: v4l2-ctl 采集失败"
    tail -20 /tmp/cam_gst.log 2>/dev/null || true
    dmesg | grep -iE 'csi size err|imx415' | tail -5 || true
    log "提示: 断电重插摄像头排线；绿屏常因用 ffplay 按错格式查看 NM12"
    return 1
  fi
  local sz exp ymean
  sz=$(wc -c < /tmp/cam_smoke.nv12)
  exp=$((1920 * 1080 * 3 / 2 * 3))
  log "cam_smoke.nv12 size=${sz} expect=${exp}"
  ymean=$(python3 -c "import numpy as np;y=np.fromfile('/tmp/cam_smoke.nv12',np.uint8,count=1920*1080);print(int(y.mean()))" 2>/dev/null || echo "?")
  log "首帧 Y 均值=${ymean}（正常场景通常 >40；接近 0 为黑帧/CSI 异常）"
}

smoke_camera_display() {
  section "5. 摄像头 → 屏幕（官方命令，8s）"
  if [ ! -e "${CAM_DEV}" ]; then
    log "SKIP"
    return 0
  fi
  if [ ! -d /dev/dri ]; then
    log "SKIP: 无 DRM，可能 MIPI 屏 overlay 未生效"
    return 0
  fi
  pkill -f "gst-launch.*${CAM_DEV}" 2>/dev/null || true
  sleep 1
  log "运行 8s autovideosink（应在 MIPI 屏上看到画面）..."
  timeout 8 gst-launch-1.0 v4l2src device="${CAM_DEV}" \
    ! "video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1" \
    ! autovideosink 2>/tmp/cam_display.log || true
  tail -10 /tmp/cam_display.log 2>/dev/null || true
}

main() {
  need_root "$@"
  NEED_REBOOT=0
  enable_mipi_screen || true
  if [ "${NEED_REBOOT}" = "1" ] && [ "${DO_REBOOT}" = "1" ]; then
    log "重启中..."
    reboot
  elif [ "${NEED_REBOOT}" = "1" ]; then
    log "请执行 DO_REBOOT=1 $0 或手动 reboot 后再跑后续步骤"
    exit 0
  fi
  check_display
  check_camera
  smoke_camera_capture || true
  smoke_camera_display || true
  section "完成"
  log "下一步: bash scripts/run_mipi_hw_demo.sh（RTSP + MIPI 轮巡）"
}

main "$@"
