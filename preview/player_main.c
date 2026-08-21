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
#include <sys/wait.h>
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

static void player_emit_frame(PlayerArgs *a, uint8_t *nv12, int *frame, uint64_t pts_us,
                              int update_display) {
    ++(*frame);
    if (update_display && (int)a->channel == a->display_channel) {
        display_rga_update_channel(a->display, (int)a->channel, nv12);
    }
    if (a->dump_every > 0 && *frame % a->dump_every == 0) {
        char out_path[128];
        snprintf(out_path, sizeof(out_path), "/tmp/ch%u.yuv", a->channel);
        FILE *fp = fopen(out_path, "wb");
        if (fp) {
            fwrite(nv12, 1, (size_t)a->width * a->height * 3 / 2, fp);
            fclose(fp);
            LOGI("dump %s frame=%d pts=%llu y0=%u", out_path, *frame,
                 (unsigned long long)pts_us, nv12[0]);
        }
    }
}

static void player_drain_decoder(MppDecoder *dec, PlayerArgs *a, uint8_t *nv12, int nv12_cap,
                                 int *frame, uint64_t pts_us, int update_display) {
    int rc;
    while ((rc = mpp_decoder_drain(dec, nv12, nv12_cap)) == 0) {
        player_emit_frame(a, nv12, frame, pts_us, update_display);
    }
    (void)rc;
}

static void *player_thread(void *arg) {
    PlayerArgs *a = (PlayerArgs *)arg;
    uint8_t *bitstream = (uint8_t *)malloc(FQ_VIDEO_SLOT_SIZE);
    uint8_t *nv12 = (uint8_t *)malloc((size_t)a->width * a->height * 3 / 2);
    MppDecoder *dec = mpp_decoder_create_hevc(a->width, a->height);
    int frame = 0;
    int nv12_cap = a->width * a->height * 3 / 2;
    int feeds_display = ((int)a->channel == a->display_channel);
    int local_preview = (getenv("PREVIEW_LOCAL") != NULL);
    int skip = 0;
    int put_fail = 0;
    int need_more = 0;

    if (!bitstream || !nv12 || !dec) {
        LOGE("player ch=%u init failed", a->channel);
        free(bitstream);
        free(nv12);
        mpp_decoder_destroy(dec);
        return NULL;
    }

    while (g_run) {
        FqFrameMeta meta;
        int rc = local_preview ? fq_preview_pop_latest(a->channel, &meta, bitstream,
                                                         FQ_VIDEO_SLOT_SIZE)
                               : fq_preview_pop(a->channel, &meta, bitstream, FQ_VIDEO_SLOT_SIZE);
        if (rc == 1) {
            if (!local_preview) {
                player_drain_decoder(dec, a, nv12, nv12_cap, &frame, 0, 0);
            }
            usleep(2000);
            continue;
        }
        if (rc < 0) {
            break;
        }
        if (meta.fmt == FQ_FMT_NV12 && (int)meta.data_len == nv12_cap) {
            player_emit_frame(a, bitstream, &frame, meta.pts_us, feeds_display);
            if (frame > 0 && frame % 100 == 0) {
                LOGI("player ch=%u local_nv12=%d depth=%u", a->channel, frame,
                     fq_preview_depth(a->channel));
            }
            continue;
        }
        if (!au_decodable((int)meta.data_len)) {
            ++skip;
            continue;
        }
        rc = mpp_decoder_decode(dec, bitstream, (int)meta.data_len, nv12, nv12_cap);
        if (rc == 0) {
            player_emit_frame(a, nv12, &frame, meta.pts_us, feeds_display);
        } else if (rc == 1) {
            ++need_more;
        } else {
            ++put_fail;
        }
        player_drain_decoder(dec, a, nv12, nv12_cap, &frame, meta.pts_us, feeds_display);
        if (frame > 0 && frame % 100 == 0) {
            LOGI("player ch=%u decoded=%d depth=%u skip=%d need_more=%d put_fail=%d",
                 a->channel, frame, fq_preview_depth(a->channel), skip, need_more, put_fail);
        }
        /* 队列积压时不 sleep，尽快追帧；每解出一帧都送显 */
        if (feeds_display && fq_preview_depth(a->channel) > 0) {
            continue;
        }
    }
    mpp_decoder_destroy(dec);
    free(bitstream);
    free(nv12);
    LOGI("player ch=%u decoded %d frames display=%d skip=%d need_more=%d put_fail=%d",
         a->channel, frame, feeds_display, skip, need_more, put_fail);
    return NULL;
}

static void *audio_playback_thread(void *arg) {
    (void)arg;
    uint8_t *buf = (uint8_t *)malloc(FQ_AUDIO_SLOT_SIZE);
    int pipefd[2];
    pid_t pid = -1;
    int frames = 0;

    if (!buf) {
        return NULL;
    }
    while (g_run && fq_preview_audio_attach_channel(0) < 0) {
        usleep(100000);
    }
    if (pipe(pipefd) < 0) {
        free(buf);
        return NULL;
    }
    pid = fork();
    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], 0);
        close(pipefd[0]);
        const char *adev = getenv("AUDIO_PLAYBACK_DEVICE");
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "gst-launch-1.0 -q fdsrc blocksize=512 ! queue max-size-buffers=32 ! "
                 "audio/mpeg,mpegversion=4,stream-format=adts,rate=44100,channels=2 ! "
                 "aacparse ! faad ! audioconvert ! audioresample ! autoaudiosink sync=false "
                 "2>/tmp/audio_playback.err");
        execlp("sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(pipefd[0]);
    LOGI("audio playback gst pid=%d", (int)pid);
    while (g_run) {
        FqFrameMeta meta;
        int rc = fq_preview_audio_pop(0, &meta, buf, FQ_AUDIO_SLOT_SIZE);
        if (rc == 1) {
            usleep(5000);
            continue;
        }
        if (rc < 0) {
            break;
        }
        size_t off = 0;
        while (off < meta.data_len) {
            ssize_t n = write(pipefd[1], buf + off, meta.data_len - off);
            if (n <= 0) {
                goto out;
            }
            off += (size_t)n;
        }
        ++frames;
        if (frames <= 3 || frames % 100 == 0) {
            LOGI("audio playback frames=%d depth=%u", frames, fq_preview_audio_depth(0));
        }
    }
out:
    close(pipefd[1]);
    if (pid > 0) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }
    free(buf);
    LOGI("audio playback total frames=%d", frames);
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
            usleep((useconds_t)a->refresh_ms * 1000);
        } else {
            usleep(5000);
        }
        ++tick;
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
    int local_preview = (getenv("PREVIEW_LOCAL") != NULL);
    int enable_audio = (getenv("ENABLE_AUDIO") != NULL);
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    if (fq_preview_pool_attach((uint32_t)chn) < 0) {
        LOGE("attach preview video pool failed");
        return 1;
    }
    if (enable_audio && fq_preview_audio_pool_attach((uint32_t)chn) < 0) {
        LOGE("attach preview audio pool failed");
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
         "refresh=%dms local=%d audio=%d",
         width, height, disp_w, disp_h, display_output_using_drm(output), display_channel,
         display_stretch, dump_every, refresh_ms, local_preview, enable_audio);

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

    pthread_t audio_th;
    int audio_started = 0;
    if (enable_audio) {
        if (pthread_create(&audio_th, NULL, audio_playback_thread, NULL) == 0) {
            audio_started = 1;
        } else {
            LOGW("audio playback thread create failed");
        }
    }

    while (g_run) {
        sleep(1);
    }
    for (int i = 0; i < chn; ++i) {
        pthread_join(th[i], NULL);
    }
    pthread_join(disp_th, NULL);
    if (audio_started) {
        pthread_join(audio_th, NULL);
    }
    display_output_destroy(output);
    display_rga_destroy(display);
    fq_preview_pool_detach();
    fq_preview_audio_pool_detach();
    return 0;
}
