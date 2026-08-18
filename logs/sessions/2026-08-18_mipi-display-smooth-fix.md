# 2026-08-18 MIPI 显示卡顿优化

## 现象
letterbox 修复后画面比例正常，但仍感觉卡顿。

## 根因
1. **每 5 帧写 1.3MB YUV 到 /tmp**（双路）— 同步磁盘 I/O 阻塞解码线程
2. **双路 player 都在做 RGA letterbox** — RGA/CPU 争抢
3. **解码线程内做 RGA** — 与显示线程竞争，wrlock 阻塞
4. **队列堆积** — 处理旧帧而非最新帧，延迟感明显
5. **偶发多实例** — stop 不干净时多个 player/rtsp_channel 同时跑

## 修复
- `dumpEvery=0` 关闭运行时 dump
- 仅 **ch0** 喂显示（`display_channel=0`）
- RGA letterbox 移到 **display 线程**，有新帧才 present
- `fq_preview_pop_latest` 丢旧帧降延迟
- `refreshMs=33`，RTSP `latency=100`

## 验收
```bash
adb shell stop_all && run_mipi_hw_demo_board.sh
adb shell "grep -c imresize /tmp/preview.log"   # 0
adb shell "ps aux | grep player | grep -v grep | wc -l"  # 1
```
