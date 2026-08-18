# 2026-08-18 MIPI 绿屏日志分析

## 现象
显示比例正常后出现**绿屏**。

## 日志抓包结论

| 检查项 | 异常时 | 修复后 |
|--------|--------|--------|
| `/tmp/ch0.yuv` Y 均值 | **0**（全黑 NV12） | **121.3** |
| UV 均值 | **0** | **129.0**（正常） |
| pub.log | `skip oversize NAL fMaxSize≈7726` | `AU truncated`（28 次，server 稳定） |
| rtsp_server | 反复 exit/554 占用 | 单实例稳定 |
| player 解码 | 0 或无效 | **37+ 帧**，y0>0 |

## 根因链
1. RTSP `ShmNalFramedSource` **跳过超大 IDR NAL**（fMaxSize≈7KB）→ 码流缺关键帧
2. MPP 解码输出 **Y=0 UV=0** 全零 NV12
3. RGA `imcvtcolor` 把 (Y=0,U=0,V=0) 映成 **绿色** MIPI 画面

绿屏不是 DRM 格式问题，而是**解码输入无效**。

## 修复
- `rtsp_server_main.cpp`：整 AU 推送 + `OutPacketBuffer::increaseMaxSizeTo(16MB)`，避免 skip NAL
- `publisher/main.c`：rtsp_server 重启前 `sleep(2)` 避免 554 抢占
- `player_main.c`：dump 改用 `decode_dst`（publish 前），日志带 `y0=`

## 验收（摄像头模式）
```
Y mean 121.3 max 207 UV mean 129.0
rtsp_server 进程数=1
AU truncated=28（待优化，但不阻塞显示）
```

## 待办
- StreamFramer 内部 fMaxSize 仍偏小，大 I 帧会 AU truncated；长期应换 **DiscreteFramer + 裸 NAL**（板端曾 exit，需 gdb）
- 或降低编码 I 帧大小（GOP/码率）
