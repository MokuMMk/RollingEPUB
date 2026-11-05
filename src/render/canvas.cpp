/*
 * RollingEPUB canvas implementation.
 *
 * Pixel formats
 * -------------
 *   MONO1 : 1 bit per pixel, MSB first, 0 = black, 1 = white,
 *           row stride = (width + 7) / 8 bytes.
 *   GRAY2 : one byte per pixel, value 0..3   (0 = black).
 *   GRAY4 : one byte per pixel, value 0..15  (0 = black).
 *   GRAY8 : one byte per pixel, value 0..255 (0 = black).
 *
 * All drawing operations clip silently; callers pass page coordinates and
 * the canvas is the single source of bounds policy.
 *
 * SPDX-License-Identifier: MIT
 */

#include "render/canvas.h"

#include <cstring>
#include <limits>
#include <new>

namespace repub {

int format_max_level(repub_pixel_format_t format) {
    switch (format) {
        case REPUB_FORMAT_MONO1:
            return 1;
        case REPUB_FORMAT_GRAY2:
            return 3;
        case REPUB_FORMAT_GRAY4:
            return 15;
        case REPUB_FORMAT_GRAY8:
            return 255;
    }
    return 1;
}

int format_bytes_per_row(int32_t width, repub_pixel_format_t format) {
    if (width <= 0) return 0;
    if (format == REPUB_FORMAT_MONO1) return (width + 7) / 8;
    return width;
}

namespace {

inline int clamp_level(int level, int max_level) {
    if (level < 0) return 0;
    if (level > max_level) return max_level;
    return level;
}

/* Round (a + b*coverage/255) to the nearest integer for both signs. */
inline int blend_round(int from, int to, uint32_t coverage) {
    const int diff = to - from;
    const int64_t num = static_cast<int64_t>(diff) * static_cast<int64_t>(coverage);
    if (num >= 0) {
        return from + static_cast<int>((num + 127) / 255);
    }
    return from + static_cast<int>((num - 127) / 255);
}

}  // namespace

void repub_canvas::set_pixel(int32_t x, int32_t y, int level) {
    if (!data || x < 0 || y < 0 || x >= width || y >= height) return;
    level = clamp_level(level, max_level);
    uint8_t* row = data + static_cast<size_t>(y) * static_cast<size_t>(stride);
    if (format == REPUB_FORMAT_MONO1) {
        const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
        if (level) {
            row[x >> 3] = static_cast<uint8_t>(row[x >> 3] | mask);
        } else {
            row[x >> 3] = static_cast<uint8_t>(row[x >> 3] & static_cast<uint8_t>(~mask));
        }
    } else {
        row[x] = static_cast<uint8_t>(level);
    }
}

int repub_canvas::get_pixel(int32_t x, int32_t y) const {
    if (!data || x < 0 || y < 0 || x >= width || y >= height) return 0;
    const uint8_t* row = data + static_cast<size_t>(y) * static_cast<size_t>(stride);
    if (format == REPUB_FORMAT_MONO1) {
        return (row[x >> 3] >> (7 - (x & 7))) & 1;
    }
    return row[x];
}

void repub_canvas::blend_pixel(int32_t x, int32_t y, int level, uint32_t coverage) {
    if (!data || x < 0 || y < 0 || x >= width || y >= height) return;
    if (coverage == 0) return;
    level = clamp_level(level, max_level);
    if (coverage >= 255) {
        set_pixel(x, y, level);
        return;
    }
    const int old = get_pixel(x, y);
    const int value = blend_round(old, level, coverage);
    set_pixel(x, y, value);
}

void repub_canvas::clear(int level) {
    if (!data || width <= 0 || height <= 0) return;
    level = clamp_level(level, max_level);
    if (format == REPUB_FORMAT_MONO1) {
        const uint8_t byte = level ? 0xFF : 0x00;
        std::memset(data, byte, static_cast<size_t>(stride) * static_cast<size_t>(height));
        return;
    }
    for (int32_t y = 0; y < height; ++y) {
        std::memset(data + static_cast<size_t>(y) * static_cast<size_t>(stride),
                    static_cast<uint8_t>(level), static_cast<size_t>(width));
    }
}

void repub_canvas::fill_rect(const Rect& rect, int level) {
    if (rect.empty() || !data) return;
    const Rect clip = rect.intersect(Rect{0, 0, width, height});
    if (clip.empty()) return;
    level = clamp_level(level, max_level);
    if (format == REPUB_FORMAT_MONO1) {
        for (int32_t y = clip.y; y < clip.bottom(); ++y) {
            uint8_t* row = data + static_cast<size_t>(y) * static_cast<size_t>(stride);
            for (int32_t x = clip.x; x < clip.right(); ++x) {
                const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
                if (level) {
                    row[x >> 3] = static_cast<uint8_t>(row[x >> 3] | mask);
                } else {
                    row[x >> 3] = static_cast<uint8_t>(row[x >> 3] & static_cast<uint8_t>(~mask));
                }
            }
        }
        return;
    }
    for (int32_t y = clip.y; y < clip.bottom(); ++y) {
        std::memset(data + static_cast<size_t>(y) * static_cast<size_t>(stride) + clip.x,
                    static_cast<uint8_t>(level), static_cast<size_t>(clip.w));
    }
}

void repub_canvas::draw_hline(int32_t x, int32_t y, int32_t w, int level) {
    if (w <= 0 || !data) return;
    if (y < 0 || y >= height) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (x + w > width) w = width - x;
    if (w <= 0) return;
    level = clamp_level(level, max_level);
    uint8_t* row = data + static_cast<size_t>(y) * static_cast<size_t>(stride);
    if (format == REPUB_FORMAT_MONO1) {
        for (int32_t i = 0; i < w; ++i) {
            const int32_t px = x + i;
            const uint8_t mask = static_cast<uint8_t>(0x80u >> (px & 7));
            if (level) {
                row[px >> 3] = static_cast<uint8_t>(row[px >> 3] | mask);
            } else {
                row[px >> 3] = static_cast<uint8_t>(row[px >> 3] & static_cast<uint8_t>(~mask));
            }
        }
        return;
    }
    std::memset(row + x, static_cast<uint8_t>(level), static_cast<size_t>(w));
}

void repub_canvas::draw_vline(int32_t x, int32_t y, int32_t h, int level) {
    if (h <= 0 || !data) return;
    if (x < 0 || x >= width) return;
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (y + h > height) h = height - y;
    if (h <= 0) return;
    level = clamp_level(level, max_level);
    if (format == REPUB_FORMAT_MONO1) {
        const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
        for (int32_t i = 0; i < h; ++i) {
            uint8_t* row = data + static_cast<size_t>(y + i) * static_cast<size_t>(stride);
            if (level) {
                row[x >> 3] = static_cast<uint8_t>(row[x >> 3] | mask);
            } else {
                row[x >> 3] = static_cast<uint8_t>(row[x >> 3] & static_cast<uint8_t>(~mask));
            }
        }
        return;
    }
    for (int32_t i = 0; i < h; ++i) {
        data[static_cast<size_t>(y + i) * static_cast<size_t>(stride) + x] =
            static_cast<uint8_t>(level);
    }
}

repub_err_t canvas_create(const repub_platform_t* platform, int32_t width,
                          int32_t height, repub_pixel_format_t format,
                          repub_canvas** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    if (width <= 0 || height <= 0) return REPUB_ERR_INVALID_ARG;
    if (format != REPUB_FORMAT_MONO1 && format != REPUB_FORMAT_GRAY2 &&
        format != REPUB_FORMAT_GRAY4 && format != REPUB_FORMAT_GRAY8) {
        return REPUB_ERR_INVALID_ARG;
    }
    const int32_t stride = format_bytes_per_row(width, format);
    if (stride <= 0) return REPUB_ERR_INVALID_ARG;
    const size_t row_bytes = static_cast<size_t>(stride);
    if (static_cast<size_t>(height) > std::numeric_limits<size_t>::max() / row_bytes) {
        return REPUB_ERR_LIMIT;
    }
    const size_t total = row_bytes * static_cast<size_t>(height);
    const repub_platform_t* plat = effective_platform(platform);

    repub_canvas* canvas = new (std::nothrow) repub_canvas();
    if (!canvas) return REPUB_ERR_MEMORY;
    canvas->platform = plat;
    canvas->width = width;
    canvas->height = height;
    canvas->stride = stride;
    canvas->format = format;
    canvas->max_level = format_max_level(format);
    canvas->data = static_cast<uint8_t*>(palloc(plat, total));
    if (!canvas->data) {
        delete canvas;
        return REPUB_ERR_MEMORY;
    }
    canvas->owned = true;
    canvas->clear(canvas->max_level); /* white page */
    *out = canvas;
    return REPUB_OK;
}

repub_err_t canvas_wrap(const repub_platform_t* platform, void* pixels,
                        int32_t width, int32_t height, int32_t stride,
                        repub_pixel_format_t format, repub_canvas** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    if (!pixels || width <= 0 || height <= 0) return REPUB_ERR_INVALID_ARG;
    if (format != REPUB_FORMAT_MONO1 && format != REPUB_FORMAT_GRAY2 &&
        format != REPUB_FORMAT_GRAY4 && format != REPUB_FORMAT_GRAY8) {
        return REPUB_ERR_INVALID_ARG;
    }
    const int32_t min_stride = format_bytes_per_row(width, format);
    if (stride < min_stride) return REPUB_ERR_INVALID_ARG;

    repub_canvas* canvas = new (std::nothrow) repub_canvas();
    if (!canvas) return REPUB_ERR_MEMORY;
    canvas->platform = effective_platform(platform);
    canvas->width = width;
    canvas->height = height;
    canvas->stride = stride;
    canvas->format = format;
    canvas->max_level = format_max_level(format);
    canvas->data = static_cast<uint8_t*>(pixels);
    canvas->owned = false;
    *out = canvas;
    return REPUB_OK;
}

void canvas_destroy(repub_canvas* canvas) {
    if (!canvas) return;
    if (canvas->owned && canvas->data) {
        pfree(canvas->platform, canvas->data);
    }
    canvas->data = nullptr;
    delete canvas;
}

}  // namespace repub
