#include "frame_queue.h"
#include "log.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_run = 1;

static void on_sig(int sig) {
    (void)sig;
    g_run = 0;
}

static int find_start_code(const uint8_t *buf, int len, int from) {
    for (int i = from; i + 3 < len; ++i) {
        if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1) {
            return i;
        }
        if (i + 4 < len && buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 0 &&
            buf[i + 3] == 1) {
            return i;
        }
    }
    return -1;
}

static int start_code_len(const uint8_t *buf, int sc) {
    return (buf[sc + 2] == 1) ? 3 : 4;
}

static int h265_nal_type(const uint8_t *buf, int sc, int len) {
    int sc_len = start_code_len(buf, sc);
    if (sc + sc_len + 1 >= len) {
        return -1;
    }
    return (buf[sc + sc_len] & 0x7E) >> 1;
}

static int is_h265_vcl(int nal_type) {
    return nal_type >= 0 && nal_type <= 31;
}

static int push_access_unit(uint32_t ch, int *frame, const uint8_t *au, int au_len) {
    if (au_len < 256) {
        return 0;
    }
    FqFrameMeta meta = {0};
    meta.data_len = (uint32_t)au_len;
    meta.pts_us = (uint64_t)(*frame) * 40000;
    meta.is_key = ((*frame) == 0) ? 1 : 0;
    if (fq_preview_push(ch, &meta, au) != 0) {
        return 0;
    }
    ++(*frame);
    if (*frame <= 3 || *frame % 50 == 0) {
        LOGI("rtsp_channel ch=%u push seq=%d len=%u", ch, *frame, meta.data_len);
    }
    return 1;
}

static int run_gst_pull(uint32_t ch, const char *url, int *total_frames) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "gst-launch-1.0 -q rtspsrc location=%s latency=100 "
             "drop-on-latency=true ! rtph265depay ! h265parse ! "
             "video/x-h265,stream-format=byte-stream,alignment=au ! "
             "fdsink fd=1 sync=false",
             url);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        LOGE("popen gst failed ch=%u", ch);
        return -1;
    }

    uint8_t chunk[16384];
    uint8_t acc[FQ_VIDEO_SLOT_SIZE];
    uint8_t au[FQ_VIDEO_SLOT_SIZE];
    int acc_len = 0;
    int au_len = 0;
    int frame = 0;

    while (g_run) {
        size_t n = fread(chunk, 1, sizeof(chunk), fp);
        if (n == 0) {
            break;
        }
        if (acc_len + (int)n > (int)sizeof(acc)) {
            acc_len = 0;
            au_len = 0;
        }
        memcpy(acc + acc_len, chunk, n);
        acc_len += (int)n;

        int sc = find_start_code(acc, acc_len, 0);
        if (sc < 0) {
            continue;
        }
        if (sc > 0) {
            memmove(acc, acc + sc, (size_t)(acc_len - sc));
            acc_len -= sc;
        }

        while (1) {
            int next = find_start_code(acc, acc_len, 4);
            if (next < 0) {
                break;
            }
            int nal_type = h265_nal_type(acc, 0, next);
            if (au_len + next > (int)sizeof(au)) {
                au_len = 0;
            }
            memcpy(au + au_len, acc, (size_t)next);
            au_len += next;

            if (is_h265_vcl(nal_type)) {
                push_access_unit(ch, &frame, au, au_len);
                au_len = 0;
            }

            memmove(acc, acc + next, (size_t)(acc_len - next));
            acc_len -= next;
        }
    }
    pclose(fp);
    *total_frames = frame;
    return frame > 0 ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <channel> <rtsp_url>\n", argv[0]);
        return 1;
    }
    uint32_t ch = (uint32_t)atoi(argv[1]);
    const char *url = argv[2];
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    if (fq_preview_attach_channel(ch) < 0) {
        LOGE("preview video shm attach failed ch=%u", ch);
        return 1;
    }

    LOGI("rtsp_channel ch=%u url=%s", ch, url);

    int total = 0;
    while (g_run) {
        int batch = 0;
        run_gst_pull(ch, url, &batch);
        total += batch;
        if (!g_run) {
            break;
        }
        if (batch > 0) {
            LOGW("rtsp_channel ch=%u stream ended batch=%d total=%d, reconnect", ch, batch,
                 total);
        } else {
            LOGW("rtsp_channel ch=%u connect failed, retry", ch);
        }
        sleep(2);
    }

    fq_preview_pool_detach();
    LOGI("rtsp_channel ch=%u pushed %d frames total", ch, total);
    return total > 0 ? 0 : 1;
}
