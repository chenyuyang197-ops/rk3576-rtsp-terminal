# 会话：三项验收全部 PASS

**日期**：2026-08-20

## 修复

| 问题 | 修复 |
|------|------|
| mic FAIL（push=3） | `publisher_mic.ini` testpattern 替代 camera；按 `audio gst pushed N` 计数 |
| resume ch1=0 | 跳过 preview 前 live2 discover；ch0 先拉流 10s 再启 ch1 |
| mipi 连跑 FAIL | rkaiq 预热 12s + publisher 前多等 3s |

## 验收结果

```
run_resume_verify_board.sh 0  → PASS (ch0 push=1100 ch1 push=1100 YUV=1382400B×2)
run_mic_verify_board.sh       → PASS (push=996 playback=973)
run_mipi_verify_rollback.sh   → PASS (2700 frames ~30fps drm=1)
```
