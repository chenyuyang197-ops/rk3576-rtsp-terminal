#include "frame_queue.h"
#include "ini_config.h"
#include "log.h"
#include "net_init.h"
#include "process_supervisor.h"

#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int count_rtsp_tasks(const IniConfig *cfg, int chn) {
    if (chn <= 1) {
        return chn;
    }
    const char *url0 = ini_get(cfg, "rtspUrl_0", "rtsp://127.0.0.1:554/live");
    for (int i = 1; i < chn; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "rtspUrl_%d", i);
        if (strcmp(ini_get(cfg, key, url0), url0) != 0) {
            return chn;
        }
    }
    LOGI("preview: duplicate rtsp URLs -> single rtsp_channel");
    return 1;
}

int main(int argc, char **argv) {
    const char *cfg_path = (argc > 1) ? argv[1] : "config/preview.ini";
    const char *bin_dir = (argc > 2) ? argv[2] : ".";
    IniConfig cfg;
    if (ini_load(cfg_path, &cfg) < 0) {
        LOGE("load ini failed: %s", cfg_path);
        return 1;
    }

    net_init_from_ini(&cfg);

    int chn = ini_get_int(&cfg, "enableChnNum", 2);
    int run_sec = ini_get_int(&cfg, "runSeconds", 0);
    int width = ini_get_int(&cfg, "width", 720);
    int height = ini_get_int(&cfg, "height", 1280);
    int rotate_sec = ini_get_int(&cfg, "rotateSeconds", 5);
    int refresh_ms = ini_get_int(&cfg, "refreshMs", 33);
    int disp_w = ini_get_int(&cfg, "displayWidth", 480);
    int disp_h = ini_get_int(&cfg, "displayHeight", 800);
    int enable_drm = ini_get_int(&cfg, "enableMipiDisplay", 1);
    int dump_every = ini_get_int(&cfg, "dumpEvery", 0);
    int display_channel = ini_get_int(&cfg, "displayChannel", 0);
    int display_stretch = ini_get_int(&cfg, "displayStretch", 1);
    int enable_audio = ini_get_int(&cfg, "enableAudio", 1);
    const char *preview_mode = ini_get(&cfg, "previewMode", "rtsp");
    int local_preview = (strcmp(preview_mode, "local") == 0);
    int rtsp_tasks = local_preview ? 0 : count_rtsp_tasks(&cfg, chn);
    char rtsp_bin[512];
    char player_bin[512];
    snprintf(rtsp_bin, sizeof(rtsp_bin), "%s/rtsp_channel", bin_dir);
    snprintf(player_bin, sizeof(player_bin), "%s/player", bin_dir);

    if (fq_preview_pool_create((uint32_t)chn) < 0) {
        LOGE("preview shm create failed for %d channels", chn);
        return 1;
    }
    if (enable_audio && fq_preview_audio_pool_create((uint32_t)chn) < 0) {
        LOGE("preview audio shm create failed for %d channels", chn);
        return 1;
    }

    if (local_preview) {
        LOGI("preview: local NV12 mode (bypass rtsp_channel/decode)");
        setenv("PREVIEW_LOCAL", "1", 1);
    }
    if (enable_audio) {
        setenv("ENABLE_AUDIO", "1", 1);
        LOGI("preview: microphone playback enabled");
    }

    SupState st;
    memset(&st, 0, sizeof(st));
    char ch_arg[16];
    for (int i = 0; i < rtsp_tasks; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "rtspUrl_%d", i);
        const char *url = ini_get(&cfg, key, "rtsp://127.0.0.1:554/live");
        snprintf(ch_arg, sizeof(ch_arg), "%d", i);
        sup_add(&st, "rtsp_channel", rtsp_bin, ch_arg, url);
    }
    char chn_arg[16], w_arg[16], h_arg[16], dump_arg[16], rot_arg[16], ref_arg[16];
    char disp_w_arg[16], disp_h_arg[16], drm_arg[16], disp_ch_arg[16], stretch_arg[16];
    snprintf(chn_arg, sizeof(chn_arg), "%d", chn);
    snprintf(w_arg, sizeof(w_arg), "%d", width);
    snprintf(h_arg, sizeof(h_arg), "%d", height);
    snprintf(dump_arg, sizeof(dump_arg), "%d", dump_every);
    snprintf(rot_arg, sizeof(rot_arg), "%d", rotate_sec);
    snprintf(ref_arg, sizeof(ref_arg), "%d", refresh_ms);
    snprintf(disp_w_arg, sizeof(disp_w_arg), "%d", disp_w);
    snprintf(disp_h_arg, sizeof(disp_h_arg), "%d", disp_h);
    snprintf(drm_arg, sizeof(drm_arg), "%d", enable_drm);
    snprintf(disp_ch_arg, sizeof(disp_ch_arg), "%d", display_channel);
    snprintf(stretch_arg, sizeof(stretch_arg), "%d", display_stretch);
    sup_add11(&st, "player", player_bin, chn_arg, w_arg, h_arg, dump_arg, rot_arg, ref_arg,
              disp_w_arg, disp_h_arg, drm_arg, disp_ch_arg, stretch_arg);

    /* player 先起；local 模式不等 RTSP */
    sup_start_one(&st, rtsp_tasks);
    if (!local_preview) {
        sleep(3);
        if (rtsp_tasks > 0) {
            sup_start_one(&st, 0);
            sleep(10);
        }
        for (int i = 1; i < rtsp_tasks; ++i) {
            sup_start_one(&st, i);
            sleep(2);
        }
    }

    if (run_sec > 0) {
        sleep((unsigned)run_sec);
        LOGI("preview_supervisor: runSeconds=%d elapsed, stopping children", run_sec);
        for (int i = 0; i < st.count; ++i) {
            if (st.tasks[i].pid > 0) {
                kill(st.tasks[i].pid, SIGTERM);
            }
        }
        for (int n = 0; n < st.count; ++n) {
            pid_t pid;
            int status;
            if (sup_wait_any(&st, &pid, &status) < 0) {
                break;
            }
        }
        return 0;
    }

    while (1) {
        pid_t pid;
        int status;
        if (sup_wait_any(&st, &pid, &status) < 0) {
            break;
        }
    }
    return 0;
}
