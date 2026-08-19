# 2026-08-19 local NV12 旁路验收通过

## 目标
解决 MIPI 预览仍卡顿：绕过 RTSP+MPP 解码，encoder GStreamer tee 直送 NV12 到 preview shm。

## 根因（本次失败）
1. `publisher_supervisor` 编译失败：`sup_add11` 少传第 11 个参数
2. `frame_queue` slot 512KB→1536KB 后，旧 shm 段（8388628B）无法按新尺寸重建，`preview/encoder` 均报 `shm create failed`

## 修复
- `frame_queue.c`：EEXIST 时用 `shmget(key,0)` 删除旧段再创建
- `publisher/main.c`：`sup_add11(..., tap_arg, NULL)`
- `stop_all_board.sh`：停止时清理 0x5200/0x5300 系列 shm
- `run_mipi_verify_rollback.sh`：local 模式 preview 先于 publisher

## 命令
```bash
source scripts/adb_env.sh
adb shell /home/lckfb/rk3576-rtsp-terminal/scripts/run_mipi_verify_rollback.sh
```

## 验收输出（90s）
```
=== mipi verify mode=local ===
[verify] drm=1 local_frames=2700 nv12_tap=2700 rtsp_push=0
nv12 tap push seq=2700 depth=1
[verify] PASS: local NV12 MIPI (~30 fps display)
```

## 结论
- 本地 NV12 旁路链路稳定 ~30fps（2700/90s）
- RTSP 回环路径仍是卡顿来源（MPP 解码 ~17fps）；MIPI 默认建议 `previewMode=local` + `localNv12Tap=1`

## 下一步
- 用户目视确认 local 模式是否流畅
- 若仍卡 → 查 display RGA+DRM present 吞吐
- 若需 RTSP 模式流畅 → 单独优化 MPP 解码/队列
