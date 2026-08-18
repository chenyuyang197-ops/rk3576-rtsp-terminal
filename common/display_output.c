#include "display_output.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <rga/im2d.h>
#include <rga/im2d_buffer.h>
#include <rga/im2d_single.h>
#include <rga/rga.h>

#if defined(RK3576_HAVE_DRM)
#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

struct DisplayOutput {
    int width;
    int height;
    int enable_drm;
    int use_drm;
    char dump_path[128];
    uint8_t *scale_nv12;
#if defined(RK3576_HAVE_DRM)
    int drm_fd;
    uint32_t conn_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t dumb_handle;
    uint32_t pitch;
    size_t map_size;
    uint8_t *map;
    drmModeModeInfo mode;
#endif
};

static size_t nv12_bytes(int w, int h) {
    return (size_t)w * h * 3 / 2;
}

static int dump_nv12(const char *path, const uint8_t *nv12, int w, int h) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }
    fwrite(nv12, 1, nv12_bytes(w, h), fp);
    fclose(fp);
    return 0;
}

#if defined(RK3576_HAVE_DRM)
static int drm_pick_mode(drmModeConnector *conn, int want_w, int want_h,
                         drmModeModeInfo *out) {
    for (int i = 0; i < conn->count_modes; ++i) {
        if ((int)conn->modes[i].hdisplay == want_w &&
            (int)conn->modes[i].vdisplay == want_h) {
            *out = conn->modes[i];
            return 0;
        }
    }
    if (conn->count_modes > 0) {
        *out = conn->modes[0];
        LOGW("display_output: no exact %dx%d mode, use %dx%d", want_w, want_h,
             out->hdisplay, out->vdisplay);
        return 0;
    }
    return -1;
}

static int drm_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
                         uint32_t *crtc_id) {
    (void)res;
    if (conn->encoder_id) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
        if (enc) {
            *crtc_id = enc->crtc_id;
            drmModeFreeEncoder(enc);
            if (*crtc_id) {
                return 0;
            }
        }
    }
    for (int i = 0; i < conn->count_encoders; ++i) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[i]);
        if (!enc) {
            continue;
        }
        if (enc->crtc_id) {
            *crtc_id = enc->crtc_id;
            drmModeFreeEncoder(enc);
            return 0;
        }
        drmModeFreeEncoder(enc);
    }
    return -1;
}

static int drm_setup(DisplayOutput *o) {
    o->drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (o->drm_fd < 0) {
        LOGW("display_output: open /dev/dri/card0 failed: %s", strerror(errno));
        return -1;
    }

    drmModeRes *res = drmModeGetResources(o->drm_fd);
    if (!res) {
        return -1;
    }

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; ++i) {
        drmModeConnector *c = drmModeGetConnector(o->drm_fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            o->conn_id = c->connector_id;
            break;
        }
        if (c) {
            drmModeFreeConnector(c);
        }
    }
    if (!conn) {
        drmModeFreeResources(res);
        LOGW("display_output: no connected DRM connector");
        return -1;
    }

    if (drm_pick_mode(conn, o->width, o->height, &o->mode) < 0 ||
        drm_find_crtc(o->drm_fd, res, conn, &o->crtc_id) < 0) {
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return -1;
    }

    struct drm_mode_create_dumb create = {
        .width = (uint32_t)o->width,
        .height = (uint32_t)o->height,
        .bpp = 32,
    };
    if (drmIoctl(o->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return -1;
    }
    o->dumb_handle = create.handle;
    o->pitch = create.pitch;
    o->map_size = create.size;
    if (drmModeAddFB(o->drm_fd, o->width, o->height, 24, 32, o->pitch, o->dumb_handle,
                     &o->fb_id) < 0) {
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return -1;
    }

    struct drm_mode_map_dumb map = {.handle = o->dumb_handle};
    if (drmIoctl(o->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return -1;
    }
    o->map = mmap(NULL, o->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, o->drm_fd,
                  map.offset);
    if (o->map == MAP_FAILED) {
        o->map = NULL;
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return -1;
    }

    if (drmModeSetCrtc(o->drm_fd, o->crtc_id, o->fb_id, 0, 0, &o->conn_id, 1, &o->mode) < 0) {
        LOGW("display_output: drmModeSetCrtc failed");
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        return -1;
    }

    LOGI("display_output: DRM MIPI %dx%d fb=%u crtc=%u conn=%u", o->width, o->height,
         o->fb_id, o->crtc_id, o->conn_id);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return 0;
}

static int drm_blit_nv12(DisplayOutput *o, const uint8_t *nv12) {
    rga_buffer_t src =
        wrapbuffer_virtualaddr_t((void *)nv12, o->width, o->height, o->width, o->height,
                                 RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t dst = wrapbuffer_virtualaddr_t(
        (void *)o->map, o->width, o->height, (int)(o->pitch / 4), o->height,
        RK_FORMAT_RGBA_8888);
    return imcvtcolor_t(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_RGBA_8888,
                        IM_COLOR_SPACE_DEFAULT, 1) == IM_STATUS_SUCCESS
               ? 0
               : -1;
}

static int drm_blit_nv12_bgr(DisplayOutput *o, const uint8_t *nv12) {
    rga_buffer_t src =
        wrapbuffer_virtualaddr_t((void *)nv12, o->width, o->height, o->width, o->height,
                                 RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t dst = wrapbuffer_virtualaddr_t(
        (void *)o->map, o->width, o->height, (int)(o->pitch / 4), o->height,
        RK_FORMAT_BGRA_8888);
    return imcvtcolor_t(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGRA_8888,
                        IM_COLOR_SPACE_DEFAULT, 1) == IM_STATUS_SUCCESS
               ? 0
               : -1;
}

static void nv12_fill_black(uint8_t *nv12, int w, int h) {
    memset(nv12, 0, (size_t)w * h);
    memset(nv12 + (size_t)w * h, 128, (size_t)w * h / 2);
}

static int scale_nv12_to_disp(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh,
                              int stretch) {
    rga_buffer_t pat;
    im_rect prect = {0, 0, 0, 0};
    memset(&pat, 0, sizeof(pat));

    if (stretch) {
        rga_buffer_t s =
            wrapbuffer_virtualaddr_t((void *)src, sw, sh, sw, sh, RK_FORMAT_YCbCr_420_SP);
        rga_buffer_t d =
            wrapbuffer_virtualaddr_t((void *)dst, dw, dh, dw, dh, RK_FORMAT_YCbCr_420_SP);
        return imresize_t(s, d, 0, 0, INTER_LINEAR, 1) == IM_STATUS_SUCCESS ? 0 : -1;
    }

    float sx = (float)dw / (float)sw;
    float sy = (float)dh / (float)sh;
    float scale = sx < sy ? sx : sy;
    int tw = (int)((float)sw * scale) & ~15;
    int th = (int)((float)sh * scale);
    if (tw <= 0 || th <= 0) {
        return -1;
    }
    int ox = (dw - tw) / 2;
    int oy = (dh - th) / 2;

    nv12_fill_black(dst, dw, dh);

    rga_buffer_t s =
        wrapbuffer_virtualaddr_t((void *)src, sw, sh, sw, sh, RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t d =
        wrapbuffer_virtualaddr_t((void *)dst, dw, dh, dw, dh, RK_FORMAT_YCbCr_420_SP);
    im_rect src_rect = {0, 0, sw, sh};
    im_rect dst_rect = {ox, oy, tw, th};
    return improcess(s, d, pat, src_rect, dst_rect, prect, IM_SYNC) == IM_STATUS_SUCCESS ? 0
                                                                                          : -1;
}

static void drm_teardown(DisplayOutput *o) {
    if (o->map && o->map != MAP_FAILED) {
        munmap(o->map, o->map_size);
        o->map = NULL;
    }
    if (o->drm_fd >= 0) {
        if (o->fb_id) {
            drmModeRmFB(o->drm_fd, o->fb_id);
            o->fb_id = 0;
        }
        if (o->dumb_handle) {
            struct drm_mode_destroy_dumb destroy = {.handle = o->dumb_handle};
            drmIoctl(o->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
            o->dumb_handle = 0;
        }
        close(o->drm_fd);
        o->drm_fd = -1;
    }
}
#endif

DisplayOutput *display_output_create(int width, int height, int enable_drm) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }
    DisplayOutput *o = calloc(1, sizeof(DisplayOutput));
    if (!o) {
        return NULL;
    }
    o->width = width;
    o->height = height;
    o->enable_drm = enable_drm;
    o->scale_nv12 = malloc(nv12_bytes(width, height));
    if (!o->scale_nv12) {
        free(o);
        return NULL;
    }
#if defined(RK3576_HAVE_DRM)
    o->drm_fd = -1;
#endif
    snprintf(o->dump_path, sizeof(o->dump_path), "/tmp/display_%dx%d.yuv", width, height);

    if (enable_drm) {
#if defined(RK3576_HAVE_DRM)
        if (drm_setup(o) == 0) {
            o->use_drm = 1;
        } else {
            LOGW("display_output: DRM unavailable, fallback to %s", o->dump_path);
        }
#else
        LOGW("display_output: built without libdrm, fallback to %s", o->dump_path);
#endif
    } else {
        LOGI("display_output: dump-only mode -> %s", o->dump_path);
    }
    return o;
}

void display_output_destroy(DisplayOutput *o) {
    if (!o) {
        return;
    }
#if defined(RK3576_HAVE_DRM)
    drm_teardown(o);
#endif
    free(o->scale_nv12);
    free(o);
}

int display_output_present(DisplayOutput *o, const uint8_t *nv12) {
    if (!o || !nv12) {
        return -1;
    }
#if defined(RK3576_HAVE_DRM)
    if (o->use_drm) {
        return drm_blit_nv12_bgr(o, nv12);
    }
#endif
    return dump_nv12(o->dump_path, nv12, o->width, o->height);
}

int display_output_present_src(DisplayOutput *o, const uint8_t *src, int src_w, int src_h,
                               int stretch) {
    if (!o || !src || !o->scale_nv12 || src_w <= 0 || src_h <= 0) {
        return -1;
    }
    if (scale_nv12_to_disp(src, src_w, src_h, o->scale_nv12, o->width, o->height, stretch) < 0) {
        LOGW("display_output: RGA scale failed %dx%d -> %dx%d", src_w, src_h, o->width,
             o->height);
        return -1;
    }
    if (display_output_present(o, o->scale_nv12) < 0) {
        LOGW("display_output: RGA color convert failed");
        return -1;
    }
    return 0;
}

const char *display_output_dump_path(const DisplayOutput *o) {
    return o ? o->dump_path : NULL;
}

int display_output_width(const DisplayOutput *o) {
    return o ? o->width : 0;
}

int display_output_height(const DisplayOutput *o) {
    return o ? o->height : 0;
}

int display_output_using_drm(const DisplayOutput *o) {
    return o ? o->use_drm : 0;
}
