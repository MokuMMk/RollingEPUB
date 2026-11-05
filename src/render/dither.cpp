/*
 * RollingEPUB dithering: threshold, ordered Bayer 4x4 / 8x8 and
 * Floyd-Steinberg error diffusion into MONO1 / GRAY2 / GRAY4 / GRAY8 canvases.
 *
 * The source is an 8-bit grayscale buffer (0 = black, 255 = white).  Ordered
 * matrices are evaluated in absolute canvas coordinates, so drawing the same
 * image at the same place always produces exactly the same pixels.
 *
 * Algorithm provenance: standard ordered-dither threshold matrices and the
 * classic Floyd-Steinberg kernel (public domain); the 2-bit/1-bit level
 * mapping follows the approach used by CrossMux GfxRenderer (MIT).
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cstdint>
#include <vector>

#include "render/canvas.h"
#include "render/image.h"

namespace repub {

namespace {

/* Standard 4x4 Bayer matrix (values 0..15). */
const uint8_t kBayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

/* Standard 8x8 Bayer matrix (values 0..63). */
const uint8_t kBayer8[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21},
};

inline int positive_mod(int value, int modulus) {
    const int m = value % modulus;
    return m < 0 ? m + modulus : m;
}

inline int clamp_int(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

/* Ordered-dither offset (0..254) for a canvas pixel, or -1 when the mode has
 * no matrix (NONE / THRESHOLD / FLOYD_STEINBERG). */
int bayer_offset(repub_dither_t mode, int x, int y) {
    switch (mode) {
        case REPUB_DITHER_BAYER4:
            return static_cast<int>(kBayer4[y & 3][x & 3]) * 16 + 8;
        case REPUB_DITHER_BAYER8:
            return static_cast<int>(kBayer8[y & 7][x & 7]) * 4 + 2;
        default:
            return -1;
    }
}

/* Quantize one 0..255 luminance sample to a canvas level.
 * `add` is the dither threshold offset (0..254); the source is treated as
 * white when gray + add >= 255 for MONO1 and via rounded scaling otherwise. */
int quantize_level(repub_pixel_format_t format, int max_level, int gray, int add) {
    gray = clamp_int(gray, 0, 255);
    if (format == REPUB_FORMAT_MONO1) {
        return (gray + add >= 255) ? 1 : 0;
    }
    const int product = gray * max_level + add;
    const int level = product / 255;
    return clamp_int(level, 0, max_level);
}

/* Nearest-neighbour source coordinate mapping for a possibly scaled rect. */
inline int map_source(int index, int dst_size, int src_size) {
    if (dst_size <= 0) return 0;
    int64_t value = static_cast<int64_t>(index) * src_size / dst_size;
    if (value < 0) value = 0;
    if (value >= src_size) value = src_size - 1;
    return static_cast<int>(value);
}

}  // namespace

void dither_canvas_rect(repub_canvas* canvas, const Rect& dest, const uint8_t* gray,
                        int32_t gray_stride, int32_t src_width, int32_t src_height,
                        repub_dither_t mode, int32_t threshold, int32_t background_level) {
    if (!canvas || !canvas->data || !gray) return;
    if (dest.w <= 0 || dest.h <= 0) return;
    if (src_width <= 0 || src_height <= 0) return;
    if (gray_stride < src_width) gray_stride = src_width;

    if (threshold <= 0) threshold = 128;
    if (threshold > 255) threshold = 255;

    if (background_level >= 0) {
        canvas->fill_rect(dest, background_level);
    }

    const Rect clip = dest.intersect(Rect{0, 0, canvas->width, canvas->height});
    if (clip.empty()) return;

    if (mode == REPUB_DITHER_FLOYD_STEINBERG) {
        /* Serpentine Floyd-Steinberg over the whole destination rect.  The
         * error rows are bounded by the rect width. */
        const int width = dest.w;
        std::vector<float> current(static_cast<size_t>(width) + 2, 0.0f);
        std::vector<float> next(static_cast<size_t>(width) + 2, 0.0f);
        const int max_level = canvas->max_level;

        for (int row = 0; row < dest.h; ++row) {
            const bool left_to_right = (row & 1) == 0;
            const int canvas_y = dest.y + row;
            for (int step = 0; step < width; ++step) {
                const int col = left_to_right ? step : (width - 1 - step);
                const int canvas_x = dest.x + col;
                const int src_x = map_source(col, width, src_width);
                const int src_y = map_source(row, dest.h, src_height);
                const int source = gray[static_cast<size_t>(src_y) *
                                            static_cast<size_t>(gray_stride) +
                                        static_cast<size_t>(src_x)];
                int value = clamp_int(source + static_cast<int>(current[static_cast<size_t>(col) + 1]),
                                      0, 255);
                int level;
                if (canvas->format == REPUB_FORMAT_MONO1) {
                    level = value >= threshold ? 1 : 0;
                } else {
                    level = clamp_int((value * max_level + max_level / 2) / 255, 0, max_level);
                }
                const int produced = (canvas->format == REPUB_FORMAT_MONO1)
                                         ? (level ? 255 : 0)
                                         : (level * 255) / max_level;
                const float error = static_cast<float>(value - produced);

                if (canvas_x >= 0 && canvas_x < canvas->width && canvas_y >= 0 &&
                    canvas_y < canvas->height) {
                    canvas->set_pixel(canvas_x, canvas_y, level);
                }
                if (left_to_right) {
                    current[static_cast<size_t>(col) + 2] += error * (7.0f / 16.0f);
                    next[static_cast<size_t>(col)] += error * (3.0f / 16.0f);
                    next[static_cast<size_t>(col) + 1] += error * (5.0f / 16.0f);
                    next[static_cast<size_t>(col) + 2] += error * (1.0f / 16.0f);
                } else {
                    current[static_cast<size_t>(col)] += error * (7.0f / 16.0f);
                    next[static_cast<size_t>(col) + 2] += error * (3.0f / 16.0f);
                    next[static_cast<size_t>(col) + 1] += error * (5.0f / 16.0f);
                    next[static_cast<size_t>(col)] += error * (1.0f / 16.0f);
                }
            }
            current.swap(next);
            std::fill(next.begin(), next.end(), 0.0f);
        }
        return;
    }

    for (int row = clip.y; row < clip.bottom(); ++row) {
        const int dest_row = row - dest.y;
        const int src_y = map_source(dest_row, dest.h, src_height);
        const uint8_t* src = gray + static_cast<size_t>(src_y) * static_cast<size_t>(gray_stride);
        for (int col = clip.x; col < clip.right(); ++col) {
            const int dest_col = col - dest.x;
            const int src_x = map_source(dest_col, dest.w, src_width);
            const int bayer = bayer_offset(mode, col, row);
            int add;
            if (bayer >= 0) {
                add = clamp_int(bayer + (128 - threshold), 0, 254);
            } else {
                add = (canvas->format == REPUB_FORMAT_MONO1) ? (255 - threshold)
                                                             : (canvas->max_level / 2);
            }
            const int level = quantize_level(canvas->format, canvas->max_level, src[src_x], add);
            canvas->set_pixel(col, row, level);
        }
    }
}

int dither_quantize_gray(int gray, repub_pixel_format_t format, int max_level, int x, int y,
                         repub_dither_t mode, int threshold) {
    if (threshold <= 0) threshold = 128;
    if (threshold > 255) threshold = 255;
    if (max_level <= 0) max_level = format_max_level(format);
    int add;
    const int bayer = bayer_offset(mode, x, y);
    if (bayer >= 0) {
        add = clamp_int(bayer + (128 - threshold), 0, 254);
    } else {
        add = (format == REPUB_FORMAT_MONO1) ? (255 - threshold) : (max_level / 2);
    }
    return quantize_level(format, max_level, gray, add);
}

}  // namespace repub
