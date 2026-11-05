/*
 * RollingEPUB image tests: decode PNG/JPEG/GIF from the fixture EPUB,
 * header probing, scaling, canvas draw (fit/zoom/pan/crop) and dithering.
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "test_harness.h"

#include "container/book.h"
#include "render/canvas.h"
#include "render/image.h"

using Canvas = repub::repub_canvas;
using Image = repub::repub_image;

namespace {


Canvas* make_canvas(int32_t width, int32_t height, repub_pixel_format_t format,
                          int32_t init_level) {
    Canvas* canvas = nullptr;
    const repub_err_t err = repub::canvas_create(nullptr, width, height, format, &canvas);
    REPUB_CHECK(err == REPUB_OK);
    REPUB_CHECK(canvas != nullptr);
    canvas->clear(init_level);
    return canvas;
}

Image* solid_image(int32_t width, int32_t height, uint8_t r, uint8_t g, uint8_t b,
                         uint8_t a) {
    repub_err_t err = REPUB_OK;
    Image* image = repub::image_create(nullptr, width, height, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(image != nullptr);
    for (size_t i = 0; i < image->rgba.size(); i += 4) {
        image->rgba[i + 0] = r;
        image->rgba[i + 1] = g;
        image->rgba[i + 2] = b;
        image->rgba[i + 3] = a;
    }
    return image;
}

struct BookFixture {
    repub::repub_book book;
    explicit BookFixture(const char* name) {
        const std::string path = fixture_path(name);
        const repub_err_t err = repub::book_open(&book, nullptr, path.c_str(), nullptr);
        REPUB_CHECK_EQ(err, REPUB_OK);
    }
    ~BookFixture() { repub::book_close(&book); }

    std::vector<uint8_t> read(const char* href) {
        std::vector<uint8_t> bytes;
        const repub_err_t err = repub::book_read_resource(book, href, &bytes);
        REPUB_CHECK_EQ(err, REPUB_OK);
        REPUB_CHECK(!bytes.empty());
        return bytes;
    }
};

}  // namespace

/* ---------------------------------------------------------------------- */
/* Decoding                                                               */
/* ---------------------------------------------------------------------- */

REPUB_TEST(image_decode_png_jpeg_gif) {
    BookFixture fixture("epub/images.epub");

    {
        const std::vector<uint8_t> bytes = fixture.read("images/pic.png");
        Image* image = nullptr;
        REPUB_CHECK_EQ(repub::image_decode(nullptr, bytes.data(), bytes.size(), 0, 0, &image),
                       REPUB_OK);
        REPUB_CHECK(image != nullptr);
        REPUB_CHECK_EQ(image->width, 64);
        REPUB_CHECK_EQ(image->height, 40);
        REPUB_CHECK_EQ(image->rgba.size(), static_cast<size_t>(64) * 40 * 4);
        REPUB_CHECK_EQ(image->rgba[0], 30);
        REPUB_CHECK_EQ(image->rgba[1], 60);
        REPUB_CHECK_EQ(image->rgba[2], 90);
        REPUB_CHECK_EQ(image->rgba[3], 255);
        repub::image_free(image);
    }

    {
        const std::vector<uint8_t> bytes = fixture.read("images/photo.jpg");
        Image* image = nullptr;
        REPUB_CHECK_EQ(repub::image_decode(nullptr, bytes.data(), bytes.size(), 0, 0, &image),
                       REPUB_OK);
        REPUB_CHECK_EQ(image->width, 80);
        REPUB_CHECK_EQ(image->height, 60);
        /* JPEG is lossy: allow a small per-channel tolerance. */
        REPUB_CHECK(std::abs(static_cast<int>(image->rgba[0]) - 0) <= 8);
        REPUB_CHECK(std::abs(static_cast<int>(image->rgba[1]) - 51) <= 8);
        REPUB_CHECK(std::abs(static_cast<int>(image->rgba[2]) - 196) <= 8);
        REPUB_CHECK_EQ(image->rgba[3], 255);
        repub::image_free(image);
    }

    {
        const std::vector<uint8_t> bytes = fixture.read("images/anim.gif");
        Image* image = nullptr;
        REPUB_CHECK_EQ(repub::image_decode(nullptr, bytes.data(), bytes.size(), 0, 0, &image),
                       REPUB_OK);
        REPUB_CHECK_EQ(image->width, 32);
        REPUB_CHECK_EQ(image->height, 32);
        repub::image_free(image);
    }
}

REPUB_TEST(image_decode_max_dimensions) {
    BookFixture fixture("epub/images.epub");
    const std::vector<uint8_t> bytes = fixture.read("images/pic.png");

    Image* image = nullptr;
    REPUB_CHECK_EQ(repub::image_decode(nullptr, bytes.data(), bytes.size(), 16, 16, &image),
                   REPUB_OK);
    REPUB_CHECK_EQ(image->width, 16);
    REPUB_CHECK_EQ(image->height, 10); /* aspect preserved: 64x40 -> 16x10 */
    repub::image_free(image);

    image = nullptr;
    REPUB_CHECK_EQ(repub::image_decode(nullptr, bytes.data(), bytes.size(), 10, 0, &image),
                   REPUB_OK);
    REPUB_CHECK_EQ(image->width, 10);
    REPUB_CHECK_EQ(image->height, 6); /* round(40 * 10 / 64) = round(6.25) */
    repub::image_free(image);

    /* No scaling when the requested box is larger than the image. */
    image = nullptr;
    REPUB_CHECK_EQ(repub::image_decode(nullptr, bytes.data(), bytes.size(), 1000, 1000, &image),
                   REPUB_OK);
    REPUB_CHECK_EQ(image->width, 64);
    REPUB_CHECK_EQ(image->height, 40);
    repub::image_free(image);
}

REPUB_TEST(image_decode_bounds_and_errors) {
    static const uint8_t tiny[4] = {0, 0, 0, 0};
    Image* image = nullptr;
    REPUB_CHECK_EQ(repub::image_decode(nullptr, tiny, sizeof(tiny), 0, 0, &image),
                   REPUB_ERR_FORMAT);
    REPUB_CHECK(image == nullptr);
    REPUB_CHECK_EQ(repub::image_decode(nullptr, nullptr, 10, 0, 0, &image),
                   REPUB_ERR_INVALID_ARG);
    /* Oversized declared payload is rejected before touching the buffer. */
    REPUB_CHECK_EQ(repub::image_decode(nullptr, tiny,
                                       static_cast<size_t>(REPUB_MAX_IMAGE_BYTES) + 1, 0, 0,
                                       &image),
                   REPUB_ERR_LIMIT);
}

REPUB_TEST(image_probe_headers) {
    BookFixture fixture("epub/images.epub");
    const std::vector<uint8_t> png = fixture.read("images/pic.png");
    const std::vector<uint8_t> jpg = fixture.read("images/photo.jpg");
    const std::vector<uint8_t> gif = fixture.read("images/anim.gif");

    int32_t width = 0;
    int32_t height = 0;
    REPUB_CHECK_EQ(repub::image_probe(png.data(), png.size(), &width, &height), REPUB_OK);
    REPUB_CHECK_EQ(width, 64);
    REPUB_CHECK_EQ(height, 40);

    REPUB_CHECK_EQ(repub::image_probe(jpg.data(), jpg.size(), &width, &height), REPUB_OK);
    REPUB_CHECK_EQ(width, 80);
    REPUB_CHECK_EQ(height, 60);

    REPUB_CHECK_EQ(repub::image_probe(gif.data(), gif.size(), &width, &height), REPUB_OK);
    REPUB_CHECK_EQ(width, 32);
    REPUB_CHECK_EQ(height, 32);

    /* Truncated PNG header: format error, never a crash. */
    REPUB_CHECK_EQ(repub::image_probe(png.data(), 10, &width, &height), REPUB_ERR_FORMAT);

    /* Minimal WebP VP8X header (header-only probing). */
    uint8_t webp[30] = {'R', 'I', 'F', 'F', 22, 0, 0, 0, 'W', 'E', 'B', 'P',
                        'V', 'P', '8', 'X', 10, 0, 0, 0, 0,   0, 0, 0,
                        31,  0,   0,   23, 0,   0};
    REPUB_CHECK_EQ(repub::image_probe(webp, sizeof(webp), &width, &height), REPUB_OK);
    REPUB_CHECK_EQ(width, 32);
    REPUB_CHECK_EQ(height, 24);

    REPUB_CHECK(repub::image_probe(nullptr, 0, &width, &height) == REPUB_ERR_INVALID_ARG);
    REPUB_CHECK(repub::image_format_supported("a/b/pic.PNG"));
    REPUB_CHECK(repub::image_format_supported("photo.jpeg"));
    REPUB_CHECK(repub::image_format_supported("anim.gif"));
    REPUB_CHECK(repub::image_format_supported("scan.bmp"));
    REPUB_CHECK(!repub::image_format_supported("notes.txt"));
    REPUB_CHECK(!repub::image_format_supported("noextension"));
}

/* ---------------------------------------------------------------------- */
/* Scaling                                                                */
/* ---------------------------------------------------------------------- */

REPUB_TEST(image_scale_up_and_down) {
    /* 4x4 quadrant image: TL red, TR green, BL blue, BR white. */
    Image* source = repub::image_create(nullptr, 4, 4, nullptr);
    REPUB_CHECK(source != nullptr);
    for (int32_t y = 0; y < 4; ++y) {
        for (int32_t x = 0; x < 4; ++x) {
            uint8_t* p = source->rgba.data() + (static_cast<size_t>(y) * 4 + x) * 4;
            const bool right = x >= 2;
            const bool bottom = y >= 2;
            p[0] = bottom ? (right ? 255 : 0) : (right ? 0 : 255);
            p[1] = bottom ? (right ? 255 : 0) : (right ? 255 : 0);
            p[2] = bottom ? 255 : 0;
            p[3] = 255;
        }
    }

    Image* upscaled = nullptr;
    REPUB_CHECK_EQ(repub::image_scale(nullptr, source, 8, 8, &upscaled), REPUB_OK);
    REPUB_CHECK_EQ(upscaled->width, 8);
    REPUB_CHECK_EQ(upscaled->height, 8);
    const uint8_t* tl = upscaled->rgba.data() + 0;
    REPUB_CHECK_EQ(tl[0], 255); /* red */
    REPUB_CHECK_EQ(tl[1], 0);
    const uint8_t* tr = upscaled->rgba.data() + (0 * 8 + 7) * 4;
    REPUB_CHECK_EQ(tr[0], 0); /* green */
    REPUB_CHECK_EQ(tr[1], 255);
    const uint8_t* bl = upscaled->rgba.data() + (7 * 8 + 0) * 4;
    REPUB_CHECK_EQ(bl[2], 255); /* blue */
    const uint8_t* br = upscaled->rgba.data() + (7 * 8 + 7) * 4;
    REPUB_CHECK_EQ(br[0], 255);
    REPUB_CHECK_EQ(br[1], 255);
    REPUB_CHECK_EQ(br[2], 255);
    REPUB_CHECK_EQ(br[3], 255);
    repub::image_free(upscaled);

    /* Downscale 4x4 -> 2x2: bilinear sampling at the pixel centres of each
     * 2x2 quadrant keeps the quadrant colours. */
    Image* downscaled = nullptr;
    REPUB_CHECK_EQ(repub::image_scale(nullptr, source, 2, 2, &downscaled), REPUB_OK);
    REPUB_CHECK_EQ(downscaled->width, 2);
    REPUB_CHECK_EQ(downscaled->height, 2);
    const uint8_t* mixed = downscaled->rgba.data();
    REPUB_CHECK_EQ(mixed[0], 255); /* TL: red */
    REPUB_CHECK_EQ(mixed[1], 0);
    REPUB_CHECK_EQ(mixed[4 + 0], 0); /* TR: green */
    REPUB_CHECK_EQ(mixed[4 + 1], 255);
    REPUB_CHECK_EQ(mixed[8 + 2], 255); /* BL: blue */
    REPUB_CHECK_EQ(mixed[12 + 0], 255); /* BR: white */
    REPUB_CHECK_EQ(mixed[12 + 1], 255);
    REPUB_CHECK_EQ(mixed[12 + 2], 255);
    REPUB_CHECK_EQ(mixed[3], 255);
    repub::image_free(downscaled);

    /* Same-size scaling copies pixels exactly. */
    Image* same = nullptr;
    REPUB_CHECK_EQ(repub::image_scale(nullptr, source, 4, 4, &same), REPUB_OK);
    REPUB_CHECK(same->rgba == source->rgba);
    repub::image_free(same);

    Image* invalid = nullptr;
    REPUB_CHECK(repub::image_scale(nullptr, source, 0, 4, &invalid) != REPUB_OK);
    REPUB_CHECK(repub::image_scale(nullptr, nullptr, 4, 4, &invalid) == REPUB_ERR_INVALID_ARG);
    REPUB_CHECK(invalid == nullptr);
    repub::image_free(source);
}

REPUB_TEST(image_scale_bilinear_checkerboard) {
    /* A 4x4 black/white checkerboard scaled to 2x2 must average each 2x2
     * source block to mid gray: this is what distinguishes true bilinear
     * downscaling from nearest/point sampling. */
    Image* source = repub::image_create(nullptr, 4, 4, nullptr);
    REPUB_CHECK(source != nullptr);
    for (int32_t y = 0; y < 4; ++y) {
        for (int32_t x = 0; x < 4; ++x) {
            const uint8_t value = ((x + y) & 1) ? 255 : 0;
            uint8_t* p = source->rgba.data() + (static_cast<size_t>(y) * 4 + x) * 4;
            p[0] = value;
            p[1] = value;
            p[2] = value;
            p[3] = 255;
        }
    }

    Image* scaled = nullptr;
    REPUB_CHECK_EQ(repub::image_scale(nullptr, source, 2, 2, &scaled), REPUB_OK);
    REPUB_CHECK_EQ(scaled->width, 2);
    REPUB_CHECK_EQ(scaled->height, 2);
    for (size_t i = 0; i < scaled->rgba.size(); i += 4) {
        REPUB_CHECK(scaled->rgba[i + 0] >= 126 && scaled->rgba[i + 0] <= 129);
        REPUB_CHECK_EQ(scaled->rgba[i + 0], scaled->rgba[i + 1]);
        REPUB_CHECK_EQ(scaled->rgba[i + 1], scaled->rgba[i + 2]);
        REPUB_CHECK_EQ(scaled->rgba[i + 3], 255);
    }

    /* 8x8 checkerboard -> 4x4 averages 2x2 blocks as well. */
    Image* large = repub::image_create(nullptr, 8, 8, nullptr);
    REPUB_CHECK(large != nullptr);
    for (int32_t y = 0; y < 8; ++y) {
        for (int32_t x = 0; x < 8; ++x) {
            const uint8_t value = ((x + y) & 1) ? 255 : 0;
            uint8_t* p = large->rgba.data() + (static_cast<size_t>(y) * 8 + x) * 4;
            p[0] = value;
            p[1] = value;
            p[2] = value;
            p[3] = 255;
        }
    }
    Image* half = nullptr;
    REPUB_CHECK_EQ(repub::image_scale(nullptr, large, 4, 4, &half), REPUB_OK);
    for (size_t i = 0; i < half->rgba.size(); i += 4) {
        REPUB_CHECK(half->rgba[i] >= 126 && half->rgba[i] <= 129);
    }
    repub::image_free(half);
    repub::image_free(large);
    repub::image_free(scaled);
    repub::image_free(source);
}

REPUB_TEST(image_scale_preserves_transparency) {
    Image* source = repub::image_create(nullptr, 2, 2, nullptr);
    REPUB_CHECK(source != nullptr);
    /* One fully transparent pixel, the rest opaque black. */
    source->rgba[3] = 0;
    for (int i = 4; i < 16; i += 4) {
        source->rgba[i + 3] = 255;
    }
    Image* scaled = nullptr;
    REPUB_CHECK_EQ(repub::image_scale(nullptr, source, 4, 4, &scaled), REPUB_OK);
    /* Bilinear enlargement blends the transparent corner with its opaque
     * neighbour, so the adjacent sample keeps a partial alpha instead of a
     * hard edge (nearest sampling would keep it fully opaque). */
    REPUB_CHECK(scaled->rgba[3] < 64);
    REPUB_CHECK(scaled->rgba[2 * 4 + 3] > 128);
    REPUB_CHECK(scaled->rgba[2 * 4 + 3] < 255);
    repub::image_free(scaled);
    repub::image_free(source);
}

/* ---------------------------------------------------------------------- */
/* Canvas drawing                                                         */
/* ---------------------------------------------------------------------- */

namespace {

int32_t count_dark(const Canvas& canvas, int32_t x0, int32_t y0, int32_t x1,
                   int32_t y1) {
    int32_t count = 0;
    for (int32_t y = y0; y < y1; ++y) {
        for (int32_t x = x0; x < x1; ++x) {
            if (canvas.get_pixel(x, y) < canvas.max_level) ++count;
        }
    }
    return count;
}

}  // namespace

REPUB_TEST(canvas_draw_image_fit_modes) {
    Image* image = solid_image(20, 10, 0, 0, 0, 255); /* opaque black */
    Canvas* canvas = make_canvas(100, 80, REPUB_FORMAT_GRAY8, 255);

    repub_image_view_t view{};
    view.dest = repub_rect_t{0, 0, 100, 80};
    view.zoom = 1.0f;
    view.fit = REPUB_IMAGE_FIT_CONTAIN;
    view.dither = REPUB_DITHER_NONE;
    view.background_level = -1;
    REPUB_CHECK_EQ(repub::canvas_draw_image(canvas, image, &view), REPUB_OK);
    REPUB_CHECK_EQ(canvas->get_pixel(50, 40), 0);  /* centered 100x50 */
    REPUB_CHECK_EQ(canvas->get_pixel(50, 5), 255); /* letterbox */
    REPUB_CHECK_EQ(canvas->get_pixel(0, 40), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(99, 40), 0);

    /* FIT_NONE: natural size, centered. */
    canvas->clear(255);
    view.fit = REPUB_IMAGE_FIT_NONE;
    REPUB_CHECK_EQ(repub::canvas_draw_image(canvas, image, &view), REPUB_OK);
    REPUB_CHECK_EQ(canvas->get_pixel(50, 40), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(10, 40), 255);
    REPUB_CHECK_EQ(canvas->get_pixel(40, 34), 255);
    REPUB_CHECK_EQ(canvas->get_pixel(40, 36), 0);

    /* FIT_COVER crops the overflow and fills the whole dest. */
    canvas->clear(255);
    view.fit = REPUB_IMAGE_FIT_COVER;
    REPUB_CHECK_EQ(repub::canvas_draw_image(canvas, image, &view), REPUB_OK);
    REPUB_CHECK_EQ(count_dark(*canvas, 0, 0, 100, 80), 100 * 80);

    /* Zoom 2 around the fit size also covers the dest. */
    canvas->clear(255);
    view.fit = REPUB_IMAGE_FIT_CONTAIN;
    view.zoom = 2.0f;
    REPUB_CHECK_EQ(repub::canvas_draw_image(canvas, image, &view), REPUB_OK);
    REPUB_CHECK_EQ(count_dark(*canvas, 0, 0, 100, 80), 100 * 80);

    /* Partially off-canvas dest clips safely. */
    canvas->clear(255);
    view.zoom = 1.0f;
    view.dest = repub_rect_t{-10, -10, 20, 20};
    REPUB_CHECK_EQ(repub::canvas_draw_image(canvas, image, &view), REPUB_OK);

    /* Invalid arguments. */
    REPUB_CHECK_EQ(repub::canvas_draw_image(nullptr, image, &view), REPUB_ERR_INVALID_ARG);
    REPUB_CHECK_EQ(repub::canvas_draw_image(canvas, nullptr, &view), REPUB_ERR_INVALID_ARG);

    repub::canvas_destroy(canvas);
    repub::image_free(image);
}

REPUB_TEST(canvas_draw_image_transparency) {
    Image* transparent = solid_image(10, 10, 0, 0, 0, 0);
    Canvas* canvas = make_canvas(40, 40, REPUB_FORMAT_GRAY8, 255);

    repub_image_view_t view{};
    view.dest = repub_rect_t{0, 0, 40, 40};
    view.zoom = 1.0f;
    view.fit = REPUB_IMAGE_FIT_CONTAIN;
    view.background_level = -1;
    REPUB_CHECK_EQ(repub::canvas_draw_image(canvas, transparent, &view), REPUB_OK);
    REPUB_CHECK_EQ(count_dark(*canvas, 0, 0, 40, 40), 0); /* fully transparent */

    /* Explicit background level is composited behind transparent pixels. */
    canvas->clear(255);
    view.background_level = 0;
    REPUB_CHECK_EQ(repub::canvas_draw_image(canvas, transparent, &view), REPUB_OK);
    REPUB_CHECK_EQ(count_dark(*canvas, 0, 0, 40, 40), 40 * 40);

    repub::canvas_destroy(canvas);
    repub::image_free(transparent);
}

REPUB_TEST(canvas_draw_image_dithering) {
    Image* mid_gray = solid_image(16, 16, 128, 128, 128, 255);

    repub_image_view_t view{};
    view.dest = repub_rect_t{0, 0, 16, 16};
    view.zoom = 1.0f;
    view.fit = REPUB_IMAGE_FIT_CONTAIN;
    view.dither = REPUB_DITHER_BAYER4;
    view.background_level = -1;

    Canvas* first = make_canvas(16, 16, REPUB_FORMAT_MONO1, 1);
    Canvas* second = make_canvas(16, 16, REPUB_FORMAT_MONO1, 1);
    REPUB_CHECK_EQ(repub::canvas_draw_image(first, mid_gray, &view), REPUB_OK);
    REPUB_CHECK_EQ(repub::canvas_draw_image(second, mid_gray, &view), REPUB_OK);

    int32_t black = 0;
    int32_t white = 0;
    for (int32_t y = 0; y < 16; ++y) {
        for (int32_t x = 0; x < 16; ++x) {
            if (first->get_pixel(x, y) == 0) {
                ++black;
            } else {
                ++white;
            }
        }
    }
    REPUB_CHECK(black > 0);
    REPUB_CHECK(white > 0);
    REPUB_CHECK(std::memcmp(first->data, second->data, 2 * 16) == 0);

    /* The 4x4 Bayer matrix must be visible in the pattern. */
    REPUB_CHECK_EQ(first->get_pixel(0, 0), 0);
    REPUB_CHECK_EQ(first->get_pixel(1, 0), 1);
    REPUB_CHECK_EQ(first->get_pixel(2, 0), 0);
    REPUB_CHECK_EQ(first->get_pixel(3, 0), 1);

    repub::canvas_destroy(first);
    repub::canvas_destroy(second);
    repub::image_free(mid_gray);
}

/* ---------------------------------------------------------------------- */
/* Dithering                                                              */
/* ---------------------------------------------------------------------- */

REPUB_TEST(dither_threshold_exact) {
    Canvas* canvas = make_canvas(4, 1, REPUB_FORMAT_MONO1, 1);
    const uint8_t gray[4] = {127, 128, 0, 255};
    repub::dither_canvas_rect(canvas, repub::Rect{0, 0, 4, 1}, gray, 4, 4, 1,
                              REPUB_DITHER_THRESHOLD, 128, -1);
    REPUB_CHECK_EQ(canvas->get_pixel(0, 0), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(1, 0), 1);
    REPUB_CHECK_EQ(canvas->get_pixel(2, 0), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(3, 0), 1);

    /* Custom threshold: 200 makes 150 black, 210 white. */
    const uint8_t gray2[2] = {150, 210};
    repub::dither_canvas_rect(canvas, repub::Rect{0, 0, 2, 1}, gray2, 2, 2, 1,
                              REPUB_DITHER_THRESHOLD, 200, -1);
    REPUB_CHECK_EQ(canvas->get_pixel(0, 0), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(1, 0), 1);
    repub::canvas_destroy(canvas);
}

REPUB_TEST(dither_bayer_patterns_exact) {
    /* 8x8 monochrome, mid gray 128.  With the default threshold, a Bayer cell
     * turns white when its ordered offset >= 127. */
    Canvas* canvas = make_canvas(8, 8, REPUB_FORMAT_MONO1, 0);
    uint8_t gray[64];
    std::memset(gray, 128, sizeof(gray));

    repub::dither_canvas_rect(canvas, repub::Rect{0, 0, 8, 8}, gray, 8, 8, 8,
                              REPUB_DITHER_BAYER4, 128, -1);
    const int expected_bayer4[4][8] = {
        {0, 1, 0, 1, 0, 1, 0, 1}, /* 0,8,2,10,0,8,2,10 -> >=8 white */
        {1, 0, 1, 0, 1, 0, 1, 0}, /* 12,4,14,6,... */
        {0, 1, 0, 1, 0, 1, 0, 1}, /* 3,11,1,9 -> >=8: 11,9 */
        {1, 0, 1, 0, 1, 0, 1, 0}, /* 15,7,13,5 -> 15,13 */
    };
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 8; ++x) {
            REPUB_CHECK_EQ(canvas->get_pixel(x, y), expected_bayer4[y][x]);
        }
    }
    repub::canvas_destroy(canvas);

    /* Gray2 levels for the same matrix are 1 or 2 for a 128 source. */
    Canvas* gray2 = make_canvas(4, 1, REPUB_FORMAT_GRAY2, 0);
    const uint8_t row[4] = {128, 128, 128, 128};
    repub::dither_canvas_rect(gray2, repub::Rect{0, 0, 4, 1}, row, 4, 4, 1,
                              REPUB_DITHER_BAYER4, 128, -1);
    REPUB_CHECK_EQ(gray2->get_pixel(0, 0), 1);
    REPUB_CHECK_EQ(gray2->get_pixel(1, 0), 2);
    REPUB_CHECK_EQ(gray2->get_pixel(2, 0), 1);
    REPUB_CHECK_EQ(gray2->get_pixel(3, 0), 2);
    repub::canvas_destroy(gray2);
}

REPUB_TEST(dither_floyd_steinberg_deterministic) {
    Canvas* first = make_canvas(16, 16, REPUB_FORMAT_MONO1, 1);
    Canvas* second = make_canvas(16, 16, REPUB_FORMAT_MONO1, 1);
    uint8_t gradient[16 * 16];
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            gradient[y * 16 + x] = static_cast<uint8_t>(x * 16);
        }
    }
    repub::dither_canvas_rect(first, repub::Rect{0, 0, 16, 16}, gradient, 16, 16, 16,
                              REPUB_DITHER_FLOYD_STEINBERG, 128, -1);
    repub::dither_canvas_rect(second, repub::Rect{0, 0, 16, 16}, gradient, 16, 16, 16,
                              REPUB_DITHER_FLOYD_STEINBERG, 128, -1);
    REPUB_CHECK(std::memcmp(first->data, second->data, 2 * 16) == 0);
    REPUB_CHECK(count_dark(*first, 0, 0, 16, 16) > 0);
    REPUB_CHECK(count_dark(*first, 0, 0, 16, 16) < 16 * 16);

    /* Background fill happens before dithering. */
    Canvas* background = make_canvas(4, 4, REPUB_FORMAT_MONO1, 1);
    const uint8_t white[16] = {255, 255, 255, 255, 255, 255, 255, 255,
                               255, 255, 255, 255, 255, 255, 255, 255};
    repub::dither_canvas_rect(background, repub::Rect{0, 0, 4, 4}, white, 4, 4, 4,
                              REPUB_DITHER_NONE, 128, 0);
    REPUB_CHECK_EQ(count_dark(*background, 0, 0, 4, 4), 0);
    repub::canvas_destroy(background);
    repub::canvas_destroy(first);
    repub::canvas_destroy(second);
}

REPUB_TEST(dither_clips_to_canvas) {
    Canvas* canvas = make_canvas(4, 4, REPUB_FORMAT_GRAY8, 255);
    uint8_t gray[8 * 8];
    std::memset(gray, 0, sizeof(gray));
    repub::dither_canvas_rect(canvas, repub::Rect{-4, -4, 8, 8}, gray, 8, 8, 8,
                              REPUB_DITHER_NONE, 128, -1);
    REPUB_CHECK_EQ(count_dark(*canvas, 0, 0, 4, 4), 16);

    /* Fully outside: no-op. */
    canvas->clear(255);
    repub::dither_canvas_rect(canvas, repub::Rect{100, 100, 4, 4}, gray, 8, 8, 8,
                              REPUB_DITHER_NONE, 128, -1);
    REPUB_CHECK_EQ(count_dark(*canvas, 0, 0, 4, 4), 0);
    repub::canvas_destroy(canvas);
}
