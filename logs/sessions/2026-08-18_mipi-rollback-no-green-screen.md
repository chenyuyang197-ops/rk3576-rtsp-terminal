# 2026-08-18 MIPI 回退至「显示正常、无绿屏」版本

## 目标
用户反馈绿屏，要求回退到此前「显示正常了，但画面依旧不流畅」时的代码状态（保留 270° 旋转 + stretch，撤销流畅度 v2 零拷贝改动）。

## 回退内容
| 模块 | 撤销 | 保留 |
|------|------|------|
| `display_rga.c/h` | 双缓冲零拷贝、`acquire_write`/`publish`/`wait_frame` | `update_channel` + `render_active` + stretch |
| `player_main.c` | MPP 直写 display 缓冲、事件驱动 `present_src` | `pop_latest`、仅 ch0 喂显示、固定 `refreshMs` 轮询 |
| `loopback.ini` | `enableChnNum=1` | `enableChnNum=2`、`displayStretch=1`、`rotateSeconds=0` |
| `preview/main.c` | 同 URL 只起 1 路 rtsp | `sup_add11` stretch 参数 |
| `rtsp_channel_main.c` | `latency=50 protocols=tcp` | `latency=100` |
| `rtsp_server_main.cpp` | （未改）整 AU + 16MB 缓冲，避免 skip NAL 绿屏 |

## 涉及文件
- `common/display_rga.c/h`
- `preview/player_main.c`
- `preview/main.c`
- `preview/rtsp_channel_main.c`
- `config/loopback.ini`

## 板端验收命令
```bash
source scripts/adb_env.sh
adb shell stop_all_board.sh
bash scripts/deploy.sh   # 或仅 push 上述文件后 build-board make player preview_supervisor
adb shell run_mipi_hw_demo_board.sh
adb shell "python3 -c \"import os; d=open('/tmp/ch0.yuv','rb').read(720*1280); print('y_mean', sum(d)/len(d))\""
# 期望 y_mean > 50，目视无绿屏
```

## 流畅度后续（稳定后再做，小步迭代）
对照 EASY-EAI `displayCurChn` 40ms：
1. 保持当前架构，仅调 `refreshMs=25~33`、`dumpEvery=0`
2. 同 URL 时可只起 1 路 `rtsp_channel`（减 CPU）
3. 长期：encoder NV12 tap 本地预览，bypass RTSP 回环

## 板端验收（2026-08-19 回退后实测）
```bash
adb shell /home/lckfb/rk3576-rtsp-terminal/scripts/run_mipi_verify_rollback.sh
```
结果：
- RTSP ready 4s
- ch0/ch1 yuv = 1382400 bytes
- **y_mean=120.5, y0=188**（非绿屏）
- drm_mipi_lines=1
- player ch0 decoded 55 frames (display=1)
- **PASS**

## 环境
- adb 设备 e621a57dd90902b3 已连通
