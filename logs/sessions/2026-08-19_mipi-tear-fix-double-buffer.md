# 2026-08-19 MIPI 晃动破图修复

## 现象
local NV12 30fps 下，晃动摄像头时屏幕出现撕裂/破图。

## 根因
1. **DRM 单 framebuffer**：RGA 往正在扫描输出的 FB 写 RGBA → 典型 tearing
2. **display_rga 单 src 缓冲**：player 更新与 RGA 读同一 NV12（虽有 mutex，快速运动时仍易在边界竞态）

## 修复
- `display_output.c`：双 dumb buffer，RGA 写 back buffer，`drmModePageFlip`（失败则 SetCrtc）再切换
- `display_rga.c`：NV12 双缓冲 ping-pong，render 时 snapshot 再 RGA

## 验收
```
[verify] PASS: local NV12 MIPI (~30 fps) drm=1 local_frames=2700
```

## 待用户确认
板端重新跑 demo，晃动摄像头目视是否还有破图。
