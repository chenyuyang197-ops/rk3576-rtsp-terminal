#include "camera_release.h"

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void camera_release_device(const char *dev_path) {
    const char *dev = (dev_path && dev_path[0]) ? dev_path : "/dev/video-camera0";
    LOGI("camera_release: %s", dev);
    system("pkill -f 'gst-launch.*v4l2src' 2>/dev/null");
    sleep(1);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "fuser -k %s 2>/dev/null", dev);
    system(cmd);
    sleep(1);
    system("systemctl restart rkaiq_3A.service 2>/dev/null");
    sleep(2);
}
