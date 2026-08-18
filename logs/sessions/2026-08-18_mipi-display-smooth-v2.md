# 2026-08-18 MIPI 显示流畅度优化 v2

## 目标
对照 EASY-EAI `displayCurChn()`（40ms 事件刷新、单路 RGA+送显），降低 RTSP 回环预览的 CPU/内存拷贝与双路争抢。

## 根因（对照 EASY-EAI）
| EASY-EAI | 本工程（优化前） |
|----------|------------------|
| 解码线程写 Mat，display 40ms RGA+commit | 每帧 memcpy 1.3MB + 双次 RGA + 40ms 盲轮询 |
| 单路 RTSP 播放器 | 同 URL 双路 gst-launch + 双路 MPP 解码 |
| 采集可直显 | 本地预览仍走 encode→RTSP→decode |

## 改动
1. **零拷贝解码**：MPP 直接写入 `display_rga` 双缓冲写侧，`publish` 后唤醒 display
2. **事件驱动刷新**：`display_rga_wait_frame(seen_seq, refresh_ms)` 替代固定 sleep 轮询
3. **合并 RGA**：`display_output_present_src()` 一次 scale + NV12→BGRA 送 DRM
4. **单路拉流**：`loopback.ini enableChnNum=1`；同 URL 时 `preview_supervisor` 只起 1 个 `rtsp_channel`
5. **RTSP 降延迟**：`latency=50`、`protocols=tcp`
6. **refreshMs=33** 对齐 ~30fps 摄像头

## 涉及文件
- `common/display_rga.c/h`
- `common/display_output.c/h`
- `preview/player_main.c`
- `preview/main.c`
- `preview/rtsp_channel_main.c`
- `config/loopback.ini`
- `common/process_supervisor.c/h`（sup_add11）

## 板端验收
```bash
adb shell stop_all_board.sh
# 部署新二进制后
adb shell run_mipi_hw_demo_board.sh
adb shell "ps aux | grep -E 'rtsp_channel|player' | grep -v grep"
# 期望：1×rtsp_channel、1×player
adb shell "grep event-driven /tmp/preview.log"
```

## 若仍不够流畅（下一步）
- 本地 MIPI 预览 bypass RTSP（encoder 侧 NV12 tap → preview shm）
- 编码 GOP/码率下调（降低 decode 压力）
- `refreshMs=25` 试 40fps（需摄像头/编码帧率支持）
