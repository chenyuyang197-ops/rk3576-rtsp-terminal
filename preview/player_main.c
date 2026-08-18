#include "display_output.h"
#include "display_rga.h"
#include "frame_queue.h"
#include "log.h"
#include "mpp_codec.h"

#include <pthread.h>
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

typedef struct {
    uint32_t channel;
    int width;
    int height;
    int dump_every;
    int display_channel;
    DisplayRga *display;
} PlayerArgs;

typedef struct {
    DisplayRga *display;
    DisplayOutput *output;
    int channel_count;
    int rotate_sec;
    int refresh_ms;
    int disp_w;
    int disp_h;
} DisplayThreadArgs;

static int au_decodable(int len) {
    return len >= 256;
}

static void *player_thread(void *arg) {
    PlayerArgs *a = (PlayerArgs *)arg;
    uint8_t *bitstream = (uint8_t *)malloc(FQ_VIDEO_SLOT_SIZE);
    uint8_t *nv12 = (uint8_t *)malloc((size_t)a->width * a->height * 3 / 2);
    MppDecoder *dec = mpp_decoder_create_hevc(a->width, a->height);
    char out_path[128];
    int frame = 0;
    int nv12_cap = a->width * a->height * 3 / 2;
    int feeds_display = ((int)a->channel == a->display_channel);

    if (!bitstream || !nv12 || !dec) {
        LOGE("player ch=%u init failed", a->channel);
        free(bitstream);
        free(nv12);
        mpp_decoder_destroy(dec);
        return NULL;
    }

    while (g_run) {
        FqFrameMeta meta;
        int rc;
        if (feeds_display) {
            rc = fq_preview_pop_latest(a->channel, &meta, bitstream, FQ_VIDEO_SLOT_SIZE);
        } else {
            rc = fq_preview_pop(a->channel, &meta, bitstream, FQ_VIDEO_SLOT_SIZE);
        }
        if (rc == 1) {
            usleep(2000);
            continue;
        }
        if (rc < 0) {
            break;
        }
        if (!au_decodable((int)meta.data_len)) {
            continue;
        }
        rc = mpp_decoder_decode(dec, bitstream, (int)meta.data_len, nv12, nv12_cap);
        if (rc == 1) {
            continue;
        }
        if (rc < 0) {
            continue;
        }
        ++frame;
        if (feeds_display) {
            display_rga_update_channel(a->display, (int)a->channel, nv12);
        }
        if (a->dump_every > 0 && frame % a->dump_every == 0) {
            snprintf(out_path, sizeof(out_path), "/tmp/ch%u.yuv", a->channel);
            FILE *fp = fopen(out_path, "wb");
            if (fp) {
                fwrite(nv12, 1, (size_t)a->width * a->height * 3 / 2, fp);
                fclose(fp);
                LOGI("dump %s frame=%d pts=%llu y0=%u", out_path, frame,
                     (unsigned long long)meta.pts_us, nv12[0]);
            }
        }
    }
    mpp_decoder_destroy(dec);
    free(bitstream);
    free(nv12);
    LOGI("player ch=%u decoded %d frames display=%d", a->channel, frame, feeds_display);
    return NULL;
}

static void *display_thread(void *arg) {
    DisplayThreadArgs *a = (DisplayThreadArgs *)arg;
    uint8_t *buf = display_rga_compose_buf(a->display);
    if (!buf) {
        return NULL;
    }
    int active = 0;
    int tick = 0;
    while (g_run) {
        if (a->rotate_sec > 0 && a->channel_count > 1 && tick > 0 &&
            tick % (a->rotate_sec * 1000 / a->refresh_ms) == 0) {
            active = (active + 1) % a->channel_count;
            display_rga_set_active(a->display, active);
            LOGI("rotate active ch=%d", active);
        }
        if (display_rga_render_active(a->display, buf) == 0) {
            display_output_present(a->output, buf);
        }
        ++tick;
        usleep((useconds_t)a->refresh_ms * 1000);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <channel_count> [src_w] [src_h] [dump_every] [rotate_sec] "
                "[refresh_ms] [disp_w] [disp_h] [enable_drm] [display_channel] [stretch]\n",
                argv[0]);
        return 1;
    }
    int chn = atoi(argv[1]);
    int width = (argc > 2) ? atoi(argv[2]) : 720;
    int height = (argc > 3) ? atoi(argv[3]) : 1280;
    int dump_every = (argc > 4) ? atoi(argv[4]) : 0;
    int rotate_sec = (argc > 5) ? atoi(argv[5]) : 5;
    int refresh_ms = (argc > 6) ? atoi(argv[6]) : 33;
    int disp_w = (argc > 7) ? atoi(argv[7]) : 480;
    int disp_h = (argc > 8) ? atoi(argv[8]) : 800;
    int enable_drm = (argc > 9) ? atoi(argv[9]) : 1;
    int display_channel = (argc > 10) ? atoi(argv[10]) : 0;
    int display_stretch = (argc > 11) ? atoi(argv[11]) : 1;
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    if (fq_preview_pool_attach((uint32_t)chn) < 0) {
        LOGE("attach preview video pool failed");
        return 1;
    }

    DisplayRga *display = display_rga_create(chn, width, height, disp_w, disp_h, display_stretch);
    if (!display) {
        LOGE("display_rga_create failed");
        return 1;
    }
    display_rga_set_active(display, display_channel);

    DisplayOutput *output = display_output_create(disp_w, disp_h, enable_drm);
    if (!output) {
        LOGE("display_output_create failed");
        display_rga_destroy(display);
        return 1;
    }
    LOGI("player display: src=%dx%d -> disp=%dx%d drm=%d display_ch=%d stretch=%d dump=%d "
         "refresh=%dms",
         width, height, disp_w, disp_h, display_output_using_drm(output), display_channel,
         display_stretch, dump_every, refresh_ms);

    pthread_t th[8];
    PlayerArgs args[8];
    for (int i = 0; i < chn; ++i) {
        args[i].channel = (uint32_t)i;
        args[i].width = width;
        args[i].height = height;
        args[i].dump_every = dump_every;
        args[i].display_channel = display_channel;
        args[i].display = display;
        pthread_create(&th[i], NULL, player_thread, &args[i]);
    }

    DisplayThreadArgs dargs = {display, output, chn, rotate_sec, refresh_ms, disp_w, disp_h};
    pthread_t disp_th;
    pthread_create(&disp_th, NULL, display_thread, &dargs);

    while (g_run) {
        sleep(1);
    }
    for (int i = 0; i < chn; ++i) {
        pthread_join(th[i], NULL);
    }
    pthread_join(disp_th, NULL);
    display_output_destroy(output);
    display_rga_destroy(display);
    fq_preview_pool_detach();
    return 0;
}
