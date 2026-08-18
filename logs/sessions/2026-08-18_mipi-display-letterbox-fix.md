# 2026-08-18 MIPI 显示 letterbox / RTSP 修复

## 目标
修复 MIPI 屏「显示很奇怪」：画面拉伸变形、RGA 花屏、双路轮巡闪烁。

## 根因
1. **非等比缩放**：720×1280 → 480×800 时 X/Y 缩放比不同，画面被拉变形。
2. **RGA imresize 失败**：letterbox 目标宽 450 未 16 字节对齐，大量 `imresize error factor=0.625`，缩放失败导致花屏/黑屏。
3. **双路 5s 轮巡**：ch0/ch1 同 URL 仍每 5s 切换，造成闪烁感。
4. **RTSP 仅 IPv6**：偶发 IPv4 bind 失败，客户端连 `192.168.1.100:554` 失败无画面。

## 修复
- `display_rga.c`：等比 letterbox + `improcess`；缩放宽高 **16 对齐**（450→448）。
- `display_output.c`：DRM 输出 RGBA8888（此前已改）。
- `loopback.ini`：`rotateSeconds = 0`；`player_main.c` 仅在 rotate>0 且多路时轮巡。
- `net_init.c`：无 carrier 时把静态 IP 挂到 `lo`。
- `rtsp_server_main.cpp`：`RkRTSPServer` 强制 IPv4 bind 成功才启动。

## 验收命令
```bash
source scripts/adb_env.sh
adb shell /home/lckfb/rk3576-rtsp-terminal/scripts/run_mipi_hw_demo_board.sh
adb shell "grep -c 'imresize error' /tmp/preview.log"
```

## 验收结果
```
[mipi-hw] RTSP IPv4 :554 ready (45s)
[mipi-hw] camera_ok=1 push_max=4400 decoded ch0=1205 ch1=1045
[mipi-hw] ch0=1382400 ch1=1382400 display=0 drm_mipi=1 expect_ch=1382400
imresize error count: 0
```

## 目视预期
- 720×1280 摄像头画面等比缩放到 448×800，左右各约 16px 黑边，不再横向/纵向拉伸。
- 无双路 5s 切换闪烁。
