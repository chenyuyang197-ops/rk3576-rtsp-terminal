#!/usr/bin/env bash
# WSL 下统一走 Windows adb.exe（避免 ADB_SERVER_SOCKET 连 5037 超时）
# 用法: ./scripts/adb.sh devices   或  source scripts/adb_env.sh && "$ADB" devices
DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=adb_env.sh
source "${DIR}/adb_env.sh"
exec "$ADB" "$@"
