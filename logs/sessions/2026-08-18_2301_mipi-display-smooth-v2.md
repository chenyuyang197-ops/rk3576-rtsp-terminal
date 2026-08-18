# 2026-08-18 部署 + MIPI demo 验收

## 目标
部署流畅度优化 v2 并在板端跑 MIPI 全链路 demo。

## 命令
```bash
bash scripts/deploy.sh          # 首次编译失败 display_output improcess_t
# 修复后 push + board make + board_install_bins.sh
bash scripts/run_mipi_hw_demo_board.sh  # 90s 预览
# 补充：stdbuf 35s 拉流验收
```

## 结果

| 项 | 结果 |
|----|------|
| 板端编译 | ✅ player/preview_supervisor/rtsp_channel 全部通过 |
| 摄像头编码 | ✅ pub.log push_max≈4650（90s） |
| RTSP :554 | ✅ 0.0.0.0:554 IPv4 |
| 单路拉流 | ✅ 1×rtsp_channel + 1×player |
| DRM MIPI | ✅ `display_output: DRM MIPI 480x800` |
| 零拷贝路径 | ✅ `zero-copy decode, event refresh` |
| 35s 拉流 | ✅ rtsp push seq≈750（~21fps） |

## 编译修复
`display_output.c`：`improcess_t`/`{}` 为 C++ 语法，改为 `imresize_t` + `improcess()` C API。

## 备注
- `preview.log` 子进程 stdout 全缓冲，demo 脚本 grep `drm_mipi=0` 为误报；杀 player 后可见 DRM 日志。
- 解码帧数仅在 player 退出时打印 `decoded N frames`，运行中不可见。

## 下一步
- 目视 MIPI 屏流畅度
- 可选：`loopback.ini` 改 `rtsp://127.0.0.1:554/live` 减少 RST 重连
- 可选：preview_supervisor 子进程 `stdbuf -oL` 或 setvbuf 行缓冲
