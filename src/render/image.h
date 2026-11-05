/*
 * RollingEPUB image decoding, scaling and dithering.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_RENDER_IMAGE_H
#define REPUB_SRC_RENDER_IMAGE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/base.h"

namespace repub {

struct repub_canvas;

struct repub_image {
    const repub_platform_t* platform = nullptr;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> rgba; /* RGBA8, width*height*4 */
};

repub_err_t image_decode(const repub_platform_t* platform, const void* data,
                         size_t size, int32_t max_width, int32_t max_height,
                         repub_image** out);
repub_image* image_create(const repub_platform_t* platform, int32_t width,
                          int32_t height, repub_err_t* err);
void image_free(repub_image* image);
repub_err_t image_scale(const repub_platform_t* platform,
                        const repub_image* src, int32_t width, int32_t height,
                        repub_image** out);
/* Read dimensions from the encoded header only. */
repub_err_t image_probe(const void* data, size_t size, int32_t* width,
                        int32_t* height);
bool image_format_supported(std::string_view path_or_href);

/* Dither/scale/render an image into a canvas. */
repub_err_t canvas_draw_image(repub_canvas* canvas, const repub_image* image,
                              const repub_image_view* view);

/* Dither a grayscale buffer (0..255) into a canvas at (x,y).
 * Used by text/decoration rendering as well. */
void dither_canvas_rect(repub_canvas* canvas, const Rect& dest,
                        const uint8_t* gray, int32_t gray_stride,
                        int32_t src_width, int32_t src_height,
                        repub_dither_t mode, int32_t threshold,
                        int32_t background_level);

}  // namespace repub

#endif  // REPUB_SRC_RENDER_IMAGE_H
