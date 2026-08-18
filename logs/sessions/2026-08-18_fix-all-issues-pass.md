# 2026-08-18 修复全部问题 — 双路 RTSP 自环验收通过

## 目标
系统性修复 deploy/RTSP/解码/双路预览问题，跑通 resume 自环 demo。

## 关键修复

| 问题 | 根因 | 修复 |
|------|------|------|
| 板端二进制不更新 | `pkill -9 -f rk3576-rtsp-terminal` 误杀 adb shell | `stop_all_board.sh` 改为匹配 `${RUN}/二进制名`；新增 `board_install_bins.sh` + md5 校验 |
| RTSP server SIGABRT/SIGSEGV | 整 AU 喂 Framer 导致 StreamParser 溢出；双客户端 fq_pop 抢帧 | `ShmNalFramedSource` 按 NAL 拆分；`reuseFirstSource=True`；双路同 URL `/live` |
| 127.0.0.1 连不上 554 | 仅 IPv6 监听 | `run_on_board.sh` 启动前配置 end0 `192.168.1.100/24`；RTSP URL 改 `192.168.1.100` |
| player 无 YUV dump | MPP 未处理 `info_change` | `MPP_DEC_SET_INFO_CHANGE_READY` + `MPP_DEC_SET_PARSER_SPLIT_MODE` |
| rtsp_channel 帧太碎 | 按 NAL 推送 | 按 VCL 组装完整 AU 再 push |

## 验收命令

```bash
bash audio-video/rk3576-rtsp-terminal/scripts/deploy.sh   # 代码变更后
adb push .../scripts/run_on_board.sh .../config/loopback.ini  # 脚本/配置变更后
adb shell /home/lckfb/rk3576-rtsp-terminal/scripts/run_on_board.sh
```

## 验收结果（2026-08-18 01:53）

```
[resume] decoded frames ch0=705 ch1=575
[resume] ch0=1382400 ch1=1382400 display=0 drm_mipi=1 expect_ch=1382400 expect_disp=576000
```

- `/tmp/ch0.yuv` = **1382400 B** ✅
- `/tmp/ch1.yuv` = **1382400 B** ✅
- 显示：**DRM MIPI 480×800** ✅（走硬件显示，无 dump 文件属正常）
- 双路 `rtsp_channel` 均存活，故障隔离后 encoder/rtsp_server/player 正常

## 下一步
- MIPI 摄像头全链路 demo（`run_mipi_hw_demo.sh`）
- 30min 压测
