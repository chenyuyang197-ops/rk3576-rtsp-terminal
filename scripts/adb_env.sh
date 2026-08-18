#!/usr/bin/env bash
# WSL 下复用 Windows adb（adb server 通常只监听 127.0.0.1，socket 转发会超时）
# 用法: source "$(dirname "$0")/adb_env.sh"

find_windows_adb() {
  local candidates=(
    "/mnt/c/Users/${USER}/AppData/Local/Android/Sdk/platform-tools/adb.exe"
    "/mnt/c/Users/cyy/AppData/Local/Microsoft/WinGet/Packages/Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe/platform-tools/adb.exe"
  )
  local p
  for p in "${candidates[@]}"; do
    if [ -f "$p" ]; then
      echo "$p"
      return 0
    fi
  done
  if command -v cmd.exe >/dev/null 2>&1; then
    p=$(cmd.exe /c "where adb" 2>/dev/null | tr -d '\r' | head -1)
    if [ -n "$p" ]; then
      echo "/mnt/c/${p//\\/\/}"
      return 0
    fi
  fi
  return 1
}

setup_rk3576_adb() {
  if [ -n "${RK3576_ADB:-}" ]; then
    ADB="${RK3576_ADB}"
    return 0
  fi

  export PATH="${HOME}/.local/android/platform-tools:${PATH}"

  if grep -qi microsoft /proc/version 2>/dev/null; then
    local win_adb
    if win_adb=$(find_windows_adb); then
      ADB="${win_adb}"
      return 0
    fi
    # 兜底：尝试 socket 转发（需 Windows 侧 adb -a nodaemon server start）
    local win_host
    win_host=$(ip route show default 2>/dev/null | awk '{print $3}')
    if [ -n "${win_host}" ]; then
      export ADB_SERVER_SOCKET="tcp:${win_host}:5037"
    fi
  fi

  ADB="${ADB:-adb}"
}

setup_rk3576_adb
