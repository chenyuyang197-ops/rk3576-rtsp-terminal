# 会话日志：resume-implementation-push

> 日期：2026-08-18  
> 目标：对齐 `resume_preject_投递版.md`，尽量落地简历条目

## 环境

- 板卡：立创泰山派 3M RK3576 · Ubuntu 24 TF 卡 · adb `e621a57dd90902b3`
- WSL adb：Windows `adb.exe`（`~/.bashrc` + `scripts/adb_env.sh`）

## 本次代码/脚本改动

| 项 | 文件 |
|----|------|
| RTSP 双 mount（`/live` + `/live2`） | `publisher/rtsp_server_main.cpp` |
| RTSP 大帧缓冲 16MB + server 崩溃重启 | `publisher/rtsp_server_main.cpp`, `publisher/main.c` |
| 双路错峰启动 + dump 间隔 5 | `preview/main.c` |
| rtsp 拉流 TCP + h265parse | `preview/rtsp_channel_main.c` |
| testpattern 自环配置 | `config/publisher_loopback.ini` |
| 双 URL 配置 | `config/loopback.ini` |
| 压测/全量验收/AAC 冒烟 | `scripts/run_stress_test_board.sh`, `run_full_verify.sh`, `run_aac_smoke_board.sh` |
| 554 就绪等待 | `scripts/run_on_board.sh` |

## 已验证（有输出）

| 验收项 | 结果 |
|--------|------|
| IMX415 v4l2 抓帧 | `/tmp/test.nv12` = **3,110,400 B** |
| MIPI kmssink 彩条 | `gst-launch videotestsrc ! kmssink` PREROLLED/PLAYING |
| shm H.265 AU | `00000001` 起始，5 帧 dump 正常 |
| ch1 rtsp_channel（单次） | push **51 frames**（修复前中间态） |
| encoder 持续推流 | video push seq **2300+** |

## 未稳定 / 回归

| 项 | 现象 |
|----|------|
| 双路 RTSP 自环 YUV dump | ch0/ch1 偶发 0 帧；Live555 双客户端曾 SIGABRT/SIGSEGV |
| `run_resume_demo.sh` 本轮 | ch0/ch1 yuv 未生成，需板端时钟同步 + 全量 deploy 后再跑 |

## 下一步

1. 板端 `sudo ntpdate` 或手动对时，消除 cmake clock skew  
2. `bash scripts/deploy.sh` 全量部署后 `run_resume_demo.sh`  
3. `run_mipi_hw_demo.sh` 摄像头 RTSP + MIPI 预览  
4. `STRESS_SEC=300 run_full_verify.sh` 填压测数据  

## 简历对齐

- ✅ MIPI/V4L2 实采（代码 + v4l2 验收）
- ✅ MIPI 480×800 显示路径（kmssink + display_output DRM）
- ⚠️ 双路 RTSP 并行帧数（架构 + live2，验收待稳）
- ⚠️ AAC 端到端、30min 压测、外网 VLC
