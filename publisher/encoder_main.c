#include "frame_queue.h"
#include "log.h"
#include "camera_release.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t g_run = 1;
static uint32_t g_pub_channels = 1;

static uint32_t read_pub_channels(void) {
    const char *v = getenv("ENC_PUB_CHANNELS");
    if (!v || !v[0]) {
        return 1;
    }
    int n = atoi(v);
    if (n < 1) {
        n = 1;
    }
    if (n > (int)FQ_MAX_CHANNELS) {
        n = FQ_MAX_CHANNELS;
    }
    return (uint32_t)n;
}

static void on_sig(int sig) {
    (void)sig;
    g_run = 0;
}

static int adts_frame_length(const uint8_t *hdr) {
    if (!hdr || hdr[0] != 0xFF || (hdr[1] & 0xF0) != 0xF0) {
        return -1;
    }
    return ((hdr[3] & 0x03) << 11) | (hdr[4] << 3) | ((hdr[5] & 0xE0) >> 5);
}

static int push_adts_frame(const uint8_t *frame, int frame_len, int *seq, int local_tap) {
    if (frame_len <= 7 || frame_len > (int)FQ_AUDIO_SLOT_SIZE) {
        return 0;
    }
    FqFrameMeta meta = {0};
    meta.data_len = (uint32_t)frame_len;
    meta.pts_us = (uint64_t)(*seq) * 23219; /* 1024/44100 s */
    int pushed = 0;
    for (uint32_t ch = 0; ch < g_pub_channels; ++ch) {
        if (fq_push(ch, FQ_TYPE_AUDIO, &meta, frame) == 0) {
            pushed = 1;
        }
    }
    if (!pushed) {
        return 0;
    }
    if (local_tap && fq_preview_audio_attach_channel(0) == 0) {
        if (fq_preview_audio_push(0, &meta, frame) == 0 && (*seq <= 3 || *seq % 100 == 0)) {
            LOGI("audio tap push seq=%d depth=%u", *seq, fq_preview_audio_depth(0));
        }
    }
    ++(*seq);
    if (*seq <= 3 || *seq % 100 == 0) {
        LOGI("audio push seq=%d len=%d depth=%u ch=%u", *seq, frame_len,
             fq_depth(0, FQ_TYPE_AUDIO), g_pub_channels);
    }
    return 1;
}

static int run_aac_gst_to_shm(uint32_t ch, const char *device) {
    char cmd[640];
    const char *dev = (device && device[0]) ? device : "hw:0,0";
    int aac_bps = 128000;
    const char *bps_env = getenv("ENC_AUDIO_BITRATE");
    if (bps_env && bps_env[0]) {
        aac_bps = atoi(bps_env);
        if (aac_bps < 64000) {
            aac_bps = 64000;
        }
    }
    double mic_gain = 4.0;
    const char *gain_env = getenv("ENC_AUDIO_GAIN");
    if (gain_env && gain_env[0]) {
        mic_gain = atof(gain_env);
        if (mic_gain < 1.0) {
            mic_gain = 1.0;
        }
        if (mic_gain > 8.0) {
            mic_gain = 8.0;
        }
    }
    snprintf(cmd, sizeof(cmd),
             "gst-launch-1.0 -q alsasrc device=%s ! audio/x-raw,rate=44100,channels=2 ! "
             "audioconvert ! volume volume=%.1f ! voaacenc bitrate=%d ! "
             "audio/mpeg,mpegversion=4,stream-format=adts ! fdsink fd=1 sync=false",
             dev, mic_gain, aac_bps);
    LOGI("audio gst: %s", cmd);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        LOGE("audio gst popen failed");
        return -1;
    }
    uint8_t chunk[4096];
    uint8_t acc[FQ_AUDIO_SLOT_SIZE * 2];
    int acc_len = 0;
    int seq = 0;
    int local_tap = (getenv("LOCAL_AUDIO_TAP") != NULL);
    if (local_tap) {
        for (int i = 0; i < 60 && g_run; ++i) {
            if (fq_preview_audio_attach_channel(0) == 0) {
                LOGI("audio tap: preview shm attached");
                break;
            }
            usleep(100000);
        }
    }
    while (g_run) {
        size_t n = fread(chunk, 1, sizeof(chunk), fp);
        if (n == 0) {
            break;
        }
        if (acc_len + (int)n > (int)sizeof(acc)) {
            acc_len = 0;
        }
        memcpy(acc + acc_len, chunk, n);
        acc_len += (int)n;
        while (acc_len >= 7) {
            if (!(acc[0] == 0xFF && (acc[1] & 0xF0) == 0xF0)) {
                memmove(acc, acc + 1, (size_t)--acc_len);
                continue;
            }
            int flen = adts_frame_length(acc);
            if (flen <= 0 || flen > (int)sizeof(acc)) {
                acc_len = 0;
                break;
            }
            if (acc_len < flen) {
                break;
            }
            (void)ch;
            push_adts_frame(acc, flen, &seq, local_tap);
            memmove(acc, acc + flen, (size_t)(acc_len - flen));
            acc_len -= flen;
        }
    }
    pclose(fp);
    LOGI("audio gst pushed %d adts frames", seq);
    return seq > 0 ? 0 : -1;
}

static void mpp_h265_enc_params(int fps, char *out, size_t out_sz) {
    int gop = fps / 2;
    if (gop < 15) {
        gop = 15;
    }
    if (gop > 30) {
        gop = 30;
    }
    int bps = 2500000;
    const char *bps_env = getenv("ENC_VIDEO_BPS");
    if (bps_env && bps_env[0]) {
        bps = atoi(bps_env);
        if (bps < 500000) {
            bps = 500000;
        }
    }
    snprintf(out, out_sz, "mpph265enc gop=%d rc-mode=cbr bps=%d", gop, bps);
}

static const char *cam_queue = "queue max-size-buffers=4 ! ";

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

static int push_h265_access_units(int fps, uint8_t *acc, int *acc_len, const uint8_t *data,
                                  int data_len, int *frame) {
    if (*acc_len + data_len > (int)FQ_VIDEO_SLOT_SIZE * 2) {
        *acc_len = 0;
    }
    memcpy(acc + *acc_len, data, (size_t)data_len);
    *acc_len += data_len;

    int pushed = 0;
    int sc = find_start_code(acc, *acc_len, 0);
    if (sc < 0) {
        return 0;
    }
    if (sc > 0) {
        memmove(acc, acc + sc, (size_t)(*acc_len - sc));
        *acc_len -= sc;
    }

    while (1) {
        int next = find_start_code(acc, *acc_len, 4);
        if (next < 0) {
            break;
        }
        FqFrameMeta meta = {0};
        meta.data_len = (uint32_t)next;
        meta.pts_us = (uint64_t)(*frame) * 1000000 / fps;
        meta.is_key = (*frame == 0) ? 1 : 0;
        int frame_pushed = 0;
        for (uint32_t ch = 0; ch < g_pub_channels; ++ch) {
            if (fq_push(ch, FQ_TYPE_VIDEO, &meta, acc) == 0) {
                frame_pushed = 1;
            }
        }
        if (frame_pushed) {
            ++(*frame);
            ++pushed;
            if (*frame <= 3 || *frame % 50 == 0) {
                LOGI("video push seq=%d len=%u depth=%u ch=%u", *frame, meta.data_len,
                     fq_depth(0, FQ_TYPE_VIDEO), g_pub_channels);
            }
        }
        memmove(acc, acc + next, (size_t)(*acc_len - next));
        *acc_len -= next;
    }
    return pushed;
}

static const char *videoflip_method(int rotation) {
    switch (rotation) {
    case 90:
        return "clockwise";
    case 180:
        return "rotate-180";
    case 270:
        return "counterclockwise";
    default:
        return NULL;
    }
}

static int build_video_gst_cmd_tee(char *cmd, size_t cmd_sz, const char *source,
                                   const char *camera_dev, int cam_w, int cam_h,
                                   int width, int height, int fps, int cam_rotation,
                                   int h265_fd, int nv12_fd) {
    char enc[96];
    mpp_h265_enc_params(fps, enc, sizeof(enc));
    if (strcmp(source, "camera") == 0) {
        const char *dev = camera_dev && camera_dev[0] ? camera_dev : "/dev/video-camera0";
        const char *flip = videoflip_method(cam_rotation);
        char flip_elt[64];
        flip_elt[0] = '\0';
        if (flip) {
            snprintf(flip_elt, sizeof(flip_elt), "videoflip method=%s ! ", flip);
        }
        if (cam_w == width && cam_h == height) {
            return snprintf(cmd, cmd_sz,
                            "gst-launch-1.0 -q v4l2src device=%s io-mode=mmap ! "
                            "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                            "%s%s"
                            "tee name=t "
                            "t. ! queue max-size-buffers=4 ! %s ! h265parse config-interval=1 ! "
                            "video/x-h265,stream-format=byte-stream,alignment=au ! "
                            "fdsink fd=%d sync=false "
                            "t. ! queue max-size-buffers=4 ! fdsink fd=%d sync=false",
                            dev, cam_w, cam_h, fps, cam_queue, flip_elt, enc, h265_fd,
                            nv12_fd) >= (int)cmd_sz
                       ? -1
                       : 0;
        }
        return snprintf(cmd, cmd_sz,
                        "gst-launch-1.0 -q v4l2src device=%s io-mode=mmap ! "
                        "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                        "%s%s"
                        "videoscale ! video/x-raw,format=NV12,width=%d,height=%d ! "
                        "tee name=t "
                        "t. ! queue max-size-buffers=4 ! %s ! h265parse config-interval=1 ! "
                        "video/x-h265,stream-format=byte-stream,alignment=au ! "
                        "fdsink fd=%d sync=false "
                        "t. ! queue max-size-buffers=4 ! fdsink fd=%d sync=false",
                        dev, cam_w, cam_h, fps, cam_queue, flip_elt, width, height, enc,
                        h265_fd, nv12_fd) >= (int)cmd_sz
                   ? -1
                   : 0;
    }
    return snprintf(cmd, cmd_sz,
                    "gst-launch-1.0 -q videotestsrc is-live=true pattern=smpte ! "
                    "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                    "tee name=t "
                    "t. ! queue max-size-buffers=2 leaky=downstream ! %s ! h265parse config-interval=1 ! "
                    "video/x-h265,stream-format=byte-stream,alignment=au ! "
                    "fdsink fd=%d sync=false "
                    "t. ! queue max-size-buffers=2 leaky=downstream ! fdsink fd=%d sync=false",
                    width, height, fps, enc, h265_fd, nv12_fd) >= (int)cmd_sz
               ? -1
               : 0;
}

typedef struct {
    int fd;
    int width;
    int height;
    int fps;
} Nv12TapArgs;

static void *nv12_preview_thread(void *arg) {
    Nv12TapArgs *a = (Nv12TapArgs *)arg;
    size_t frame_sz = (size_t)a->width * a->height * 3 / 2;
    uint8_t *buf = (uint8_t *)malloc(frame_sz);
    int seq = 0;
    if (!buf) {
        close(a->fd);
        return NULL;
    }
    while (g_run && fq_preview_attach_channel(0) < 0) {
        usleep(100000);
    }
    LOGI("nv12 tap: preview shm attached, frame=%zu", frame_sz);
    while (g_run) {
        size_t off = 0;
        while (off < frame_sz) {
            ssize_t n = read(a->fd, buf + off, frame_sz - off);
            if (n <= 0) {
                goto out;
            }
            off += (size_t)n;
        }
        FqFrameMeta meta = {0};
        meta.data_len = (uint32_t)frame_sz;
        meta.fmt = FQ_FMT_NV12;
        meta.pts_us = (uint64_t)seq * 1000000 / (uint64_t)(a->fps > 0 ? a->fps : 30);
        if (fq_preview_push(0, &meta, buf) == 0) {
            ++seq;
            if (seq <= 3 || seq % 100 == 0) {
                LOGI("nv12 tap push seq=%d depth=%u", seq, fq_preview_depth(0));
            }
        }
    }
out:
    free(buf);
    close(a->fd);
    LOGI("nv12 tap pushed %d frames total", seq);
    return NULL;
}

static int pump_h265_stream(int fd, int fps, uint8_t *acc, int *acc_len, int *frame) {
    uint8_t chunk[16384];
    while (g_run) {
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        push_h265_access_units(fps, acc, acc_len, chunk, (int)n, frame);
    }
    return *frame;
}

static int run_video_gst_tee(const char *source, const char *camera_dev, int cam_w,
                               int cam_h, int width, int height, int fps, int cam_rotation) {
    char cmd[2048];
    int h265_pipe[2];
    int nv12_pipe[2];
    if (pipe(h265_pipe) < 0 || pipe(nv12_pipe) < 0) {
        LOGE("video tee pipe failed");
        return -1;
    }
    if (build_video_gst_cmd_tee(cmd, sizeof(cmd), source, camera_dev, cam_w, cam_h, width,
                                height, fps, cam_rotation, h265_pipe[1],
                                nv12_pipe[1]) < 0) {
        LOGE("video tee gst cmd too long");
        return -1;
    }
    LOGI("video gst tee: %s", cmd);
    pid_t pid = fork();
    if (pid < 0) {
        LOGE("video tee fork failed");
        return -1;
    }
    if (pid == 0) {
        close(h265_pipe[0]);
        close(nv12_pipe[0]);
        execlp("sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(h265_pipe[1]);
    close(nv12_pipe[1]);

    Nv12TapArgs tap = {nv12_pipe[0], width, height, fps};
    pthread_t th;
    pthread_create(&th, NULL, nv12_preview_thread, &tap);

    uint8_t acc[FQ_VIDEO_SLOT_SIZE * 2];
    int acc_len = 0;
    int frame = 0;
    pump_h265_stream(h265_pipe[0], fps, acc, &acc_len, &frame);
    close(h265_pipe[0]);
    g_run = 0;
    pthread_join(th, NULL);
    waitpid(pid, NULL, 0);
    LOGI("video gst tee pushed %d h265 access units", frame);
    return frame > 0 ? 0 : -1;
}

static int build_video_gst_cmd(char *cmd, size_t cmd_sz, const char *source,
                               const char *camera_dev, int cam_w, int cam_h, int width,
                               int height, int fps, int cam_rotation, int num_buffers) {
    char enc[96];
    mpp_h265_enc_params(fps, enc, sizeof(enc));
    if (strcmp(source, "camera") == 0) {
        const char *dev = camera_dev && camera_dev[0] ? camera_dev : "/dev/video-camera0";
        const char *flip = videoflip_method(cam_rotation);
        char flip_elt[64];
        flip_elt[0] = '\0';
        if (flip) {
            snprintf(flip_elt, sizeof(flip_elt), "videoflip method=%s ! ", flip);
        }
        if (cam_w == width && cam_h == height) {
            if (num_buffers > 0) {
                return snprintf(
                           cmd, cmd_sz,
                           "gst-launch-1.0 -q v4l2src device=%s io-mode=mmap num-buffers=%d ! "
                           "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                           "%s%s"
                           "%s ! h265parse config-interval=1 ! "
                           "video/x-h265,stream-format=byte-stream,alignment=au ! "
                           "queue max-size-buffers=4 ! fdsink fd=1 sync=false",
                           dev, num_buffers, cam_w, cam_h, fps, cam_queue, flip_elt, enc) >=
                           (int)cmd_sz
                           ? -1
                           : 0;
            }
            return snprintf(cmd, cmd_sz,
                            "gst-launch-1.0 -q v4l2src device=%s io-mode=mmap ! "
                            "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                            "%s%s"
                            "%s ! h265parse config-interval=1 ! "
                            "video/x-h265,stream-format=byte-stream,alignment=au ! "
                            "queue max-size-buffers=4 ! fdsink fd=1 sync=false",
                            dev, cam_w, cam_h, fps, cam_queue, flip_elt, enc) >= (int)cmd_sz
                           ? -1
                           : 0;
        }
        if (num_buffers > 0) {
            return snprintf(
                       cmd, cmd_sz,
                       "gst-launch-1.0 -q v4l2src device=%s io-mode=mmap num-buffers=%d ! "
                       "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                       "%s%s"
                       "videoscale ! video/x-raw,format=NV12,width=%d,height=%d ! "
                       "%s ! h265parse config-interval=1 ! "
                       "video/x-h265,stream-format=byte-stream,alignment=au ! "
                       "queue max-size-buffers=4 ! fdsink fd=1 sync=false",
                       dev, num_buffers, cam_w, cam_h, fps, cam_queue, flip_elt, width, height,
                       enc) >= (int)cmd_sz
                       ? -1
                       : 0;
        }
        return snprintf(cmd, cmd_sz,
                        "gst-launch-1.0 -q v4l2src device=%s io-mode=mmap ! "
                        "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                        "%s%s"
                        "videoscale ! video/x-raw,format=NV12,width=%d,height=%d ! "
                        "%s ! h265parse config-interval=1 ! "
                        "video/x-h265,stream-format=byte-stream,alignment=au ! "
                        "queue max-size-buffers=4 ! fdsink fd=1 sync=false",
                        dev, cam_w, cam_h, fps, cam_queue, flip_elt, width, height, enc) >=
                   (int)cmd_sz
                   ? -1
                   : 0;
    }
    if (num_buffers > 0) {
        return snprintf(cmd, cmd_sz,
                        "gst-launch-1.0 -q videotestsrc pattern=smpte num-buffers=%d ! "
                        "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                        "%s ! h265parse config-interval=1 ! "
                        "video/x-h265,stream-format=byte-stream,alignment=au ! "
                        "queue max-size-buffers=1 leaky=downstream ! fdsink fd=1 sync=false",
                        num_buffers, width, height, fps, enc) >= (int)cmd_sz
                   ? -1
                   : 0;
    }
    return snprintf(cmd, cmd_sz,
                    "gst-launch-1.0 -q videotestsrc is-live=true pattern=smpte ! "
                    "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                    "%s ! h265parse config-interval=1 ! "
                    "video/x-h265,stream-format=byte-stream,alignment=au ! "
                    "queue max-size-buffers=1 leaky=downstream ! fdsink fd=1 sync=false",
                    width, height, fps, enc) >= (int)cmd_sz
               ? -1
               : 0;
}

static int run_video_gst_to_shm(const char *source, const char *camera_dev, int cam_w,
                                int cam_h, int width, int height, int fps, int cam_rotation,
                                int seconds, int local_nv12_tap) {
    if (local_nv12_tap && seconds <= 0) {
        return run_video_gst_tee(source, camera_dev, cam_w, cam_h, width, height, fps,
                                 cam_rotation);
    }
    char cmd[1536];
    int num_buffers = (seconds > 0) ? seconds * fps : 0;

    if (strcmp(source, "camera") == 0) {
        (void)camera_dev;
        (void)cam_w;
        (void)cam_h;
    }

    if (build_video_gst_cmd(cmd, sizeof(cmd), source, camera_dev, cam_w, cam_h, width, height,
                            fps, cam_rotation, num_buffers) < 0) {
        LOGE("video gst cmd too long");
        return -1;
    }
    LOGI("video gst: %s", cmd);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        LOGE("video gst popen failed");
        return -1;
    }
    uint8_t chunk[16384];
    uint8_t acc[FQ_VIDEO_SLOT_SIZE * 2];
    int acc_len = 0;
    int frame = 0;
    while (g_run) {
        size_t n = fread(chunk, 1, sizeof(chunk), fp);
        if (n == 0) {
            break;
        }
        push_h265_access_units(fps, acc, &acc_len, chunk, (int)n, &frame);
    }
    if (acc_len > 0) {
        FqFrameMeta meta = {0};
        meta.data_len = (uint32_t)acc_len;
        meta.pts_us = (uint64_t)frame * 1000000 / fps;
        for (uint32_t ch = 0; ch < g_pub_channels; ++ch) {
            fq_push(ch, FQ_TYPE_VIDEO, &meta, acc);
        }
        ++frame;
    }
    pclose(fp);
    LOGI("video gst pushed %d access units", frame);
    return frame > 0 ? 0 : -1;
}

int main(int argc, char **argv) {
    int width = (argc > 1) ? atoi(argv[1]) : 720;
    int height = (argc > 2) ? atoi(argv[2]) : 1280;
    int fps = (argc > 3) ? atoi(argv[3]) : 25;
    int seconds = (argc > 4) ? atoi(argv[4]) : 0;
    const char *source = (argc > 5) ? argv[5] : "testpattern";
    const char *camera_dev = (argc > 6) ? argv[6] : "/dev/video-camera0";
    int cam_w = (argc > 7) ? atoi(argv[7]) : 1920;
    int cam_h = (argc > 8) ? atoi(argv[8]) : 1080;
    int cam_rotation = (argc > 9) ? atoi(argv[9]) : 270;
    int local_nv12_tap = (argc > 10) ? atoi(argv[10]) : 0;
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    setlinebuf(stdout);

    if (strcmp(source, "camera") == 0) {
        LOGI("encoder: MIPI camera %s %dx%d rot=%d -> encode %dx%d @ %dfps", camera_dev,
             cam_w, cam_h, cam_rotation, width, height, fps);
    } else {
        LOGI("encoder: testpattern NV12 %dx%d @ %dfps", width, height, fps);
    }

    g_pub_channels = read_pub_channels();
    LOGI("encoder: pub_channels=%u", g_pub_channels);
    if (fq_pool_create(FQ_TYPE_VIDEO, g_pub_channels) < 0 ||
        fq_pool_create(FQ_TYPE_AUDIO, g_pub_channels) < 0) {
        LOGE("shm create failed");
        return 1;
    }

    pid_t apid = -1;
    const char *enc_audio = getenv("ENC_ENABLE_AUDIO");
    if (!enc_audio || strcmp(enc_audio, "0") != 0) {
        const char *adev = getenv("ENC_AUDIO_DEVICE");
        apid = fork();
        if (apid == 0) {
            run_aac_gst_to_shm(0, adev);
            _exit(0);
        }
        if (apid < 0) {
            LOGW("audio fork failed, continuing video-only");
            apid = 0;
        } else {
            LOGI("encoder: audio child pid=%d device=%s", (int)apid,
                 adev && adev[0] ? adev : "hw:0,0");
        }
    } else {
        LOGI("encoder: audio disabled (ENC_ENABLE_AUDIO=0)");
    }

    int rc;

    if (seconds > 0) {
        rc = run_video_gst_to_shm(source, camera_dev, cam_w, cam_h, width, height, fps,
                                  cam_rotation, seconds, local_nv12_tap);
    } else {
        rc = run_video_gst_to_shm(source, camera_dev, cam_w, cam_h, width, height, fps,
                                  cam_rotation, 0, local_nv12_tap);
    }

    if (apid > 0) {
        kill(apid, SIGTERM);
        waitpid(apid, NULL, 0);
    }
    fq_pool_detach(FQ_TYPE_VIDEO);
    fq_pool_detach(FQ_TYPE_AUDIO);
    return rc;
}
