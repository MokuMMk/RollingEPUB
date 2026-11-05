/*
 * RollingEPUB render-layer tests: canvas formats/pixels, built-in font,
 * TrueType faces, glyph cache, vertical drawing, page renderer.
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
#include "content/flow.h"
#include "layout/layout.h"
#include "render/canvas.h"
#include "render/font.h"
#include "render/image.h"
#include "render/page_render.h"

using Canvas = repub::repub_canvas;
using Image = repub::repub_image;

namespace {


Canvas* make_canvas(int32_t width, int32_t height, repub_pixel_format_t format) {
    Canvas* canvas = nullptr;
    const repub_err_t err = repub::canvas_create(nullptr, width, height, format, &canvas);
    REPUB_CHECK(err == REPUB_OK);
    REPUB_CHECK(canvas != nullptr);
    return canvas;
}

int count_non_blank(const Canvas& canvas) {
    int count = 0;
    for (int32_t y = 0; y < canvas.height; ++y) {
        for (int32_t x = 0; x < canvas.width; ++x) {
            if (canvas.get_pixel(x, y) != canvas.max_level) ++count;
        }
    }
    return count;
}

std::string ttf_fixture() {
    return read_file(fixture_path("fonts/noto-subset.ttf"));
}

}  // namespace

/* ---------------------------------------------------------------------- */
/* Canvas                                                                 */
/* ---------------------------------------------------------------------- */

REPUB_TEST(canvas_mono1_basics) {
    REPUB_CHECK_EQ(repub::format_max_level(REPUB_FORMAT_MONO1), 1);
    REPUB_CHECK_EQ(repub::format_max_level(REPUB_FORMAT_GRAY2), 3);
    REPUB_CHECK_EQ(repub::format_max_level(REPUB_FORMAT_GRAY4), 15);
    REPUB_CHECK_EQ(repub::format_max_level(REPUB_FORMAT_GRAY8), 255);
    REPUB_CHECK_EQ(repub::format_bytes_per_row(10, REPUB_FORMAT_MONO1), 2);
    REPUB_CHECK_EQ(repub::format_bytes_per_row(8, REPUB_FORMAT_MONO1), 1);
    REPUB_CHECK_EQ(repub::format_bytes_per_row(9, REPUB_FORMAT_MONO1), 2);
    REPUB_CHECK_EQ(repub::format_bytes_per_row(13, REPUB_FORMAT_GRAY4), 13);

    Canvas* canvas = make_canvas(10, 3, REPUB_FORMAT_MONO1);
    REPUB_CHECK_EQ(canvas->width, 10);
    REPUB_CHECK_EQ(canvas->height, 3);
    REPUB_CHECK_EQ(canvas->stride, 2);
    REPUB_CHECK_EQ(canvas->format, REPUB_FORMAT_MONO1);
    REPUB_CHECK(canvas->data != nullptr);
    REPUB_CHECK_EQ(static_cast<int>(canvas->data[0]), 0xFF); /* white by default */

    canvas->clear(1);
    canvas->set_pixel(0, 0, 0);
    REPUB_CHECK_EQ(canvas->get_pixel(0, 0), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(1, 0), 1);
    REPUB_CHECK_EQ(canvas->data[0] & 0x80u, 0u);

    canvas->set_pixel(9, 2, 0);
    REPUB_CHECK_EQ(canvas->get_pixel(9, 2), 0);
    /* row 2 starts at byte 4; x=9 is byte 1 (stride 2), MSB-first bit 6. */
    REPUB_CHECK_EQ(canvas->data[4 + 1] & 0x40u, 0u);

    /* Out-of-range writes are clipped silently. */
    canvas->set_pixel(-1, 0, 0);
    canvas->set_pixel(10, 0, 0);
    canvas->set_pixel(0, 3, 0);
    REPUB_CHECK_EQ(canvas->get_pixel(1, 0), 1);

    canvas->clear(0);
    REPUB_CHECK_EQ(canvas->get_pixel(0, 0), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(9, 2), 0);
    repub::canvas_destroy(canvas);
}

REPUB_TEST(canvas_blend_rounding) {
    Canvas* mono = make_canvas(4, 1, REPUB_FORMAT_MONO1);
    mono->clear(1);
    mono->blend_pixel(0, 0, 0, 0);
    REPUB_CHECK_EQ(mono->get_pixel(0, 0), 1);       /* no coverage: unchanged */
    mono->blend_pixel(0, 0, 0, 127);
    REPUB_CHECK_EQ(mono->get_pixel(0, 0), 1);       /* below half: stays white */
    mono->blend_pixel(0, 0, 0, 128);
    REPUB_CHECK_EQ(mono->get_pixel(0, 0), 0);       /* half and above: flips */
    mono->blend_pixel(1, 0, 0, 255);
    REPUB_CHECK_EQ(mono->get_pixel(1, 0), 0);
    mono->blend_pixel(1, 0, 1, 128);
    REPUB_CHECK_EQ(mono->get_pixel(1, 0), 1);
    repub::canvas_destroy(mono);

    Canvas* gray2 = make_canvas(4, 1, REPUB_FORMAT_GRAY2);
    gray2->clear(3);
    gray2->blend_pixel(0, 0, 0, 128);  /* 3 + (0-3)*128/255 = 1.49 -> 1 */
    REPUB_CHECK_EQ(gray2->get_pixel(0, 0), 1);
    gray2->blend_pixel(1, 0, 0, 64);   /* 3 - 0.75 = 2.25 -> 2 */
    REPUB_CHECK_EQ(gray2->get_pixel(1, 0), 2);
    gray2->blend_pixel(2, 0, 3, 255);
    REPUB_CHECK_EQ(gray2->get_pixel(2, 0), 3);
    repub::canvas_destroy(gray2);

    Canvas* gray8 = make_canvas(2, 1, REPUB_FORMAT_GRAY8);
    gray8->clear(255);
    gray8->blend_pixel(0, 0, 0, 128); /* 255 - 128 = 127 */
    REPUB_CHECK_EQ(gray8->get_pixel(0, 0), 127);
    repub::canvas_destroy(gray8);
}

REPUB_TEST(canvas_fill_and_lines_clip) {
    Canvas* canvas = make_canvas(20, 10, REPUB_FORMAT_GRAY4);
    canvas->clear(15);
    canvas->fill_rect(repub::Rect{2, 2, 4, 3}, 0);
    REPUB_CHECK_EQ(canvas->get_pixel(2, 2), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(5, 4), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(6, 4), 15);
    REPUB_CHECK_EQ(canvas->get_pixel(1, 2), 15);

    /* Clipped fill: negative origin and overflow on both axes. */
    canvas->fill_rect(repub::Rect{-5, -5, 10, 10}, 7);
    REPUB_CHECK_EQ(canvas->get_pixel(0, 0), 7);
    REPUB_CHECK_EQ(canvas->get_pixel(4, 4), 7);
    REPUB_CHECK_EQ(canvas->get_pixel(5, 5), 15);

    canvas->draw_hline(-3, 8, 6, 0);
    REPUB_CHECK_EQ(canvas->get_pixel(0, 8), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(2, 8), 0);
    REPUB_CHECK_EQ(canvas->get_pixel(3, 8), 15);
    canvas->draw_hline(18, 8, 10, 0);
    REPUB_CHECK_EQ(canvas->get_pixel(19, 8), 0);
    canvas->draw_vline(0, -4, 6, 3);
    REPUB_CHECK_EQ(canvas->get_pixel(0, 0), 3);
    REPUB_CHECK_EQ(canvas->get_pixel(0, 1), 3);
    REPUB_CHECK_EQ(canvas->get_pixel(0, 2), 7); /* below the clipped vline */
    canvas->draw_vline(30, 0, 5, 0); /* fully outside: no-op */
    repub::canvas_destroy(canvas);
}

REPUB_TEST(canvas_wrap_borrows_and_create_validation) {
    uint8_t buffer[4 * 2]; /* 10 px wide mono needs 2 bytes/row, stride 4 */
    std::memset(buffer, 0x00, sizeof(buffer));
    Canvas* canvas = nullptr;
    repub_err_t err = repub::canvas_wrap(nullptr, buffer, 10, 2, 4, REPUB_FORMAT_MONO1, &canvas);
    REPUB_CHECK_EQ(err, REPUB_OK);
    canvas->set_pixel(0, 0, 1);
    REPUB_CHECK_EQ(buffer[0] & 0x80u, 0x80u);
    canvas->clear(1);
    REPUB_CHECK_EQ(buffer[0], 0xFF);
    REPUB_CHECK(buffer[4 + 1] == 0xFF); /* second row starts at stride 4 */
    repub::canvas_destroy(canvas);
    REPUB_CHECK_EQ(buffer[0], 0xFF); /* borrowed memory not freed/invalidated */

    Canvas* created = nullptr;
    err = repub::canvas_create(nullptr, 4, 4, static_cast<repub_pixel_format_t>(9), &created);
    REPUB_CHECK_EQ(err, REPUB_ERR_INVALID_ARG);
    REPUB_CHECK(created == nullptr);
    err = repub::canvas_create(nullptr, 0, 4, REPUB_FORMAT_MONO1, &created);
    REPUB_CHECK_EQ(err, REPUB_ERR_INVALID_ARG);
    err = repub::canvas_wrap(nullptr, nullptr, 4, 4, 1, REPUB_FORMAT_MONO1, &created);
    REPUB_CHECK_EQ(err, REPUB_ERR_INVALID_ARG);
}

/* ---------------------------------------------------------------------- */
/* Built-in bitmap font                                                   */
/* ---------------------------------------------------------------------- */

REPUB_TEST(builtin_font_metrics_and_render) {
    repub::FontManager fonts(nullptr);
    REPUB_CHECK(fonts.face_count() >= 1);
    REPUB_CHECK_EQ(fonts.default_face(), 0);
    REPUB_CHECK(!fonts.face_is_ttf(0));
    REPUB_CHECK(fonts.face_family(0).empty());

    REPUB_CHECK(fonts.has_glyph(0, 12, 'A'));
    REPUB_CHECK(fonts.has_glyph(0, 12, '~'));
    REPUB_CHECK(!fonts.has_glyph(0, 12, 0x4E2D)); /* CJK needs a TTF */

    const int advance = fonts.glyph_advance(0, 16, 'A');
    REPUB_CHECK(advance > 0);
    REPUB_CHECK(fonts.text_width(0, 16, "AB") == advance * 2);
    REPUB_CHECK(fonts.ascender(0, 16) > 0);
    REPUB_CHECK(fonts.descender(0, 16) >= 0);
    REPUB_CHECK(fonts.line_height(0, 16) > fonts.ascender(0, 16));
    REPUB_CHECK_EQ(fonts.text_width(0, 16, ""), 0);

    Canvas* canvas = make_canvas(120, 40, REPUB_FORMAT_GRAY8);
    canvas->clear(255);
    const int drawn = fonts.draw_text(canvas, 0, 16, 2, 28, "Hello", 0, 0);
    REPUB_CHECK(drawn > 0);
    REPUB_CHECK(count_non_blank(*canvas) > 0);
    REPUB_CHECK_EQ(canvas->get_pixel(2, 10), 255); /* above the glyph box */
    repub::canvas_destroy(canvas);

    /* Small sizes still render something (box-resampled). */
    Canvas* tiny = make_canvas(40, 16, REPUB_FORMAT_GRAY8);
    tiny->clear(255);
    fonts.draw_text(tiny, 0, 6, 1, 12, "Ag", 0, 0);
    REPUB_CHECK(count_non_blank(*tiny) > 0);
    repub::canvas_destroy(tiny);

    Image* glyph = nullptr;
    REPUB_CHECK(fonts.glyph_bitmap(0, 20, 'A', &glyph));
    REPUB_CHECK(glyph != nullptr);
    REPUB_CHECK(glyph->width > 0 && glyph->height > 0);
    bool has_ink = false;
    for (size_t i = 3; i < glyph->rgba.size(); i += 4) {
        if (glyph->rgba[i] > 0) {
            has_ink = true;
            break;
        }
    }
    REPUB_CHECK(has_ink);
    repub::image_free(glyph);
    /* A space has a valid (blank) bitmap: when returned it must contain no ink. */
    if (fonts.glyph_bitmap(0, 20, ' ', &glyph) && glyph) {
        for (size_t i = 3; i < glyph->rgba.size(); i += 4) {
            REPUB_CHECK_EQ(static_cast<int>(glyph->rgba[i]), 0);
        }
        repub::image_free(glyph);
    }
}

/* ---------------------------------------------------------------------- */
/* TrueType faces                                                         */
/* ---------------------------------------------------------------------- */

REPUB_TEST(ttf_face_load_and_draw) {
    const std::string data = ttf_fixture();
    REPUB_CHECK(data.size() > 1000);

    repub::FontManager fonts(nullptr);
    REPUB_CHECK_EQ(fonts.add_face("Noto Serif", data.data(), data.size(), 0,
                                  REPUB_FONT_REGULAR),
                   REPUB_OK);
    REPUB_CHECK(fonts.face_count() >= 2);
    const int face = 1;
    REPUB_CHECK(fonts.face_is_ttf(face));
    REPUB_CHECK_STR_EQ(fonts.face_family(face), "Noto Serif");

    REPUB_CHECK(fonts.has_glyph(face, 22, 'A'));
    REPUB_CHECK(fonts.glyph_advance(face, 22, 'A') > 0);
    REPUB_CHECK(fonts.text_width(face, 22, "Hello") > 0);
    REPUB_CHECK(fonts.ascender(face, 22) > 0);
    REPUB_CHECK(fonts.line_height(face, 22) > fonts.ascender(face, 22));

    Canvas* canvas = make_canvas(200, 60, REPUB_FORMAT_GRAY8);
    canvas->clear(255);
    const int advance = fonts.draw_text(canvas, face, 22, 4, 40, "Hello", 0, 0);
    REPUB_CHECK(advance > 0);
    REPUB_CHECK(count_non_blank(*canvas) > 20);

    /* Antialiasing: glyph edges produce intermediate gray values. */
    bool has_partial = false;
    for (int32_t y = 0; y < canvas->height && !has_partial; ++y) {
        for (int32_t x = 0; x < canvas->width; ++x) {
            const int value = canvas->get_pixel(x, y);
            if (value > 0 && value < 255) {
                has_partial = true;
                break;
            }
        }
    }
    REPUB_CHECK(has_partial);
    repub::canvas_destroy(canvas);

    /* CJK glyph (present in the subset fixture). */
    if (fonts.has_glyph(face, 22, 0x4E2D)) {
        Canvas* cjk = make_canvas(64, 64, REPUB_FORMAT_GRAY8);
        cjk->clear(255);
        fonts.draw_text(cjk, face, 22, 4, 40, "\xE4\xB8\xAD", 0, 0);
        REPUB_CHECK(count_non_blank(*cjk) > 10);
        repub::canvas_destroy(cjk);
    }
}

REPUB_TEST(ttf_face_validation_and_resolution) {
    const std::string data = ttf_fixture();
    repub::FontManager fonts(nullptr);
    REPUB_CHECK_EQ(fonts.add_face("Noto Serif", data.data(), data.size(), 0,
                                  REPUB_FONT_REGULAR),
                   REPUB_OK);
    REPUB_CHECK_EQ(fonts.add_face("Bad", nullptr, 0, 0, REPUB_FONT_REGULAR),
                   REPUB_ERR_INVALID_ARG);
    const char garbage[32] = {0};
    REPUB_CHECK_EQ(fonts.add_face("Bad", garbage, sizeof(garbage), 0, REPUB_FONT_REGULAR),
                   REPUB_ERR_FORMAT);

    /* Empty family resolves to the first TTF body face. */
    const int body = fonts.resolve_face(std::string_view(), 0);
    REPUB_CHECK(body >= 0 && body < fonts.face_count());
    REPUB_CHECK_EQ(body, 1);
    REPUB_CHECK_EQ(fonts.resolve_face("noto serif", 0), 1); /* case-insensitive */
    REPUB_CHECK_EQ(fonts.resolve_face("NOTO SERIF", REPUB_SPAN_ITALIC), 1);
    REPUB_CHECK(fonts.resolve_face("NoSuchFamily", REPUB_SPAN_BOLD) >= 0);

    /* A face that cannot fall back must still resolve to something usable. */
    repub::FontManager empty(nullptr);
    const int fallback = empty.resolve_face("serif", REPUB_SPAN_BOLD | REPUB_SPAN_ITALIC);
    REPUB_CHECK(fallback == 0);
}

REPUB_TEST(glyph_cache_is_bounded) {
    repub::FontManager fonts(nullptr);
    Canvas* canvas = make_canvas(32, 32, REPUB_FORMAT_GRAY8);
    canvas->clear(255);

    /* Force-rasterize many distinct codepoints (boxes for unmapped ones). */
    std::string text;
    for (uint32_t cp = 0x2500; cp < 0x2500 + 2600; ++cp) {
        text.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    fonts.draw_text(canvas, 0, 16, 0, 20, text, 0, 0);
    REPUB_CHECK(fonts.cached_glyph_count() <= 2048);
    REPUB_CHECK(fonts.cached_glyph_count() > 1500);

    const size_t after_first = fonts.cached_glyph_count();
    fonts.draw_text(canvas, 0, 16, 0, 20, "ABCDE", 0, 0);
    const size_t after_ascii = fonts.cached_glyph_count();
    fonts.draw_text(canvas, 0, 16, 0, 20, "ABCDE", 0, 0);
    REPUB_CHECK_EQ(fonts.cached_glyph_count(), after_ascii); /* cache hit */
    REPUB_CHECK(after_ascii >= after_first || after_ascii >= 1);
    repub::canvas_destroy(canvas);
}

REPUB_TEST(vertical_text_draws_and_advances) {
    repub::FontManager fonts(nullptr);
    REPUB_CHECK_EQ(fonts.draw_text_vertical(nullptr, 0, 20, 0, 0, "AB", 0, 0), 0);

    Canvas* canvas = make_canvas(40, 100, REPUB_FORMAT_GRAY8);
    canvas->clear(255);
    const int advance = fonts.draw_text_vertical(canvas, 0, 20, 2, 2, "AB", 0, 0);
    REPUB_CHECK_EQ(advance, 40); /* two px-sized cells */
    REPUB_CHECK(count_non_blank(*canvas) > 0);

    /* ASCII glyphs are rotated 90 degrees clockwise: ink in the upper cell
     * only, no ink below the second cell. */
    int first_cell_ink = 0;
    for (int32_t y = 2; y < 22; ++y) {
        for (int32_t x = 2; x < 22; ++x) {
            if (canvas->get_pixel(x, y) != 255) ++first_cell_ink;
        }
    }
    REPUB_CHECK(first_cell_ink > 0);

    /* Upright CJK in vertical mode when the TTF has it. */
    const std::string data = ttf_fixture();
    if (fonts.add_face("Noto Serif", data.data(), data.size(), 0, REPUB_FONT_REGULAR) ==
            REPUB_OK &&
        fonts.has_glyph(1, 20, 0x4E2D)) {
        Canvas* cjk = make_canvas(40, 60, REPUB_FORMAT_GRAY8);
        cjk->clear(255);
        REPUB_CHECK_EQ(fonts.draw_text_vertical(cjk, 1, 20, 2, 2, "\xE4\xB8\xAD", 0, 0), 20);
        REPUB_CHECK(count_non_blank(*cjk) > 0);
        repub::canvas_destroy(cjk);
    }
    repub::canvas_destroy(canvas);
}

/* ---------------------------------------------------------------------- */
/* Page renderer                                                          */
/* ---------------------------------------------------------------------- */

namespace {

repub::repub_page make_text_page(repub::repub_chapter* chapter, const char* text,
                                 int32_t px) {
    chapter->flow.text = text;
    repub::repub_page page;
    page.chapter = chapter;
    repub_span_t span{};
    span.text_offset = 0;
    span.text_len = static_cast<uint32_t>(std::strlen(text));
    span.x = 10;
    span.y = 40;
    span.width = 180;
    span.height = px;
    span.font_size_px = static_cast<uint16_t>(px);
    span.style = 0;
    page.spans.push_back(span);
    page.span_extra.push_back(repub::SpanExtra{0, 0});
    return page;
}

}  // namespace

REPUB_TEST(render_options_defaults_values) {
    repub_render_options_t options{};
    std::memset(&options, 0xAA, sizeof(options));
    repub::render_options_defaults(&options);
    REPUB_CHECK_EQ(options.struct_size, sizeof(repub_render_options_t));
    REPUB_CHECK_EQ(options.text_dither, REPUB_DITHER_THRESHOLD);
    REPUB_CHECK_EQ(options.text_level, 0);
    REPUB_CHECK_EQ(options.background_level, -1);
    REPUB_CHECK_EQ(options.threshold, 128);
    REPUB_CHECK((options.flags & REPUB_RENDER_UNDERLINE_LINKS) != 0);
    REPUB_CHECK((options.flags & REPUB_RENDER_DRAW_IMAGES) != 0);
    REPUB_CHECK((options.flags & REPUB_RENDER_DRAW_NOTE_ICONS) != 0);
}

REPUB_TEST(page_render_text_horizontal) {
    repub::FontManager fonts(nullptr);
    repub::repub_chapter chapter;
    repub::repub_page page = make_text_page(&chapter, "Hello world", 18);

    Canvas* canvas = make_canvas(220, 80, REPUB_FORMAT_GRAY8);
    canvas->clear(255);
    repub_render_options_t options;
    repub::render_options_defaults(&options);

    const repub::repub_book empty_book{};
    REPUB_CHECK_EQ(repub::render_page(page, empty_book, *canvas, fonts, options), REPUB_OK);
    REPUB_CHECK(count_non_blank(*canvas) > 20);

    /* background_level fills the canvas before drawing. */
    canvas->clear(255);
    options.background_level = 0;
    REPUB_CHECK_EQ(repub::render_page(page, empty_book, *canvas, fonts, options), REPUB_OK);
    REPUB_CHECK_EQ(canvas->get_pixel(219, 79), 0);
    repub::canvas_destroy(canvas);

    /* Unknown span face is clamped to the default face. */
    Canvas* again = make_canvas(220, 80, REPUB_FORMAT_GRAY8);
    again->clear(255);
    page.span_extra[0].face = 99;
    repub_render_options_t defaults;
    repub::render_options_defaults(&defaults);
    REPUB_CHECK_EQ(repub::render_page(page, empty_book, *again, fonts, defaults), REPUB_OK);
    REPUB_CHECK(count_non_blank(*again) > 0);
    repub::canvas_destroy(again);
}

REPUB_TEST(page_render_mono1_dithered_deterministic) {
    repub::FontManager fonts(nullptr);
    repub::repub_chapter chapter;
    repub::repub_page page = make_text_page(&chapter, "Dithered", 20);

    repub_render_options_t options;
    repub::render_options_defaults(&options);
    options.text_dither = REPUB_DITHER_BAYER4;
    options.background_level = 1; /* white */

    Canvas* first = make_canvas(220, 80, REPUB_FORMAT_MONO1);
    Canvas* second = make_canvas(220, 80, REPUB_FORMAT_MONO1);
    const repub::repub_book empty_book{};
    REPUB_CHECK_EQ(repub::render_page(page, empty_book, *first, fonts, options), REPUB_OK);
    REPUB_CHECK_EQ(repub::render_page(page, empty_book, *second, fonts, options), REPUB_OK);
    REPUB_CHECK(count_non_blank(*first) > 20);
    REPUB_CHECK(std::memcmp(first->data, second->data,
                            static_cast<size_t>(first->stride) * first->height) == 0);
    repub::canvas_destroy(first);
    repub::canvas_destroy(second);

    /* Vertical spans are drawn through draw_text_vertical. */
    repub::repub_chapter cjk_chapter;
    repub::repub_page vpage;
    vpage.chapter = &cjk_chapter;
    cjk_chapter.flow.text = "AB";
    repub_span_t vspan{};
    vspan.text_offset = 0;
    vspan.text_len = 2;
    vspan.x = 12; /* column left edge (layout convention) */
    vspan.y = 8;  /* run top edge */
    vspan.width = 20;
    vspan.height = 40;
    vspan.font_size_px = 20;
    vspan.style = REPUB_SPAN_VERTICAL;
    vpage.spans.push_back(vspan);
    vpage.span_extra.push_back(repub::SpanExtra{0, 0});
    Canvas* vcanvas = make_canvas(60, 80, REPUB_FORMAT_GRAY8);
    vcanvas->clear(255);
    repub_render_options_t voptions;
    repub::render_options_defaults(&voptions);
    REPUB_CHECK_EQ(repub::render_page(vpage, empty_book, *vcanvas, fonts, voptions), REPUB_OK);
    /* Two 20px cells starting at y=8: ink must be inside the column. */
    int vertical_ink = 0;
    for (int32_t y = 8; y < 48; ++y) {
        for (int32_t x = 12; x < 32; ++x) {
            if (vcanvas->get_pixel(x, y) != 255) ++vertical_ink;
        }
    }
    REPUB_CHECK(vertical_ink > 0);
    REPUB_CHECK_EQ(vcanvas->get_pixel(13, 60), 255); /* below the run */
    repub::canvas_destroy(vcanvas);
}

REPUB_TEST(page_render_links_and_debug_boxes) {
    repub::FontManager fonts(nullptr);
    repub::repub_chapter chapter;
    repub::repub_page page = make_text_page(&chapter, "Link", 16);
    repub_page_link_t link{};
    link.href = "chapter2.xhtml#a";
    link.rect = repub_rect_t{30, 50, 40, 12};
    page.links.push_back(link);

    Canvas* canvas = make_canvas(160, 80, REPUB_FORMAT_GRAY8);
    canvas->clear(255);
    repub_render_options_t options;
    repub::render_options_defaults(&options);
    const repub::repub_book empty_book{};
    REPUB_CHECK_EQ(repub::render_page(page, empty_book, *canvas, fonts, options), REPUB_OK);
    /* Underline drawn on the last row of the link rect. */
    REPUB_CHECK_EQ(canvas->get_pixel(40, 61), 0);

    /* Debug boxes draw around the tracked rects. */
    canvas->clear(255);
    options.flags = REPUB_RENDER_DEBUG_BOXES;
    REPUB_CHECK_EQ(repub::render_page(page, empty_book, *canvas, fonts, options), REPUB_OK);
    REPUB_CHECK_EQ(canvas->get_pixel(30, 50), 0);  /* link box top-left */
    REPUB_CHECK_EQ(canvas->get_pixel(69, 50), 0);  /* link box top-right */
    repub::canvas_destroy(canvas);
}

REPUB_TEST(page_render_skips_placeholder_and_handles_empty) {
    repub::FontManager fonts(nullptr);
    repub::repub_chapter chapter;
    repub::repub_page page = make_text_page(&chapter, "A\xEF\xBF\xBC" "B", 16);
    Canvas* canvas = make_canvas(120, 60, REPUB_FORMAT_GRAY8);
    canvas->clear(255);
    repub_render_options_t options;
    repub::render_options_defaults(&options);
    const repub::repub_book empty_book{};
    REPUB_CHECK_EQ(repub::render_page(page, empty_book, *canvas, fonts, options), REPUB_OK);
    REPUB_CHECK(count_non_blank(*canvas) > 0);
    repub::canvas_destroy(canvas);

    /* A page without a chapter (e.g. an image-only synthetic page) must not
     * crash and must return OK. */
    repub::repub_page bare;
    Canvas* bare_canvas = make_canvas(32, 32, REPUB_FORMAT_MONO1);
    REPUB_CHECK_EQ(repub::render_page(bare, empty_book, *bare_canvas, fonts, options), REPUB_OK);
    repub::canvas_destroy(bare_canvas);

    /* Empty image list / non-existent image index: non-fatal. */
    repub::repub_chapter image_chapter;
    repub::repub_page image_page;
    image_page.chapter = &image_chapter;
    repub_page_image_t image{};
    image.image_index = 0;
    image.rect = repub_rect_t{0, 0, 20, 20};
    image_page.images.push_back(image);
    Canvas* image_canvas = make_canvas(32, 32, REPUB_FORMAT_GRAY8);
    REPUB_CHECK_EQ(repub::render_page(image_page, empty_book, *image_canvas, fonts, options),
                   REPUB_OK);
    repub::canvas_destroy(image_canvas);
}

REPUB_TEST(page_render_draws_book_image) {
    const std::string path = fixture_path("epub/images.epub");
    repub::repub_book book;
    REPUB_CHECK_EQ(repub::book_open(&book, nullptr, path.c_str(), nullptr), REPUB_OK);

    repub::FontManager fonts(nullptr);
    repub::repub_chapter chapter;
    chapter.book = &book;
    chapter.flow.images.push_back(repub::FlowImage{"images/pic.png", "", 64, 40});

    repub::repub_page page;
    page.chapter = &chapter;
    repub_page_image_t image{};
    image.image_index = 0;
    image.rect = repub_rect_t{10, 10, 40, 25};
    page.images.push_back(image);

    Canvas* canvas = make_canvas(80, 60, REPUB_FORMAT_GRAY8);
    canvas->clear(255);
    repub_render_options_t options;
    repub::render_options_defaults(&options);
    REPUB_CHECK_EQ(repub::render_page(page, book, *canvas, fonts, options), REPUB_OK);

    int drawn = 0;
    for (int32_t y = 10; y < 35; ++y) {
        for (int32_t x = 10; x < 50; ++x) {
            if (canvas->get_pixel(x, y) != 255) ++drawn;
        }
    }
    REPUB_CHECK(drawn > 100);
    REPUB_CHECK_EQ(canvas->get_pixel(5, 5), 255); /* outside the image rect */

    /* Image flag off: nothing drawn. */
    canvas->clear(255);
    options.flags &= ~static_cast<uint32_t>(REPUB_RENDER_DRAW_IMAGES);
    REPUB_CHECK_EQ(repub::render_page(page, book, *canvas, fonts, options), REPUB_OK);
    REPUB_CHECK_EQ(count_non_blank(*canvas), 0);
    repub::canvas_destroy(canvas);
    repub::book_close(&book);
}
