#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fill_nv12_bar(uint8_t *y, uint8_t *uv, int w, int h, int frame_idx) {
    int bar_w = w / 7;
    const uint8_t y_vals[7] = {235, 210, 170, 145, 106, 81, 41};
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            int bar = (col / bar_w) % 7;
            y[row * w + col] = (uint8_t)((y_vals[bar] + (frame_idx & 7)) & 0xFF);
        }
    }
    for (int row = 0; row < h / 2; ++row) {
        for (int col = 0; col < w / 2; ++col) {
            uv[row * w + col * 2] = 128;
            uv[row * w + col * 2 + 1] = 128;
        }
    }
}

int main(int argc, char **argv) {
    const int w = (argc > 1) ? atoi(argv[1]) : 1280;
    const int h = (argc > 2) ? atoi(argv[2]) : 720;
    const char *out = (argc > 3) ? argv[3] : "/tmp/test_pattern.nv12";
    const int frames = (argc > 4) ? atoi(argv[4]) : 1;

    size_t y_size = (size_t)w * h;
    size_t uv_size = y_size / 2;
    uint8_t *buf = (uint8_t *)malloc(y_size + uv_size);
    if (!buf) {
        return 1;
    }
    FILE *fp = fopen(out, "wb");
    if (!fp) {
        free(buf);
        return 1;
    }
    for (int i = 0; i < frames; ++i) {
        fill_nv12_bar(buf, buf + y_size, w, h, i);
        if (fwrite(buf, 1, y_size + uv_size, fp) != y_size + uv_size) {
            fclose(fp);
            free(buf);
            return 1;
        }
    }
    fclose(fp);
    free(buf);
    printf("wrote %s (%dx%d x %d frames)\n", out, w, h, frames);
    return 0;
}
