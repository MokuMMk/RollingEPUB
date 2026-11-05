/*
 * RollingEPUB image support: decode (stb_image), header probing, scaling and
 * canvas drawing with fit/zoom/pan/crop and dithering.
 *
 * Decoding is done by the vendored stb_image.h (public domain / MIT) in this
 * single translation unit.  The API always works on RGBA8, which keeps the
 * scaler and canvas renderer simple; alpha is handled with premultiplied
 * interpolation so transparent edges do not bleed black.
 *
 * Header probing (PNG/JPEG/GIF/BMP/WebP) never decodes the pixels and is safe
 * on arbitrary input; every read is bounds checked against the caller size.
 *
 * SPDX-License-Identifier: MIT
 */

#include "render/image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#include "render/canvas.h"
#include "rolling_epub/repub_config.h"

#if REPUB_ENABLE_IMAGES
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#if REPUB_ENABLE_GIF
#define STBI_ONLY_GIF
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

namespace repub {

namespace {

constexpr int64_t kMaxImageDimension = 65535;

bool valid_dimensions(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return false;
    if (width > kMaxImageDimension || height > kMaxImageDimension) return false;
    return static_cast<int64_t>(width) * static_cast<int64_t>(height) <=
           static_cast<int64_t>(REPUB_MAX_IMAGE_PIXELS);
}

inline uint32_t read_be16u(const uint8_t* p) { return (uint32_t(p[0]) << 8) | p[1]; }
inline uint32_t read_be32u(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

int32_t read_le32i(const uint8_t* p) {
    return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (uint32_t(p[1]) << 8) |
                                (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24));
}

uint32_t read_le16u(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (uint32_t(p[1]) << 8); }

uint32_t read_le24u(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
}

/* ---- Header probing ---------------------------------------------------- */

repub_err_t probe_png(const uint8_t* data, size_t size, int32_t* width, int32_t* height) {
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 24) return REPUB_ERR_FORMAT;
    if (std::memcmp(data, kSig, 8) != 0) return REPUB_ERR_FORMAT;
    if (std::memcmp(data + 12, "IHDR", 4) != 0) return REPUB_ERR_FORMAT;
    const uint32_t w = read_be32u(data + 16);
    const uint32_t h = read_be32u(data + 20);
    if (w == 0 || h == 0 || w > static_cast<uint32_t>(kMaxImageDimension) ||
        h > static_cast<uint32_t>(kMaxImageDimension)) {
        return REPUB_ERR_LIMIT;
    }
    *width = static_cast<int32_t>(w);
    *height = static_cast<int32_t>(h);
    return REPUB_OK;
}

repub_err_t probe_gif(const uint8_t* data, size_t size, int32_t* width, int32_t* height) {
    if (size < 10) return REPUB_ERR_FORMAT;
    if (std::memcmp(data, "GIF87a", 6) != 0 && std::memcmp(data, "GIF89a", 6) != 0) {
        return REPUB_ERR_FORMAT;
    }
    const uint32_t w = read_le16u(data + 6);
    const uint32_t h = read_le16u(data + 8);
    if (w == 0 || h == 0) return REPUB_ERR_FORMAT;
    *width = static_cast<int32_t>(w);
    *height = static_cast<int32_t>(h);
    return REPUB_OK;
}

repub_err_t probe_bmp(const uint8_t* data, size_t size, int32_t* width, int32_t* height) {
    if (size < 26) return REPUB_ERR_FORMAT;
    if (data[0] != 'B' || data[1] != 'M') return REPUB_ERR_FORMAT;
    const int32_t w = read_le32i(data + 18);
    int32_t h = read_le32i(data + 22);
    if (h < 0) h = -h; /* top-down DIB */
    if (w <= 0 || h <= 0) return REPUB_ERR_FORMAT;
    if (w > static_cast<int32_t>(kMaxImageDimension) ||
        h > static_cast<int32_t>(kMaxImageDimension)) {
        return REPUB_ERR_LIMIT;
    }
    *width = w;
    *height = h;
    return REPUB_OK;
}

repub_err_t probe_jpeg(const uint8_t* data, size_t size, int32_t* width, int32_t* height) {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) return REPUB_ERR_FORMAT;
    size_t pos = 2;
    while (pos + 3 < size) {
        if (data[pos] != 0xFF) {
            ++pos;
            continue;
        }
        size_t marker_pos = pos;
        while (marker_pos < size && data[marker_pos] == 0xFF) ++marker_pos;
        if (marker_pos >= size) break;
        const uint8_t marker = data[marker_pos];
        if (marker == 0xD8 || marker == 0x01) {
            pos = marker_pos + 1;
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA) break; /* EOI / start of scan */
        if (marker_pos + 2 >= size) break;
        const uint32_t segment_len = read_be16u(data + marker_pos + 1);
        if (segment_len < 2) break;
        const bool is_sof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
                            marker != 0xC8 && marker != 0xCC;
        if (is_sof) {
            if (marker_pos + 8 >= size) return REPUB_ERR_FORMAT;
            const uint32_t h = read_be16u(data + marker_pos + 4);
            const uint32_t w = read_be16u(data + marker_pos + 6);
            if (w == 0 || h == 0) return REPUB_ERR_FORMAT;
            *width = static_cast<int32_t>(w);
            *height = static_cast<int32_t>(h);
            return REPUB_OK;
        }
        pos = marker_pos + 1 + segment_len;
    }
    return REPUB_ERR_FORMAT;
}

repub_err_t probe_webp(const uint8_t* data, size_t size, int32_t* width, int32_t* height) {
    if (size < 30) return REPUB_ERR_FORMAT;
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WEBP", 4) != 0) {
        return REPUB_ERR_FORMAT;
    }
    const uint8_t* chunk = data + 12;
    if (std::memcmp(chunk, "VP8 ", 4) == 0) {
        if (size < 30) return REPUB_ERR_FORMAT;
        const uint32_t w = read_le16u(data + 26) & 0x3FFFu;
        const uint32_t h = read_le16u(data + 28) & 0x3FFFu;
        if (w == 0 || h == 0) return REPUB_ERR_FORMAT;
        *width = static_cast<int32_t>(w);
        *height = static_cast<int32_t>(h);
        return REPUB_OK;
    }
    if (std::memcmp(chunk, "VP8L", 4) == 0) {
        if (size < 25) return REPUB_ERR_FORMAT;
        if (data[20] != 0x2F) return REPUB_ERR_FORMAT;
        const uint32_t bits = static_cast<uint32_t>(data[21]) |
                              (static_cast<uint32_t>(data[22]) << 8) |
                              (static_cast<uint32_t>(data[23]) << 16) |
                              (static_cast<uint32_t>(data[24]) << 24);
        const uint32_t w = (bits & 0x3FFFu) + 1;
        const uint32_t h = ((bits >> 14) & 0x3FFFu) + 1;
        *width = static_cast<int32_t>(w);
        *height = static_cast<int32_t>(h);
        return REPUB_OK;
    }
    if (std::memcmp(chunk, "VP8X", 4) == 0) {
        if (size < 30) return REPUB_ERR_FORMAT;
        const uint32_t w = read_le24u(data + 24) + 1;
        const uint32_t h = read_le24u(data + 27) + 1;
        if (w == 0 || h == 0) return REPUB_ERR_FORMAT;
        *width = static_cast<int32_t>(w);
        *height = static_cast<int32_t>(h);
        return REPUB_OK;
    }
    return REPUB_ERR_FORMAT;
}

}  // namespace

/* ---------------------------------------------------------------------- */
/* Images                                                                 */
/* ---------------------------------------------------------------------- */

repub_image* image_create(const repub_platform_t* platform, int32_t width, int32_t height,
                          repub_err_t* err) {
    if (err) *err = REPUB_OK;
    if (!valid_dimensions(width, height)) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
    repub_image* image = new (std::nothrow) repub_image();
    if (!image) {
        if (err) *err = REPUB_ERR_MEMORY;
        return nullptr;
    }
    image->platform = effective_platform(platform);
    image->width = width;
    image->height = height;
    try {
        image->rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
    } catch (...) {
        delete image;
        if (err) *err = REPUB_ERR_MEMORY;
        return nullptr;
    }
    return image;
}

void image_free(repub_image* image) { delete image; }

repub_err_t image_decode(const repub_platform_t* platform, const void* data, size_t size,
                         int32_t max_width, int32_t max_height, repub_image** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    if (!data || size == 0) return REPUB_ERR_INVALID_ARG;
#if !REPUB_ENABLE_IMAGES
    (void)platform;
    (void)max_width;
    (void)max_height;
    return REPUB_ERR_UNSUPPORTED;
#else
    if (size > static_cast<size_t>(REPUB_MAX_IMAGE_BYTES)) return REPUB_ERR_LIMIT;
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) return REPUB_ERR_LIMIT;

    int width = 0;
    int height = 0;
    int components = 0;
    if (!stbi_info_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &width,
                               &height, &components)) {
        return REPUB_ERR_FORMAT;
    }
    if (!valid_dimensions(width, height)) {
        return REPUB_ERR_LIMIT;
    }

    int target_width = width;
    int target_height = height;
    bool needs_scale = false;
    if (max_width > 0 || max_height > 0) {
        const double sx = max_width > 0 ? static_cast<double>(max_width) / width : 1.0;
        const double sy = max_height > 0 ? static_cast<double>(max_height) / height : 1.0;
        double factor = std::min(sx, sy);
        if (factor < 1.0) {
            target_width = std::max(1, static_cast<int>(std::lround(width * factor)));
            target_height = std::max(1, static_cast<int>(std::lround(height * factor)));
            needs_scale = target_width != width || target_height != height;
        }
    }

    int decoded_width = 0;
    int decoded_height = 0;
    int decoded_components = 0;
    stbi_uc* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data),
                                            static_cast<int>(size), &decoded_width,
                                            &decoded_height, &decoded_components, 4);
    if (!pixels) return REPUB_ERR_FORMAT;
    if (!valid_dimensions(decoded_width, decoded_height)) {
        stbi_image_free(pixels);
        return REPUB_ERR_LIMIT;
    }

    repub_err_t err = REPUB_OK;
    repub_image* image = image_create(platform, decoded_width, decoded_height, &err);
    if (!image) {
        stbi_image_free(pixels);
        return err == REPUB_OK ? REPUB_ERR_MEMORY : err;
    }
    std::memcpy(image->rgba.data(), pixels,
                static_cast<size_t>(decoded_width) * static_cast<size_t>(decoded_height) * 4u);
    stbi_image_free(pixels);

    if (needs_scale) {
        repub_image* scaled = nullptr;
        err = image_scale(platform, image, target_width, target_height, &scaled);
        image_free(image);
        if (err != REPUB_OK) return err;
        *out = scaled;
        return REPUB_OK;
    }
    *out = image;
    return REPUB_OK;
#endif
}

repub_err_t image_scale(const repub_platform_t* platform, const repub_image* src, int32_t width,
                        int32_t height, repub_image** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    if (!src || src->rgba.empty()) return REPUB_ERR_INVALID_ARG;
    if (!valid_dimensions(width, height)) return REPUB_ERR_INVALID_ARG;
    if (src->width <= 0 || src->height <= 0) return REPUB_ERR_INVALID_ARG;

    repub_err_t err = REPUB_OK;
    repub_image* dst = image_create(platform, width, height, &err);
    if (!dst) return err == REPUB_OK ? REPUB_ERR_MEMORY : err;

    const int32_t sw = src->width;
    const int32_t sh = src->height;
    const uint8_t* src_pixels = src->rgba.data();
    uint8_t* dst_pixels = dst->rgba.data();

    if (sw == width && sh == height) {
        std::memcpy(dst_pixels, src_pixels, src->rgba.size());
        *out = dst;
        return REPUB_OK;
    }

    /* Bilinear resample on premultiplied RGBA (enlargement and reduction). */
    for (int32_t y = 0; y < height; ++y) {
        const double fy = (static_cast<double>(y) + 0.5) * sh / height - 0.5;
        int32_t y0 = static_cast<int32_t>(std::floor(fy));
        double wy = fy - y0;
        if (y0 < 0) {
            y0 = 0;
            wy = 0.0;
        }
        int32_t y1 = std::min(sh - 1, y0 + 1);
        if (wy <= 0.0) {
            y1 = y0;
            wy = 0.0;
        }
        for (int32_t x = 0; x < width; ++x) {
            const double fx = (static_cast<double>(x) + 0.5) * sw / width - 0.5;
            int32_t x0 = static_cast<int32_t>(std::floor(fx));
            double wx = fx - x0;
            if (x0 < 0) {
                x0 = 0;
                wx = 0.0;
            }
            int32_t x1 = std::min(sw - 1, x0 + 1);
            if (wx <= 0.0) {
                x1 = x0;
                wx = 0.0;
            }
            const uint8_t* p00 = src_pixels + (static_cast<size_t>(y0) * sw + x0) * 4;
            const uint8_t* p10 = src_pixels + (static_cast<size_t>(y0) * sw + x1) * 4;
            const uint8_t* p01 = src_pixels + (static_cast<size_t>(y1) * sw + x0) * 4;
            const uint8_t* p11 = src_pixels + (static_cast<size_t>(y1) * sw + x1) * 4;

            const double w00 = (1.0 - wx) * (1.0 - wy);
            const double w10 = wx * (1.0 - wy);
            const double w01 = (1.0 - wx) * wy;
            const double w11 = wx * wy;

            double alpha = 0.0;
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;
            const uint8_t* samples[4] = {p00, p10, p01, p11};
            const double weights[4] = {w00, w10, w01, w11};
            for (int i = 0; i < 4; ++i) {
                const double a = samples[i][3] / 255.0;
                alpha += a * weights[i];
                red += samples[i][0] * a * weights[i];
                green += samples[i][1] * a * weights[i];
                blue += samples[i][2] * a * weights[i];
            }
            uint8_t out_alpha = static_cast<uint8_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0));
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (alpha > 0.0) {
                r = static_cast<uint8_t>(std::lround(std::clamp(red / alpha, 0.0, 255.0)));
                g = static_cast<uint8_t>(std::lround(std::clamp(green / alpha, 0.0, 255.0)));
                b = static_cast<uint8_t>(std::lround(std::clamp(blue / alpha, 0.0, 255.0)));
            }
            uint8_t* out_pixel = dst_pixels + (static_cast<size_t>(y) * width + x) * 4;
            out_pixel[0] = r;
            out_pixel[1] = g;
            out_pixel[2] = b;
            out_pixel[3] = out_alpha;
        }
    }
    *out = dst;
    return REPUB_OK;
}

repub_err_t image_probe(const void* data, size_t size, int32_t* width, int32_t* height) {
    if (!data || size == 0 || !width || !height) return REPUB_ERR_INVALID_ARG;
    *width = 0;
    *height = 0;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    if (size >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        return probe_png(bytes, size, width, height);
    }
    if (size >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
        return probe_bmp(bytes, size, width, height);
    }
    if (size >= 6 && std::memcmp(bytes, "GIF8", 4) == 0) {
        return probe_gif(bytes, size, width, height);
    }
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        return probe_jpeg(bytes, size, width, height);
    }
    if (size >= 12 && std::memcmp(bytes, "RIFF", 4) == 0 &&
        std::memcmp(bytes + 8, "WEBP", 4) == 0) {
        return probe_webp(bytes, size, width, height);
    }
    return REPUB_ERR_UNSUPPORTED;
}

bool image_format_supported(std::string_view path_or_href) {
    size_t dot = path_or_href.find_last_of('.');
    size_t slash = path_or_href.find_last_of("/\\");
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return false;
    }
    std::string_view ext = path_or_href.substr(dot + 1);
    std::string lowered;
    lowered.reserve(ext.size());
    for (char ch : ext) {
        lowered.push_back((ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch);
    }
    if (lowered == "png" || lowered == "jpg" || lowered == "jpeg" || lowered == "bmp") {
        return REPUB_ENABLE_IMAGES != 0;
    }
    if (lowered == "gif") {
        return REPUB_ENABLE_IMAGES != 0 && REPUB_ENABLE_GIF != 0;
    }
    if (lowered == "webp") {
        return REPUB_ENABLE_IMAGES != 0 && REPUB_ENABLE_WEBP != 0;
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Canvas drawing                                                         */
/* ---------------------------------------------------------------------- */

namespace {

struct RGBA {
    double r = 0;
    double g = 0;
    double b = 0;
    double a = 0;
};

RGBA sample_bilinear(const repub_image& image, double fx, double fy) {
    const double x = std::clamp(fx, 0.0, static_cast<double>(image.width) - 1.0);
    const double y = std::clamp(fy, 0.0, static_cast<double>(image.height) - 1.0);
    const int32_t x0 = static_cast<int32_t>(std::floor(x));
    const int32_t y0 = static_cast<int32_t>(std::floor(y));
    const int32_t x1 = std::min(image.width - 1, x0 + 1);
    const int32_t y1 = std::min(image.height - 1, y0 + 1);
    const double wx = x - x0;
    const double wy = y - y0;
    const uint8_t* p00 = image.rgba.data() + (static_cast<size_t>(y0) * image.width + x0) * 4;
    const uint8_t* p10 = image.rgba.data() + (static_cast<size_t>(y0) * image.width + x1) * 4;
    const uint8_t* p01 = image.rgba.data() + (static_cast<size_t>(y1) * image.width + x0) * 4;
    const uint8_t* p11 = image.rgba.data() + (static_cast<size_t>(y1) * image.width + x1) * 4;
    const double w00 = (1.0 - wx) * (1.0 - wy);
    const double w10 = wx * (1.0 - wy);
    const double w01 = (1.0 - wx) * wy;
    const double w11 = wx * wy;
    const uint8_t* samples[4] = {p00, p10, p01, p11};
    const double weights[4] = {w00, w10, w01, w11};
    double alpha = 0.0;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    for (int i = 0; i < 4; ++i) {
        const double a = samples[i][3] / 255.0;
        alpha += a * weights[i];
        red += samples[i][0] * a * weights[i];
        green += samples[i][1] * a * weights[i];
        blue += samples[i][2] * a * weights[i];
    }
    RGBA out;
    out.a = alpha;
    if (alpha > 0.0) {
        out.r = red / alpha;
        out.g = green / alpha;
        out.b = blue / alpha;
    }
    return out;
}

}  // namespace

repub_err_t canvas_draw_image(repub_canvas* canvas, const repub_image* image,
                              const repub_image_view* view) {
    if (!canvas || !image || !view) return REPUB_ERR_INVALID_ARG;
    if (image->width <= 0 || image->height <= 0 || image->rgba.empty()) {
        return REPUB_ERR_INVALID_ARG;
    }
    Rect dest = from_public(view->dest);
    if (dest.empty()) return REPUB_OK;
    if (canvas->width <= 0 || canvas->height <= 0) return REPUB_ERR_INVALID_ARG;

    double fit_scale = 1.0;
    switch (view->fit) {
        case REPUB_IMAGE_FIT_NONE:
            fit_scale = 1.0;
            break;
        case REPUB_IMAGE_FIT_COVER:
            fit_scale = std::max(static_cast<double>(dest.w) / image->width,
                                 static_cast<double>(dest.h) / image->height);
            break;
        case REPUB_IMAGE_FIT_CONTAIN:
        default:
            fit_scale = std::min(static_cast<double>(dest.w) / image->width,
                                 static_cast<double>(dest.h) / image->height);
            break;
    }
    double zoom = view->zoom;
    if (!(zoom > 0.0) || !std::isfinite(zoom)) zoom = 1.0;
    if (zoom > 64.0) zoom = 64.0;
    const double scale = fit_scale * zoom;

    int32_t draw_w = std::max(1, static_cast<int32_t>(std::lround(image->width * scale)));
    int32_t draw_h = std::max(1, static_cast<int32_t>(std::lround(image->height * scale)));
    int32_t draw_x = dest.x + (dest.w - draw_w) / 2;
    int32_t draw_y = dest.y + (dest.h - draw_h) / 2;

    /* Pan within the overflow of the fitted image (normalized -1..1). */
    double pan_x = view->pan_x;
    double pan_y = view->pan_y;
    if (!std::isfinite(pan_x)) pan_x = 0.0;
    if (!std::isfinite(pan_y)) pan_y = 0.0;
    pan_x = std::clamp(pan_x, -1.0, 1.0);
    pan_y = std::clamp(pan_y, -1.0, 1.0);
    const int32_t overflow_x = std::max(0, draw_w - dest.w);
    const int32_t overflow_y = std::max(0, draw_h - dest.h);
    draw_x -= static_cast<int32_t>(std::lround(pan_x * overflow_x * 0.5));
    draw_y -= static_cast<int32_t>(std::lround(pan_y * overflow_y * 0.5));

    /* Always crop to the destination rectangle, then to the canvas. */
    Rect clip = Rect{draw_x, draw_y, draw_w, draw_h}
                    .intersect(dest)
                    .intersect(Rect{0, 0, canvas->width, canvas->height});
    if (clip.empty()) return REPUB_OK;

    const double scale_x = static_cast<double>(draw_w) / image->width;
    const double scale_y = static_cast<double>(draw_h) / image->height;

    std::vector<uint8_t> gray(static_cast<size_t>(clip.w) * static_cast<size_t>(clip.h), 255);
    const int max_level = canvas->max_level;
    const int32_t background_level = view->background_level;

    for (int32_t y = 0; y < clip.h; ++y) {
        const int32_t canvas_y = clip.y + y;
        for (int32_t x = 0; x < clip.w; ++x) {
            const int32_t canvas_x = clip.x + x;
            const double fx = (static_cast<double>(canvas_x - draw_x) + 0.5) / scale_x - 0.5;
            const double fy = (static_cast<double>(canvas_y - draw_y) + 0.5) / scale_y - 0.5;
            const RGBA sample = sample_bilinear(*image, fx, fy);
            int32_t background_gray;
            if (background_level >= 0) {
                background_gray = (std::clamp(background_level, 0, max_level) * 255) / max_level;
            } else {
                background_gray = (canvas->get_pixel(canvas_x, canvas_y) * 255) / max_level;
            }
            const double luma =
                (0.299 * sample.r + 0.587 * sample.g + 0.114 * sample.b) / 255.0;
            const double alpha = std::clamp(sample.a, 0.0, 1.0);
            const double mixed = luma * alpha + (background_gray / 255.0) * (1.0 - alpha);
            gray[static_cast<size_t>(y) * static_cast<size_t>(clip.w) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(std::lround(std::clamp(mixed, 0.0, 1.0) * 255.0));
        }
    }

    repub_dither_t mode = view->dither;
    if (mode != REPUB_DITHER_THRESHOLD && mode != REPUB_DITHER_BAYER4 &&
        mode != REPUB_DITHER_BAYER8 && mode != REPUB_DITHER_FLOYD_STEINBERG) {
        mode = REPUB_DITHER_NONE;
    }
    dither_canvas_rect(canvas, clip, gray.data(), clip.w, clip.w, clip.h, mode, 128, -1);
    return REPUB_OK;
}

}  // namespace repub
