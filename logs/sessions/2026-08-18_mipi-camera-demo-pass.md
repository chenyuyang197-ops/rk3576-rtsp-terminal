# 2026-08-18 MIPI 摄像头全链路 demo 通过

## 目标
IMX415 实采 → H.265 RTSP → 双路预览硬解 → MIPI 480×800 轮巡

## 修复
- 去掉 `v4l2-ctl --set-fmt-video=...NV12`（破坏 rkisp NM12 与 GStreamer 协商）
- `camera_release.c` 增加 `rkaiq_3A` 重启
- `encoder_main.c` camera 模式 release 后 sleep 3s
- `run_mipi_hw_demo_board.sh` 对齐 IPv4/554 等待，不在 encoder 前做 gst 冒烟

## 命令
```bash
adb shell "date -s '2026-08-18 21:48:00'"   # 避免 clock skew
bash scripts/run_mipi_hw_demo_board.sh      # 或 WSL 侧 push 脚本后板端执行
```

## 验收（2026-08-18 21:50）

```
video push seq=3250+
decoded ch0=1375 ch1=1205
ch0=1382400 ch1=1382400 drm_mipi=1
```

- IMX415 → mpph265enc → RTSP ✅
- 双路 rtsp_channel + MPP 硬解 YUV ✅
- DRM MIPI 480×800 轮巡 ✅
