#include "display_rga.h"

#include "log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <rga/im2d.h>
#include <rga/im2d_buffer.h>
#include <rga/im2d_single.h>
#include <rga/im2d_type.h>
#include <rga/rga.h>

typedef struct {
    uint8_t *src_nv12;
    uint8_t *disp_nv12;
    pthread_mutex_t lock;
    uint64_t seq;
    uint64_t rendered_seq;
    int ready;
    int cleared;
} DisplayChannel;

struct DisplayRga {
    int channel_count;
    int src_w;
    int src_h;
    int disp_w;
    int disp_h;
    int stretch;
    int active;
    uint8_t *compose_buf;
    DisplayChannel *channels;
};

static size_t nv12_bytes(int w, int h) {
    return (size_t)w * h * 3 / 2;
}

static void nv12_fill_black(uint8_t *buf, int w, int h) {
    size_t y_size = (size_t)w * h;
    memset(buf, 16, y_size);
    memset(buf + y_size, 128, y_size / 2);
}

static int rga_blit_nv12(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh,
                         int stretch, int *cleared) {
    rga_buffer_t src_buf =
        wrapbuffer_virtualaddr_t((void *)src, sw, sh, sw, sh, RK_FORMAT_YCbCr_420_SP);

    if (sw == dw && sh == dh) {
        rga_buffer_t dst_buf =
            wrapbuffer_virtualaddr_t((void *)dst, dw, dh, dw, dh, RK_FORMAT_YCbCr_420_SP);
        return imcopy_t(src_buf, dst_buf, 1) == IM_STATUS_SUCCESS ? 0 : -1;
    }
    if (!stretch && sw == dh && sh == dw) {
        rga_buffer_t dst_buf =
            wrapbuffer_virtualaddr_t((void *)dst, dw, dh, dw, dh, RK_FORMAT_YCbCr_420_SP);
        return imrotate_t(src_buf, dst_buf, IM_HAL_TRANSFORM_ROT_90, 1) == IM_STATUS_SUCCESS
                   ? 0
                   : -1;
    }

    int nw = dw;
    int nh = dh;
    int ox = 0;
    int oy = 0;
    if (!stretch) {
        double sx = (double)dw / sw;
        double sy = (double)dh / sh;
        double s = sx < sy ? sx : sy;
        nw = ((int)(sw * s)) & ~15;
        nh = ((int)(sh * s)) & ~15;
        if (nw < 16 || nh < 16) {
            return -1;
        }
        ox = ((dw - nw) / 2) & ~1;
        oy = ((dh - nh) / 2) & ~1;
    }

    if (!cleared || !*cleared) {
        nv12_fill_black(dst, dw, dh);
        if (cleared) {
            *cleared = 1;
        }
    }

    rga_buffer_t dst_buf =
        wrapbuffer_virtualaddr_t((void *)dst, dw, dh, dw, dh, RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t pat = {0};
    im_rect srect = {0, 0, sw, sh};
    im_rect drect = {ox, oy, nw, nh};
    im_rect prect = {0, 0, 0, 0};
    return improcess(src_buf, dst_buf, pat, srect, drect, prect, IM_SYNC) == IM_STATUS_SUCCESS
               ? 0
               : -1;
}

DisplayRga *display_rga_create(int channel_count, int src_w, int src_h, int disp_w, int disp_h,
                               int stretch) {
    if (channel_count <= 0 || channel_count > 8) {
        return NULL;
    }
    DisplayRga *d = calloc(1, sizeof(DisplayRga));
    if (!d) {
        return NULL;
    }
    d->channel_count = channel_count;
    d->src_w = src_w;
    d->src_h = src_h;
    d->disp_w = disp_w;
    d->disp_h = disp_h;
    d->stretch = stretch ? 1 : 0;
    d->active = 0;
    d->compose_buf = malloc(nv12_bytes(disp_w, disp_h));
    d->channels = calloc((size_t)channel_count, sizeof(DisplayChannel));
    if (!d->compose_buf || !d->channels) {
        display_rga_destroy(d);
        return NULL;
    }
    for (int i = 0; i < channel_count; ++i) {
        d->channels[i].src_nv12 = malloc(nv12_bytes(src_w, src_h));
        d->channels[i].disp_nv12 = malloc(nv12_bytes(disp_w, disp_h));
        pthread_mutex_init(&d->channels[i].lock, NULL);
        if (!d->channels[i].src_nv12 || !d->channels[i].disp_nv12) {
            display_rga_destroy(d);
            return NULL;
        }
    }
    LOGI("display_rga: ch=%d src=%dx%d disp=%dx%d mode=%s", channel_count, src_w, src_h,
         disp_w, disp_h, stretch ? "stretch(EASY-EAI)" : "letterbox");
    return d;
}

void display_rga_destroy(DisplayRga *d) {
    if (!d) {
        return;
    }
    if (d->channels) {
        for (int i = 0; i < d->channel_count; ++i) {
            free(d->channels[i].src_nv12);
            free(d->channels[i].disp_nv12);
            pthread_mutex_destroy(&d->channels[i].lock);
        }
        free(d->channels);
    }
    free(d->compose_buf);
    free(d);
}

uint8_t *display_rga_compose_buf(DisplayRga *d) {
    return d ? d->compose_buf : NULL;
}

int display_rga_update_channel(DisplayRga *d, int channel, const uint8_t *src_nv12) {
    if (!d || channel < 0 || channel >= d->channel_count || !src_nv12) {
        return -1;
    }
    DisplayChannel *c = &d->channels[channel];
    pthread_mutex_lock(&c->lock);
    memcpy(c->src_nv12, src_nv12, nv12_bytes(d->src_w, d->src_h));
    ++c->seq;
    c->ready = 1;
    pthread_mutex_unlock(&c->lock);
    return 0;
}

int display_rga_set_active(DisplayRga *d, int channel) {
    if (!d || channel < 0 || channel >= d->channel_count) {
        return -1;
    }
    d->active = channel;
    return 0;
}

int display_rga_render_active(DisplayRga *d, uint8_t *disp_nv12) {
    if (!d || !disp_nv12) {
        return -1;
    }
    DisplayChannel *c = &d->channels[d->active];
    pthread_mutex_lock(&c->lock);
    if (!c->ready || c->rendered_seq == c->seq) {
        pthread_mutex_unlock(&c->lock);
        return 1;
    }
    int blit_rc = rga_blit_nv12(c->src_nv12, d->src_w, d->src_h, c->disp_nv12, d->disp_w,
                                d->disp_h, d->stretch, &c->cleared);
    if (blit_rc != 0) {
        static int warn_count;
        if (warn_count < 5) {
            LOGW("display_rga ch=%d RGA blit failed", d->active);
            ++warn_count;
        }
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    c->rendered_seq = c->seq;
    memcpy(disp_nv12, c->disp_nv12, nv12_bytes(d->disp_w, d->disp_h));
    pthread_mutex_unlock(&c->lock);
    return 0;
}

int display_rga_dump_active(DisplayRga *d, const char *path) {
    if (!d || !path) {
        return -1;
    }
    if (display_rga_render_active(d, d->compose_buf) != 0) {
        return -1;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }
    fwrite(d->compose_buf, 1, nv12_bytes(d->disp_w, d->disp_h), fp);
    fclose(fp);
    return 0;
}
