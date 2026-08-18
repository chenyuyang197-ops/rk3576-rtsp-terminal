# 会话日志：初始移植与板内自环打通

> 日期：2026-08-16  
> 主题：RK3576 RTSP 最小硬件计划 5/5 完成

## 1. 目标

- 在泰山派 3M Ubuntu 24 TF 卡上完成 RTSP 终端移植
- 无屏/无网条件下板内自环验收
- 建立 logs 与 skill 供面试复习

## 2. 环境快照

```
PRETTY_NAME="Ubuntu 24.04.4 LTS"
/ on /dev/mmcblk1p8 (TF SD64G)
aarch64
adb: e621a57dd90902b3 device
```

硬件：无 HDMI/MIPI/网线/WiFi 天线。

## 3. 主要执行命令

```bash
bash audio-video/rk3576-rtsp-terminal/scripts/deploy.sh
bash audio-video/rk3576-rtsp-terminal/scripts/run_loopback.sh
# 或板端
/home/lckfb/rk3576-rtsp-terminal/scripts/run_on_board.sh
```

## 4. 关键输出

见 `logs/05_命令与输出原文.md` 章节 F。

**核心验收**：`/tmp/ch0.yuv` = **1382400 字节**

## 5. 验收表

| 指标 | 期望 | 实际 | 结论 |
|------|------|------|------|
| 计划 todo 5 项 | 全部完成 | 全部完成 | ✅ |
| t.h265 冒烟 | >1MB | 1240063 B | ✅ |
| mic.wav | ~529KB | 529244 B | ✅ |
| RTSP PLAY | 成功 | Sent PLAY request | ✅ |
| ch0.yuv | 1382400 | 1382400 | ✅ |
| kill rtsp_channel 0 | 其他进程存活 | encoder/server/player 仍在 | ✅ |

## 6. 与简历对齐

**已推进**：
- 多进程 + shm + Live555 架构
- fork 取流进程 + dump 代替 RGA 显示
- 板内 RTSP 闭环

**仍缺**：
- 真实 MIPI 摄像头、720×1280 竖屏
- RGA 轮巡 HDMI
- 2 路完整故障隔离日志
- 30min 压测 CPU/内存数据填 resume 占位符

## 7. 产出物

- 工程：`audio-video/rk3576-rtsp-terminal/`
- 日志：`logs/00`～`06`、`logs/sessions/`
- Skill：`.cursor/skills/rk3576-rtsp-terminal/`
- 脚本：`log_session.sh`

## 8. 下一步

- [ ] 2 路 loopback + wait 退出码日志
- [ ] AAC 独占声卡验证
- [ ] 有网后 VLC 外拉截图
