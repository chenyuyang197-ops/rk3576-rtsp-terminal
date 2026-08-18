# 会话日志：简历对齐与 logs 全量更新

> 日期：2026-08-16  
> 主题：720×1280 / 554 / RGA / 预览 shm 分离 / logs 更新

## 1. 目标

- 按 `resume_preject_投递版.md` 对齐工程实现
- 更新 `logs/` 全套文档至最新状态
- 明确本地验收文件路径与查看方式

## 2. 环境

```
板卡：泰山派 3M RK3576
系统：Ubuntu 24.04.4 TF (mmcblk1p8)
adb：e621a57dd90902b3 device
WSL：ffmpeg 6.1.1 已安装
```

## 3. 本日工程变更摘要

| 模块 | 变更 |
|------|------|
| `encoder_main.c` | 720×1280；H.265 **AU 切分** |
| `frame_queue.c` | 预览 shm `0x5300` 与发布分离 |
| `display_rga.c` | RGA 轮巡 5s/40ms → dump |
| `net_init.c` | INI 静态 IP（end0） |
| `publisher/main.c` | 读 ini；554 |
| `run_resume_demo.sh` | 简历一键验收 |

## 4. 验收数据

| 指标 | 期望 | 实际 | 结论 |
|------|------|------|------|
| ch0.yuv | 1382400 | 1382400 | ✅ |
| display_720x1280.yuv | 1382400 | 1382400（WSL pull） | ✅ |
| RTSP 端口 | 554 | rtsp_server 554 | ✅ |
| encoder | 720×1280 | 命令行一致 | ✅ |
| 故障隔离 | kill ch0 | encoder/player 存活 | ✅ |
| ch1.yuv | 1382400 | 偶发缺失 | ⚠️ |

## 5. 本地文件（WSL）

```
/tmp/rk3576-smoke/t.h265      1240063 B
/tmp/rk3576-smoke/mic.wav      529244 B
/tmp/rk3576-ch0.yuv           1382400 B
/tmp/rk3576-display_720x1280.yuv 1382400 B
/tmp/rk3576-ch0.png           ~197 KB
/tmp/rk3576-display.png       ~223 KB
```

## 6. logs 更新清单

- `00_进度总览.md` — 简历对齐阶段、对齐表、复现命令
- `03_板内自环日志.md` — 720×1280/554/RGA 章节
- `04_面试复习对照.md` — 数字、ffplay 6.x、去 EASY-EAI 口述
- `05_命令与输出原文.md` — § J/K
- `06_踩坑与修复记录.md` — § 13–18
- `07_简历对齐路线图.md` — 精简同步
- `08_本地验收文件索引.md` — **新增**
- `README.md` — 索引更新

## 7. 踩坑（本日）

- ffplay `-pix_fmt` → 改用 `-pixel_format -video_size`（§16）
- 预览 attach failed → 预览 shm 分离（§14）
- Live555 StreamParser 崩溃 → AU 切分（§13）

## 8. 下一步

- [ ] IMX415 接入 V4L2
- [ ] 2 路 ch1 稳定 + wait 退出码日志
- [ ] 30 min 压测
- [ ] 投递稿填实测 CPU/内存
