# 2026-08-18 对齐 EASY-EAI 显示/采集

## EASY-EAI 做法（Solution 层）
- 摄像头 `rgbcamera_init(720,1280, 270)` — **270° 旋转在采集端**
- 显示 `disp_init(720,1280)` + `disp_commit(BGR888)` — **全屏同分辨率，无 480×800 letterbox**
- RTSP 播放器：解码 NV12 → RGA **ROT_270** + 缩放 → BGR → disp_commit
- 默认 **拉伸铺满** win（不设 crop 则拉伸）

## 我们之前的问题
1. 1920×1080 横屏 **未旋转** 直接 videoscale 到 720×1280 → 人像比例错误
2. 480×800 letterbox 与 EASY-EAI 全屏策略不同
3. `snprintf` 参数错位导致 camera encoder **段错误**（已修）

## 修复
- `publisher.ini`: `cameraRotation=270`
- `encoder`: `videoflip method=counterclockwise ! videoscale` 
- `display_rga`: `displayStretch=1`（EASY-EAI 拉伸铺满）
- `display_output`: NV12→**BGRA8888**（对齐 BGR 显示链路）
- 性能：dumpEvery=0、RGA 在显示线程、pop_latest

## 验收
```
encoder 720 1280 30 8 camera ... 270
video gst: ... videoflip method=counterclockwise ! videoscale ...
video gst pushed 255 access units
```
