#include "frame_queue.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

typedef struct {
    uint32_t slot_size;
    uint32_t slot_count;
    uint32_t write_idx;
    uint32_t read_idx;
    uint32_t count;
} FqRingHeader;

typedef struct {
    FqRingHeader hdr;
    uint8_t slots[];
} FqRing;

typedef struct {
    int shmid;
    FqRing *ring;
    uint32_t slot_size;
    uint32_t slot_count;
} FqChannelCtx;

static FqChannelCtx g_video[FQ_MAX_CHANNELS];
static FqChannelCtx g_audio[FQ_MAX_CHANNELS];
static uint32_t g_video_channels;
static uint32_t g_audio_channels;

static FqChannelCtx g_preview_video[FQ_MAX_CHANNELS];
static uint32_t g_preview_video_channels;

static FqChannelCtx g_preview_audio[FQ_MAX_CHANNELS];
static uint32_t g_preview_audio_channels;

static key_t fq_key(FqMediaType type, uint32_t channel) {
    return (key_t)(FQ_SHM_KEY_BASE + (type * 16) + channel);
}

static key_t fq_preview_key(uint32_t channel) {
    return (key_t)(FQ_SHM_PREVIEW_KEY_BASE + channel);
}

static key_t fq_preview_audio_key(uint32_t channel) {
    return (key_t)(FQ_SHM_PREVIEW_AUDIO_KEY_BASE + channel);
}

static size_t fq_shm_size(uint32_t slot_size, uint32_t slot_count) {
    return sizeof(FqRingHeader) + (size_t)slot_size * slot_count;
}

static int fq_open_channel_key(FqChannelCtx *ctx, key_t key, uint32_t slot_size,
                               uint32_t slot_count, int create);

static int fq_open_channel(FqChannelCtx *ctx, FqMediaType type, uint32_t channel,
                           uint32_t slot_size, uint32_t slot_count, int create) {
    key_t key = fq_key(type, channel);
    return fq_open_channel_key(ctx, key, slot_size, slot_count, create);
}

static int fq_open_channel_key(FqChannelCtx *ctx, key_t key, uint32_t slot_size,
                               uint32_t slot_count, int create) {
    size_t size = fq_shm_size(slot_size, slot_count);
    int flags = create ? (IPC_CREAT | IPC_EXCL | 0666) : 0666;
    int shmid = shmget(key, size, flags);
    if (shmid < 0 && create && errno == EEXIST) {
        /* 旧段尺寸可能不同（如 slot 从 512KB 扩到 1536KB），须先按 key 删除 */
        int old = shmget(key, 0, 0666);
        if (old >= 0) {
            shmctl(old, IPC_RMID, NULL);
        }
        shmid = shmget(key, size, IPC_CREAT | IPC_EXCL | 0666);
    }
    if (shmid < 0) {
        fprintf(stderr, "fq shmget key=0x%x size=%zu failed: %s\n", (unsigned)key, size,
                strerror(errno));
        return -1;
    }
    void *addr = shmat(shmid, NULL, 0);
    if (addr == (void *)-1) {
        return -1;
    }
    ctx->shmid = shmid;
    ctx->ring = (FqRing *)addr;
    ctx->slot_size = slot_size;
    ctx->slot_count = slot_count;
    if (create) {
        memset(ctx->ring, 0, size);
        ctx->ring->hdr.slot_size = slot_size;
        ctx->ring->hdr.slot_count = slot_count;
    }
    return 0;
}

static FqChannelCtx *fq_ctx(FqMediaType type, uint32_t channel) {
    if (channel >= FQ_MAX_CHANNELS) {
        return NULL;
    }
    return (type == FQ_TYPE_VIDEO) ? &g_video[channel] : &g_audio[channel];
}

int fq_pool_create(FqMediaType type, uint32_t channel_num) {
    uint32_t slot_size =
        (type == FQ_TYPE_VIDEO) ? FQ_VIDEO_SLOT_SIZE : FQ_AUDIO_SLOT_SIZE;
    uint32_t slot_count =
        (type == FQ_TYPE_VIDEO) ? FQ_VIDEO_SLOTS : FQ_AUDIO_SLOTS;
    uint32_t *n = (type == FQ_TYPE_VIDEO) ? &g_video_channels : &g_audio_channels;
    *n = channel_num;
    for (uint32_t i = 0; i < channel_num; ++i) {
        if (fq_open_channel(fq_ctx(type, i), type, i, slot_size, slot_count, 1) < 0) {
            return -1;
        }
    }
    return 0;
}

int fq_pool_attach(FqMediaType type, uint32_t channel_num) {
    uint32_t slot_size =
        (type == FQ_TYPE_VIDEO) ? FQ_VIDEO_SLOT_SIZE : FQ_AUDIO_SLOT_SIZE;
    uint32_t slot_count =
        (type == FQ_TYPE_VIDEO) ? FQ_VIDEO_SLOTS : FQ_AUDIO_SLOTS;
    uint32_t *n = (type == FQ_TYPE_VIDEO) ? &g_video_channels : &g_audio_channels;
    *n = channel_num;
    for (uint32_t i = 0; i < channel_num; ++i) {
        if (fq_open_channel(fq_ctx(type, i), type, i, slot_size, slot_count, 0) < 0) {
            return -1;
        }
    }
    return 0;
}

void fq_pool_detach(FqMediaType type) {
    uint32_t n = (type == FQ_TYPE_VIDEO) ? g_video_channels : g_audio_channels;
    for (uint32_t i = 0; i < n; ++i) {
        FqChannelCtx *ctx = fq_ctx(type, i);
        if (ctx && ctx->ring) {
            shmdt(ctx->ring);
            ctx->ring = NULL;
        }
    }
}

static int fq_push_ring(FqRing *ring, uint32_t slot_size, uint32_t slot_count,
                        const FqFrameMeta *meta, const uint8_t *data) {
    if (meta->data_len + sizeof(FqFrameMeta) > slot_size) {
        return -1;
    }
    if (ring->hdr.count >= slot_count) {
        ring->hdr.read_idx = (ring->hdr.read_idx + 1) % slot_count;
        ring->hdr.count--;
    }
    uint32_t idx = ring->hdr.write_idx;
    uint8_t *slot = ring->slots + (size_t)idx * slot_size;
    memcpy(slot, meta, sizeof(FqFrameMeta));
    memcpy(slot + sizeof(FqFrameMeta), data, meta->data_len);
    ring->hdr.write_idx = (idx + 1) % slot_count;
    ring->hdr.count++;
    return 0;
}

static int fq_pop_ring(FqRing *ring, uint32_t slot_size, uint32_t slot_count,
                       FqFrameMeta *meta, uint8_t *data, uint32_t data_cap) {
    if (ring->hdr.count == 0) {
        return 1;
    }
    uint32_t idx = ring->hdr.read_idx;
    uint8_t *slot = ring->slots + (size_t)idx * slot_size;
    FqFrameMeta tmp;
    memcpy(&tmp, slot, sizeof(tmp));
    if (tmp.data_len > data_cap) {
        return -1;
    }
    memcpy(meta, &tmp, sizeof(tmp));
    memcpy(data, slot + sizeof(FqFrameMeta), tmp.data_len);
    ring->hdr.read_idx = (idx + 1) % slot_count;
    ring->hdr.count--;
    return 0;
}

int fq_push(uint32_t channel, FqMediaType type, const FqFrameMeta *meta,
            const uint8_t *data) {
    FqChannelCtx *ctx = fq_ctx(type, channel);
    if (!ctx || !ctx->ring) {
        return -1;
    }
    return fq_push_ring(ctx->ring, ctx->slot_size, ctx->slot_count, meta, data);
}

int fq_pop(uint32_t channel, FqMediaType type, FqFrameMeta *meta, uint8_t *data,
           uint32_t data_cap) {
    FqChannelCtx *ctx = fq_ctx(type, channel);
    if (!ctx || !ctx->ring) {
        return -1;
    }
    return fq_pop_ring(ctx->ring, ctx->slot_size, ctx->slot_count, meta, data,
                       data_cap);
}

bool fq_empty(uint32_t channel, FqMediaType type) {
    FqChannelCtx *ctx = fq_ctx(type, channel);
    return !ctx || !ctx->ring || ctx->ring->hdr.count == 0;
}

int fq_flush(uint32_t channel, FqMediaType type) {
    FqChannelCtx *ctx = fq_ctx(type, channel);
    if (!ctx || !ctx->ring) {
        return -1;
    }
    ctx->ring->hdr.read_idx = ctx->ring->hdr.write_idx;
    ctx->ring->hdr.count = 0;
    return 0;
}

uint32_t fq_depth(uint32_t channel, FqMediaType type) {
    FqChannelCtx *ctx = fq_ctx(type, channel);
    if (!ctx || !ctx->ring) {
        return 0;
    }
    return ctx->ring->hdr.count;
}

static int fq_preview_open(uint32_t channel, int create) {
    if (channel >= FQ_MAX_CHANNELS) {
        return -1;
    }
    return fq_open_channel_key(&g_preview_video[channel], fq_preview_key(channel),
                               FQ_VIDEO_SLOT_SIZE, FQ_VIDEO_SLOTS, create);
}

int fq_preview_pool_create(uint32_t channel_num) {
    g_preview_video_channels = channel_num;
    for (uint32_t i = 0; i < channel_num; ++i) {
        if (fq_preview_open(i, 1) < 0) {
            return -1;
        }
    }
    return 0;
}

int fq_preview_pool_attach(uint32_t channel_num) {
    g_preview_video_channels = channel_num;
    for (uint32_t i = 0; i < channel_num; ++i) {
        if (fq_preview_open(i, 0) < 0) {
            return -1;
        }
    }
    return 0;
}

int fq_preview_attach_channel(uint32_t channel) {
    if (channel >= FQ_MAX_CHANNELS) {
        return -1;
    }
    if (channel + 1 > g_preview_video_channels) {
        g_preview_video_channels = channel + 1;
    }
    return fq_preview_open(channel, 0);
}

void fq_preview_pool_detach(void) {
    for (uint32_t i = 0; i < g_preview_video_channels; ++i) {
        if (g_preview_video[i].ring) {
            shmdt(g_preview_video[i].ring);
            g_preview_video[i].ring = NULL;
        }
    }
}

int fq_preview_push(uint32_t channel, const FqFrameMeta *meta, const uint8_t *data) {
    if (channel >= g_preview_video_channels || !g_preview_video[channel].ring) {
        return -1;
    }
    FqChannelCtx *ctx = &g_preview_video[channel];
    return fq_push_ring(ctx->ring, ctx->slot_size, ctx->slot_count, meta, data);
}

int fq_preview_pop(uint32_t channel, FqFrameMeta *meta, uint8_t *data, uint32_t data_cap) {
    if (channel >= g_preview_video_channels || !g_preview_video[channel].ring) {
        return -1;
    }
    FqChannelCtx *ctx = &g_preview_video[channel];
    return fq_pop_ring(ctx->ring, ctx->slot_size, ctx->slot_count, meta, data, data_cap);
}

int fq_preview_pop_latest(uint32_t channel, FqFrameMeta *meta, uint8_t *data,
                          uint32_t data_cap) {
    while (fq_preview_depth(channel) > 1) {
        (void)fq_preview_pop(channel, meta, data, data_cap);
    }
    return fq_preview_pop(channel, meta, data, data_cap);
}

int fq_preview_flush(uint32_t channel) {
    if (channel >= g_preview_video_channels || !g_preview_video[channel].ring) {
        return -1;
    }
    FqChannelCtx *ctx = &g_preview_video[channel];
    ctx->ring->hdr.read_idx = ctx->ring->hdr.write_idx;
    ctx->ring->hdr.count = 0;
    return 0;
}

uint32_t fq_preview_depth(uint32_t channel) {
    if (channel >= g_preview_video_channels || !g_preview_video[channel].ring) {
        return 0;
    }
    return g_preview_video[channel].ring->hdr.count;
}

static int fq_preview_audio_open(uint32_t channel, int create) {
    if (channel >= FQ_MAX_CHANNELS) {
        return -1;
    }
    return fq_open_channel_key(&g_preview_audio[channel], fq_preview_audio_key(channel),
                               FQ_AUDIO_SLOT_SIZE, FQ_AUDIO_SLOTS, create);
}

int fq_preview_audio_pool_create(uint32_t channel_num) {
    g_preview_audio_channels = channel_num;
    for (uint32_t i = 0; i < channel_num; ++i) {
        if (fq_preview_audio_open(i, 1) < 0) {
            return -1;
        }
    }
    return 0;
}

int fq_preview_audio_pool_attach(uint32_t channel_num) {
    g_preview_audio_channels = channel_num;
    for (uint32_t i = 0; i < channel_num; ++i) {
        if (fq_preview_audio_open(i, 0) < 0) {
            return -1;
        }
    }
    return 0;
}

int fq_preview_audio_attach_channel(uint32_t channel) {
    if (channel >= FQ_MAX_CHANNELS) {
        return -1;
    }
    if (channel + 1 > g_preview_audio_channels) {
        g_preview_audio_channels = channel + 1;
    }
    return fq_preview_audio_open(channel, 0);
}

void fq_preview_audio_pool_detach(void) {
    for (uint32_t i = 0; i < g_preview_audio_channels; ++i) {
        if (g_preview_audio[i].ring) {
            shmdt(g_preview_audio[i].ring);
            g_preview_audio[i].ring = NULL;
        }
    }
}

int fq_preview_audio_push(uint32_t channel, const FqFrameMeta *meta, const uint8_t *data) {
    if (channel >= g_preview_audio_channels || !g_preview_audio[channel].ring) {
        return -1;
    }
    FqChannelCtx *ctx = &g_preview_audio[channel];
    return fq_push_ring(ctx->ring, ctx->slot_size, ctx->slot_count, meta, data);
}

int fq_preview_audio_pop(uint32_t channel, FqFrameMeta *meta, uint8_t *data, uint32_t data_cap) {
    if (channel >= g_preview_audio_channels || !g_preview_audio[channel].ring) {
        return -1;
    }
    FqChannelCtx *ctx = &g_preview_audio[channel];
    return fq_pop_ring(ctx->ring, ctx->slot_size, ctx->slot_count, meta, data, data_cap);
}

uint32_t fq_preview_audio_depth(uint32_t channel) {
    if (channel >= g_preview_audio_channels || !g_preview_audio[channel].ring) {
        return 0;
    }
    return g_preview_audio[channel].ring->hdr.count;
}
