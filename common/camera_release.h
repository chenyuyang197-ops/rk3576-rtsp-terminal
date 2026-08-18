#ifndef RK3576_CAMERA_RELEASE_H
#define RK3576_CAMERA_RELEASE_H

#ifdef __cplusplus
extern "C" {
#endif

/* 释放 V4L2 摄像头占用（gst/v4l2 残留进程） */
void camera_release_device(const char *dev_path);

#ifdef __cplusplus
}
#endif

#endif
