/*
 * RollingEPUB canvas: a small grayscale/1-bit framebuffer abstraction.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_RENDER_CANVAS_H
#define REPUB_SRC_RENDER_CANVAS_H

#include <cstddef>
#include <cstdint>

#include "core/base.h"

namespace repub {

/* Pixel format max level: MONO1=1, GRAY2=3, GRAY4=15, GRAY8=255. */
int format_max_level(repub_pixel_format_t format);
int format_bytes_per_row(int32_t width, repub_pixel_format_t format);

struct repub_canvas {
    const repub_platform_t* platform = nullptr;
    int32_t width = 0;
    int32_t height = 0;
    int32_t stride = 0; /* bytes per row */
    repub_pixel_format_t format = REPUB_FORMAT_MONO1;
    uint8_t* data = nullptr;
    bool owned = false;
    int max_level = 1;

    void set_pixel(int32_t x, int32_t y, int level);
    int get_pixel(int32_t x, int32_t y) const;
    /* Alpha blend a level (0..max) with 0..255 coverage. */
    void blend_pixel(int32_t x, int32_t y, int level, uint32_t coverage);
    void clear(int level);
    void fill_rect(const Rect& rect, int level);
    void draw_hline(int32_t x, int32_t y, int32_t w, int level);
    void draw_vline(int32_t x, int32_t y, int32_t h, int level);
};

repub_err_t canvas_create(const repub_platform_t* platform, int32_t width,
                          int32_t height, repub_pixel_format_t format,
                          repub_canvas** out);
repub_err_t canvas_wrap(const repub_platform_t* platform, void* pixels,
                        int32_t width, int32_t height, int32_t stride,
                        repub_pixel_format_t format, repub_canvas** out);
void canvas_destroy(repub_canvas* canvas);

}  // namespace repub

#endif  // REPUB_SRC_RENDER_CANVAS_H
