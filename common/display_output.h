#ifndef RK3576_DISPLAY_OUTPUT_H
#define RK3576_DISPLAY_OUTPUT_H

#include <stdint.h>

/* MIPI 屏（C42388916 480×800）输出；无 DRM 时回退 YUV dump */
typedef struct DisplayOutput DisplayOutput;

DisplayOutput *display_output_create(int width, int height, int enable_drm);
void display_output_destroy(DisplayOutput *o);

/* 呈现一帧 NV12（已是屏分辨率）；0=成功，-1=失败 */
int display_output_present(DisplayOutput *o, const uint8_t *nv12);

/* 源分辨率 NV12 一次 RGA 缩放+送显；对齐 EASY-EAI displayCurChn 单 pass */
int display_output_present_src(DisplayOutput *o, const uint8_t *src, int src_w, int src_h,
                               int stretch);

const char *display_output_dump_path(const DisplayOutput *o);
int display_output_width(const DisplayOutput *o);
int display_output_height(const DisplayOutput *o);
int display_output_using_drm(const DisplayOutput *o);

#endif
