# 2026-08-19 麦克风 AAC 本地回放

## 改动
- `frame_queue`：preview 音频 shm 池（0x5310）
- `encoder`：alsasrc → voaacenc ADTS，按帧 push + local audio tap
- `rtsp_server`：ShmAdtsLiveSource + MPEG4GenericRTPSink AAC 轨
- `player`：faad + autoaudiosink 回放线程
- `run_mic_verify_board.sh`：20s 验收

## 板端验收
```
audio_push=900 audio_playback=800
[mic] PASS: microphone AAC capture + playback
```

## 配置
- `publisher.ini`：`enableAudio=1`, `audioDevice=hw:0,0`
- `loopback.ini`：`enableAudio=1`
