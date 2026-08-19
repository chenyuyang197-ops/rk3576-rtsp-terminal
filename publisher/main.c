#include "ini_config.h"
#include "log.h"
#include "net_init.h"
#include "process_supervisor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *cfg_path = (argc > 1) ? argv[1] : "config/publisher.ini";
    const char *bin_dir = (argc > 2) ? argv[2] : ".";
    int port_override = (argc > 3) ? atoi(argv[3]) : -1;
    int run_sec_override = (argc > 4) ? atoi(argv[4]) : -1;

    IniConfig cfg;
    if (ini_load(cfg_path, &cfg) < 0) {
        LOGE("load ini failed: %s", cfg_path);
        return 1;
    }

    net_init_from_ini(&cfg);

    int port = (port_override >= 0) ? port_override : ini_get_int(&cfg, "rtspPort", 554);
    int run_sec = (run_sec_override >= 0) ? run_sec_override : ini_get_int(&cfg, "runSeconds", 0);
    int width = ini_get_int(&cfg, "width", 720);
    int height = ini_get_int(&cfg, "height", 1280);
    int fps = ini_get_int(&cfg, "fps", 25);
    const char *vsrc = ini_get(&cfg, "videoSource", "testpattern");
    const char *cam_dev = ini_get(&cfg, "cameraDevice", "/dev/video-camera0");
    int cam_w = ini_get_int(&cfg, "cameraWidth", 1920);
    int cam_h = ini_get_int(&cfg, "cameraHeight", 1080);
    int cam_rot = ini_get_int(&cfg, "cameraRotation", 270);
    int local_tap = ini_get_int(&cfg, "localNv12Tap", 0);

    char enc_bin[512];
    char srv_bin[512];
    char w_arg[16], h_arg[16], fps_arg[16], port_arg[16], sec_arg[16];
    char src_arg[32], cam_arg[64], camw_arg[16], camh_arg[16], camrot_arg[16], tap_arg[16];
    snprintf(enc_bin, sizeof(enc_bin), "%s/encoder", bin_dir);
    snprintf(srv_bin, sizeof(srv_bin), "%s/rtsp_server", bin_dir);
    snprintf(w_arg, sizeof(w_arg), "%d", width);
    snprintf(h_arg, sizeof(h_arg), "%d", height);
    snprintf(fps_arg, sizeof(fps_arg), "%d", fps);
    snprintf(port_arg, sizeof(port_arg), "%d", port);
    snprintf(sec_arg, sizeof(sec_arg), "%d", run_sec);
    snprintf(src_arg, sizeof(src_arg), "%s", vsrc);
    snprintf(cam_arg, sizeof(cam_arg), "%s", cam_dev);
    snprintf(camw_arg, sizeof(camw_arg), "%d", cam_w);
    snprintf(camh_arg, sizeof(camh_arg), "%d", cam_h);
    snprintf(camrot_arg, sizeof(camrot_arg), "%d", cam_rot);
    snprintf(tap_arg, sizeof(tap_arg), "%d", local_tap);

    LOGI("publisher_supervisor: %dx%d@%dfps port=%d source=%s cam=%s %dx%d rot=%d tap=%d",
         width, height, fps, port, vsrc, cam_dev, cam_w, cam_h, cam_rot, local_tap);

    SupState st;
    memset(&st, 0, sizeof(st));
    sup_add11(&st, "encoder", enc_bin, w_arg, h_arg, fps_arg, sec_arg, src_arg, cam_arg,
              camw_arg, camh_arg, camrot_arg, tap_arg, NULL);
    sup_start_one(&st, 0);
    sleep(2);
    sup_add(&st, "rtsp_server", srv_bin, port_arg, NULL);
    sup_start_one(&st, 1);

    if (run_sec > 0) {
        sleep((unsigned)run_sec);
        return 0;
    }
    while (1) {
        pid_t pid;
        int status;
        int idx = sup_wait_any(&st, &pid, &status);
        if (idx < 0) {
            break;
        }
        if (idx == 1) {
            LOGW("rtsp_server exited, restarting");
            sleep(2);
            st.tasks[1].exited = 0;
            st.tasks[1].pid = -1;
            sup_start_one(&st, 1);
        }
    }
    return 0;
}
