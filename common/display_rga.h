#ifndef RK3576_DISPLAY_RGA_H
#define RK3576_DISPLAY_RGA_H

#include <stdint.h>

typedef struct DisplayRga DisplayRga;

DisplayRga *display_rga_create(int channel_count, int src_w, int src_h, int disp_w, int disp_h,
                               int stretch);
void display_rga_destroy(DisplayRga *d);

uint8_t *display_rga_compose_buf(DisplayRga *d);
int display_rga_update_channel(DisplayRga *d, int channel, const uint8_t *src_nv12);
int display_rga_set_active(DisplayRga *d, int channel);
int display_rga_render_active(DisplayRga *d, uint8_t *disp_nv12);
int display_rga_dump_active(DisplayRga *d, const char *path);

#endif
