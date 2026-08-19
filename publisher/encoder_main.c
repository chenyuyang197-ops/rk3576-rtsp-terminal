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

static void on_sig(int sig) {
    (void)sig;
    g_run = 0;
}

static int run_aac_gst_to_shm(uint32_t ch) {
    FILE *fp = popen(
        "gst-launch-1.0 -q alsasrc device=hw:0,0 ! audio/x-raw,rate=44100,channels=2 ! "
        "audioconvert ! voaacenc bitrate=64000 ! audio/mpeg,mpegversion=4,stream-format=adts ! "
        "fdsink fd=1 sync=false",
        "r");
    if (!fp) {
        return -1;
    }
    uint8_t buf[FQ_AUDIO_SLOT_SIZE];
    while (g_run) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n == 0) {
            break;
        }
        FqFrameMeta meta = {0};
        meta.data_len = (uint32_t)n;
        fq_push(ch, FQ_TYPE_AUDIO, &meta, buf);
    }
    pclose(fp);
    return 0;
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
        if (fq_push(0, FQ_TYPE_VIDEO, &meta, acc) == 0) {
            ++(*frame);
            ++pushed;
            if (*frame <= 3 || *frame % 50 == 0) {
                LOGI("video push seq=%d len=%u depth=%u", *frame, meta.data_len,
                     fq_depth(0, FQ_TYPE_VIDEO));
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
    if (strcmp(source, "camera") == 0) {
        const char *dev = camera_dev && camera_dev[0] ? camera_dev : "/dev/video-camera0";
        const char *flip = videoflip_method(cam_rotation);
        char flip_elt[64];
        flip_elt[0] = '\0';
        if (flip) {
            snprintf(flip_elt, sizeof(flip_elt), "videoflip method=%s ! ", flip);
        }
        return snprintf(cmd, cmd_sz,
                        "gst-launch-1.0 -q v4l2src device=%s io-mode=mmap ! "
                        "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                        "queue max-size-buffers=2 leaky=downstream ! %s"
                        "videoscale ! video/x-raw,format=NV12,width=%d,height=%d ! "
                        "tee name=t "
                        "t. ! queue max-size-buffers=2 leaky=downstream ! mpph265enc gop=%d ! "
                        "h265parse config-interval=1 ! "
                        "video/x-h265,stream-format=byte-stream,alignment=au ! "
                        "fdsink fd=%d sync=false "
                        "t. ! queue max-size-buffers=2 leaky=downstream ! fdsink fd=%d sync=false",
                        dev, cam_w, cam_h, fps, flip_elt, width, height, fps, h265_fd,
                        nv12_fd) >= (int)cmd_sz
                   ? -1
                   : 0;
    }
    return snprintf(cmd, cmd_sz,
                    "gst-launch-1.0 -q videotestsrc is-live=true pattern=smpte ! "
                    "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                    "tee name=t "
                    "t. ! queue max-size-buffers=2 leaky=downstream ! mpph265enc gop=%d ! "
                    "h265parse config-interval=1 ! "
                    "video/x-h265,stream-format=byte-stream,alignment=au ! "
                    "fdsink fd=%d sync=false "
                    "t. ! queue max-size-buffers=2 leaky=downstream ! fdsink fd=%d sync=false",
                    width, height, fps, fps, h265_fd, nv12_fd) >= (int)cmd_sz
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
    if (strcmp(source, "camera") == 0) {
        const char *dev = camera_dev && camera_dev[0] ? camera_dev : "/dev/video-camera0";
        const char *flip = videoflip_method(cam_rotation);
        char flip_elt[64];
        flip_elt[0] = '\0';
        if (flip) {
            snprintf(flip_elt, sizeof(flip_elt), "videoflip method=%s ! ", flip);
        }
        if (num_buffers > 0) {
            return snprintf(
                       cmd, cmd_sz,
                       "gst-launch-1.0 -q v4l2src device=%s io-mode=mmap num-buffers=%d ! "
                       "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                       "queue max-size-buffers=2 leaky=downstream ! %s"
                       "videoscale ! video/x-raw,format=NV12,width=%d,height=%d ! "
                       "mpph265enc gop=%d ! h265parse config-interval=1 ! "
                       "video/x-h265,stream-format=byte-stream,alignment=au ! "
                       "queue max-size-buffers=2 leaky=downstream ! fdsink fd=1 sync=false",
                       dev, num_buffers, cam_w, cam_h, fps, flip_elt, width, height, fps) >=
                   (int)cmd_sz
                       ? -1
                       : 0;
        }
        return snprintf(cmd, cmd_sz,
                        "gst-launch-1.0 -q v4l2src device=%s io-mode=mmap ! "
                        "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                        "queue max-size-buffers=2 leaky=downstream ! %s"
                        "videoscale ! video/x-raw,format=NV12,width=%d,height=%d ! "
                        "mpph265enc gop=%d ! h265parse config-interval=1 ! "
                        "video/x-h265,stream-format=byte-stream,alignment=au ! "
                        "queue max-size-buffers=2 leaky=downstream ! fdsink fd=1 sync=false",
                        dev, cam_w, cam_h, fps, flip_elt, width, height, fps) >= (int)cmd_sz
                   ? -1
                   : 0;
    }
    if (num_buffers > 0) {
        return snprintf(cmd, cmd_sz,
                        "gst-launch-1.0 -q videotestsrc pattern=smpte num-buffers=%d ! "
                        "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                        "mpph265enc gop=%d ! h265parse config-interval=1 ! "
                        "video/x-h265,stream-format=byte-stream,alignment=au ! "
                        "queue max-size-buffers=1 leaky=downstream ! fdsink fd=1 sync=false",
                        num_buffers, width, height, fps, fps) >= (int)cmd_sz
                   ? -1
                   : 0;
    }
    return snprintf(cmd, cmd_sz,
                    "gst-launch-1.0 -q videotestsrc is-live=true pattern=smpte ! "
                    "video/x-raw,format=NV12,width=%d,height=%d,framerate=%d/1 ! "
                    "mpph265enc gop=%d ! h265parse config-interval=1 ! "
                    "video/x-h265,stream-format=byte-stream,alignment=au ! "
                    "queue max-size-buffers=1 leaky=downstream ! fdsink fd=1 sync=false",
                    width, height, fps, fps) >= (int)cmd_sz
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
        fq_push(0, FQ_TYPE_VIDEO, &meta, acc);
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

    if (fq_pool_create(FQ_TYPE_VIDEO, 1) < 0 || fq_pool_create(FQ_TYPE_AUDIO, 1) < 0) {
        LOGE("shm create failed");
        return 1;
    }

    pid_t apid = fork();
    if (apid == 0) {
        run_aac_gst_to_shm(0);
        _exit(0);
    } else if (apid < 0) {
        LOGW("audio fork failed, continuing video-only");
        apid = 0;
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
