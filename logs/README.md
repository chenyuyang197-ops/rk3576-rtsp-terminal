# 调试日志目录

> 最后更新：2026-08-16

本目录记录 RK3576 RTSP 的**进度、命令、输出、简历对齐与面试对照**。

## 强制规范（Skill）

每次板端测试或改代码后：

```bash
bash audio-video/rk3576-rtsp-terminal/scripts/log_session.sh "简短主题"
```

Skill：`audio-video/rk3576-rtsp-terminal/.cursor/skills/rk3576-rtsp-terminal/SKILL.md`

---

## 阅读顺序

| 文件 | 内容 |
|------|------|
| [00_进度总览.md](./00_进度总览.md) | **最新进度、简历对齐表、复现命令** |
| [01_环境搭建记录.md](./01_环境搭建记录.md) | TF / adb / deploy |
| [02_冒烟与验证日志.md](./02_冒烟与验证日志.md) | MPP / ALSA |
| [03_板内自环日志.md](./03_板内自环日志.md) | 自环 + **简历 demo 720×1280/554/RGA** |
| [04_面试复习对照.md](./04_面试复习对照.md) | 话术、数字、ffplay 注意 |
| [05_命令与输出原文.md](./05_命令与输出原文.md) | 终端输出存档 |
| [06_踩坑与修复记录.md](./06_踩坑与修复记录.md) | **18+ 条**踩坑 |
| [07_简历对齐路线图.md](./07_简历对齐路线图.md) | 与投递稿对照 |
| [08_本地验收文件索引.md](./08_本地验收文件索引.md) | **YUV/H.265/WAV/PNG 路径与查看** |
| [sessions/](./sessions/) | 按日期会话 |

---

## 本地媒体文件（WSL）

| 路径 | 说明 |
|------|------|
| `/tmp/rk3576-smoke/t.h265` | 冒烟 H.265 |
| `/tmp/rk3576-smoke/mic.wav` | 冒烟录音 |
| `/tmp/rk3576-ch0.yuv` | 解码 720×1280 NV12 |
| `/tmp/rk3576-display_720x1280.yuv` | RGA 轮巡 dump |
| `/tmp/rk3576-ch0.png` | 彩条截图 |

---

## 复现

```bash
bash ../scripts/deploy.sh
bash ../scripts/run_resume_demo.sh
ffplay -f rawvideo -pixel_format nv12 -video_size 720x1280 /tmp/rk3576-ch0.yuv
explorer.exe /tmp/rk3576-ch0.png
bash ../scripts/log_session.sh "after-test"
```

---

## 关联文档

- 投递稿：`../../resume_preject_投递版.md`
- 完整版：`../../resume_preject.md`
