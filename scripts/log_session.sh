#!/usr/bin/env bash
# 创建新的 RK3576 会话日志骨架，可选采集 adb 环境快照
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOPIC="${1:-session}"
DATE="$(date +%Y-%m-%d)"
STAMP="$(date +%H%M)"
SAFE_TOPIC="$(echo "$TOPIC" | tr ' /:' '___' | tr -cd '[:alnum:]_-')"
SESSION_DIR="${ROOT}/logs/sessions"
OUT="${SESSION_DIR}/${DATE}_${STAMP}_${SAFE_TOPIC}.md"

mkdir -p "${SESSION_DIR}"

# shellcheck source=adb_env.sh
source "$(dirname "$0")/adb_env.sh"

ENV_BLOCK="（adb 未连接，请手动补充）"
if "$ADB" devices 2>/dev/null | grep -q 'device$'; then
  ENV_BLOCK="$("$ADB" shell 'echo "=== os-release ==="; head -3 /etc/os-release; echo "=== findmnt ==="; findmnt /; echo "=== uname ==="; uname -m' 2>/dev/null || echo 'adb shell failed')"
fi

cat > "${OUT}" <<EOF
# 会话日志：${TOPIC}

> 日期：$(date '+%Y-%m-%d %H:%M:%S')  
> 自动生成：scripts/log_session.sh

## 1. 目标

（填写本次目标）

## 2. 环境快照（自动采集）

\`\`\`
${ENV_BLOCK}
\`\`\`

## 3. 执行命令

\`\`\`bash
# 在此粘贴完整命令
\`\`\`

## 4. 原始输出

\`\`\`
# 在此粘贴终端输出
\`\`\`

## 5. 验收数据

| 指标 | 期望 | 实际 | 结论 |
|------|------|------|------|
| ch0.yuv 大小 | 1382400 | | |
| t.h265 冒烟 | ~1240063 | | |
| RTSP PLAY | 成功 | | |

## 6. 与简历对齐（resume_preject.md）

- 本次推进：
- 仍缺：

## 7. 踩坑

（无则删本节；有则同步写入 logs/06_踩坑与修复记录.md）

## 8. 下一步

- [ ] 

EOF

echo "Created: ${OUT}"
echo "Edit the file and fill sections 1, 3, 4, 5 before ending the session."
