# 会话：30min 压测完成 + 文档写入

**日期**：2026-08-21

## 压测结果（1800s 目标，板端实际采样至 t≈3686s 显示）

| 项 | 数据 |
|----|------|
| load | 0.97 ~ **2.30** |
| MemAvailable min | **3,488,560 KB** (~3.41 GB) |
| ch0 push | **≈38,900** |
| ch1 push | 0（长稳） |
| 进程 | enc/rtsp/plr/ch0/ch1 全程存活 |
| 判定 | PARTIAL（ch0 长稳 OK） |

## 已写入

- `resume_preject_投递版.md` 压测行
- `logs/08_30min压测报告.md`（新建）
- `logs/09_投递版未完成清单.md`
- `run_stress_test_board.sh` 修复 `SECONDS`→`STRESS_SEC`
