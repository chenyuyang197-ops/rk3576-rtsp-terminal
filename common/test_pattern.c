#include "test_pattern.h"

#include <string.h>

void test_pattern_fill_nv12(uint8_t *nv12, int width, int height, int frame_idx) {
    uint8_t *y = nv12;
    uint8_t *uv = nv12 + (size_t)width * height;
    int bar_w = width / 7;
    const uint8_t y_vals[7] = {235, 210, 170, 145, 106, 81, 41};
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            int bar = (col / bar_w) % 7;
            y[row * width + col] = (uint8_t)((y_vals[bar] + (frame_idx & 7)) & 0xFF);
        }
    }
    for (int row = 0; row < height / 2; ++row) {
        for (int col = 0; col < width / 2; ++col) {
            uv[row * width + col * 2] = 128;
            uv[row * width + col * 2 + 1] = 128;
        }
    }
}
