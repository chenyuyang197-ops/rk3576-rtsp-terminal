# 会话：三项验收连跑 PASS + 30min 压测启动

**日期**：2026-08-21

## 已完成

| 脚本 | 结果 |
|------|------|
| `run_resume_verify_board.sh 0` | PASS ch0/ch1 push≈1100 YUV×2 |
| `run_mic_verify_board.sh` | PASS push=996 playback=974 |
| `run_mipi_verify_rollback.sh` | PASS 2700 frames drm=1 |

## 进行中

```bash
adb shell /home/lckfb/rk3576-rtsp-terminal/scripts/run_stress_test_board.sh /home/lckfb/rk3576-rtsp-terminal/run 1800
```

日志：`/tmp/stress_test_result.txt`（板端）

60s 采样：load=1.36 mem_avail≈3.5GB ch0=1 ch1=1 plr=1

## 文档

- `resume_preject_投递版.md` 已更新双路/麦克风数据
- `logs/09_投递版未完成清单.md` 已更新
