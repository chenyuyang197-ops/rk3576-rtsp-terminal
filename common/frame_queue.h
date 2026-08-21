#ifndef RK3576_FRAME_QUEUE_H
#define RK3576_FRAME_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FQ_MAX_CHANNELS 8
#define FQ_VIDEO_SLOT_SIZE (1536 * 1024)
#define FQ_AUDIO_SLOT_SIZE (8 * 1024)
#define FQ_VIDEO_SLOTS 16
#define FQ_AUDIO_SLOTS 32
#define FQ_SHM_KEY_BASE 0x5200
#define FQ_SHM_PREVIEW_KEY_BASE 0x5300
#define FQ_SHM_PREVIEW_AUDIO_KEY_BASE 0x5310

typedef enum {
    FQ_TYPE_VIDEO = 0,
    FQ_TYPE_AUDIO = 1
} FqMediaType;

typedef struct {
    uint32_t data_len;
    uint64_t pts_us;
    uint8_t is_key;
    uint8_t fmt; /* 0=H265 AU, 1=NV12 raw（本地 MIPI 旁路） */
    uint8_t reserved[6];
} FqFrameMeta;

#define FQ_FMT_H265 0
#define FQ_FMT_NV12 1

int fq_pool_create(FqMediaType type, uint32_t channel_num);
int fq_pool_attach(FqMediaType type, uint32_t channel_num);
void fq_pool_detach(FqMediaType type);

int fq_push(uint32_t channel, FqMediaType type, const FqFrameMeta *meta,
            const uint8_t *data);
int fq_pop(uint32_t channel, FqMediaType type, FqFrameMeta *meta,
           uint8_t *data, uint32_t data_cap);
bool fq_empty(uint32_t channel, FqMediaType type);
int fq_flush(uint32_t channel, FqMediaType type);
uint32_t fq_depth(uint32_t channel, FqMediaType type);

/* 预览端独立 shm 池（与发布端 encoder/rtsp_server 隔离） */
int fq_preview_pool_create(uint32_t channel_num);
int fq_preview_pool_attach(uint32_t channel_num);
int fq_preview_attach_channel(uint32_t channel);
void fq_preview_pool_detach(void);
int fq_preview_push(uint32_t channel, const FqFrameMeta *meta, const uint8_t *data);
int fq_preview_pop(uint32_t channel, FqFrameMeta *meta, uint8_t *data, uint32_t data_cap);
/* 丢弃队列中旧帧，只保留最新一帧（降低显示延迟与卡顿） */
int fq_preview_pop_latest(uint32_t channel, FqFrameMeta *meta, uint8_t *data,
                          uint32_t data_cap);
int fq_preview_flush(uint32_t channel);
uint32_t fq_preview_depth(uint32_t channel);

/* 预览端音频 shm（麦克风 AAC-ADTS 本地旁路） */
int fq_preview_audio_pool_create(uint32_t channel_num);
int fq_preview_audio_pool_attach(uint32_t channel_num);
int fq_preview_audio_attach_channel(uint32_t channel);
void fq_preview_audio_pool_detach(void);
int fq_preview_audio_push(uint32_t channel, const FqFrameMeta *meta, const uint8_t *data);
int fq_preview_audio_pop(uint32_t channel, FqFrameMeta *meta, uint8_t *data, uint32_t data_cap);
uint32_t fq_preview_audio_depth(uint32_t channel);

#ifdef __cplusplus
}
#endif

#endif
