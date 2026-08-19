# 2026-08-19 MIPI 流畅度小步优化（不动 display_rga 核心）

## 目标
在回退稳定版基础上减轻卡顿，不修改 `display_rga.c/h` 的 update/render 路径。

## 改动
| 项 | 前 | 后 | 作用 |
|----|----|----|------|
| `enableChnNum` | 2 | **1** | 1 路 gst + 1 路 MPP 解码，减 CPU 争抢 |
| `refreshMs` | 33 | **25** | 显示线程约 40Hz 轮询 |
| RTSP | latency=100 | **50 + tcp** | 降低拉流缓冲延迟 |
| `preview/main.c` | 双路 rtsp | **同 URL 只起 1 路** | 防误配双拉 |
| `player_main` | 固定 sleep | **无新帧 5ms / 有帧 refreshMs** | 更快跟上解码，不碰 RGA |

## 未改
- `display_rga.c/h`、`display_output.c/h` 核心逻辑

## 板端验收（解码修复后）
- decoded **1576** / push 2572（~17.5fps，原 ~1fps）
- 修复：`mpp_decoder_drain` + 按序 `pop`（`pop_latest` 破坏 HEVC 参考链）
- PASS

## 涉及文件
- `common/mpp_codec.c/h`
- `preview/player_main.c`
