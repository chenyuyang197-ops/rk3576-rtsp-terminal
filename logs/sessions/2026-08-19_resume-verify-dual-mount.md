# 会话：投递版补齐 — 双 mount 镜像 + 文档同步

**日期**：2026-08-19  
**目标**：补齐投递版待验项（2 路 RTSP、AAC 双轨、验收脚本、简历文档）

## 环境

- 板卡：RK3576 泰山派，adb `e621a57dd90902b3`
- 板端：`/home/lckfb/rk3576-rtsp-terminal/run/`

## 代码改动

| 模块 | 改动 |
|------|------|
| `encoder_main.c` | `ENC_PUB_CHANNELS=2` 镜像 push 视频/音频 |
| `rtsp_server_main.cpp` | `/live`→ch0、`/live2`→ch1；AU pending/truncate |
| `publisher/main.c` + INI | `rtspStreamChannels=2` |
| `preview/main.c` | RTSP 双路错峰 3s+2s |
| `stop_all_board.sh` | 清理 0x5201/0x5211/0x5301；`fuser -k 554` |
| `run_resume_verify_board.sh` | 120s 双路 + discoverer + 故障隔离 |

## 验收命令

```bash
source scripts/adb_env.sh
adb shell /home/lckfb/rk3576-rtsp-terminal/scripts/run_resume_verify_board.sh 0
adb shell /home/lckfb/rk3576-rtsp-terminal/scripts/diag_2ch_preview_no_discover.sh
```

## 关键结果

| 项 | 结果 |
|----|------|
| gst-discoverer `/live` | H.265 720×1280 + AAC 44100 2ch ✅ |
| ch0 RTSP 120s | push≈2900, decoded≈2200, YUV=1382400B ✅ |
| ch1 单测 rtsp_channel | push≈228~348/10s ✅ |
| ch1 full preview | push=0 ❌（player+audio 同跑时） |
| 故障隔离 kill ch0 | encoder/rtsp/player/ch1 存活 ✅ |
| 双 mount 架构 | 镜像 shm + 分通道取帧 ✅ |

## 与简历差距

- 已更新 `resume_preject_投递版.md`、`logs/09`、`logs/00`
- 仍待：ch1 full preview 稳定、30min 压测、外网 VLC

## 下一步

1. Live555 大 AU fMaxSize 调优，减少 gst SIGSEGV
2. full preview 下抓 ch1 RTSP 握手日志
3. PC 侧 VLC 拉流 + 听音
