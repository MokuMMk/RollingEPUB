/*
 * Dedicated end-to-end test for the detailed wowaka fixture:
 * metadata/TOC, every image format, rich text features, mixed text/image
 * pages, image-only page, footnotes, ruby, vertical writing, bilinear
 * resampling and the popup/zoom helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "rolling_epub/repub.h"
#include "render/image.h"
#include "test_harness.h"

namespace {

constexpr const char* kBook = "epub/wowaka.epub";
constexpr const char* kFont = "fonts/noto-subset.ttf";

repub_layout_spec_t make_spec(bool vertical) {
    repub_layout_spec_t spec;
    repub_layout_spec_defaults(&spec);
    spec.page_rect = repub_rect_t{0, 0, 480, 800};
    spec.font_size_px = 21;
    spec.line_height_percent = 168;
    spec.paragraph_gap_percent = 60;
    spec.first_line_indent_em = 2;
    spec.justify = 1;
    spec.images_visible = 1;
    spec.hyphenation = 0;
    spec.writing_mode = vertical ? REPUB_WRITING_VERTICAL_RL : REPUB_WRITING_HORIZONTAL;
    return spec;
}

bool add_serif_font(repub_book_t* book) {
    const std::string ttf = read_file(fixture_path(kFont));
    if (ttf.empty()) return false;
    repub_font_desc_t desc{};
    desc.family = "serif";
    desc.data = ttf.data();
    desc.size = ttf.size();
    desc.face_index = 0;
    desc.style = REPUB_FONT_REGULAR;
    if (repub_book_add_font(book, &desc) != REPUB_OK) return false;
    return repub_book_set_body_font(book, "serif", REPUB_FONT_REGULAR) == REPUB_OK;
}

repub_book_t* open_wowaka(repub_platform_t* platform, bool with_font = true) {
    repub_err_t err = REPUB_OK;
    repub_book_t* book = repub_book_open(platform, fixture_path(kBook).c_str(), nullptr, &err);
    REPUB_CHECK(book != nullptr);
    if (with_font) REPUB_CHECK(add_serif_font(book));
    return book;
}

int count_non_white(repub_canvas_t* canvas) {
    const int w = repub_canvas_width(canvas);
    const int h = repub_canvas_height(canvas);
    const int stride = repub_canvas_stride(canvas);
    const uint8_t* px = static_cast<const uint8_t*>(repub_canvas_data(canvas));
    int count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (px[y * stride + x] != 255) ++count;
        }
    }
    return count;
}

}  // namespace

REPUB_TEST(wowaka_metadata_and_toc) {
    repub_platform_t platform = repub_platform_default();
    repub_book_t* book = open_wowaka(&platform);
    const repub_metadata_t* meta = repub_book_metadata(book);
    REPUB_CHECK(meta != nullptr);
    REPUB_CHECK(std::strstr(meta->title, "wowaka") != nullptr);
    REPUB_CHECK(std::strstr(meta->title, "噪音") != nullptr);
    REPUB_CHECK_STR_EQ(meta->language, "zh-CN");
    REPUB_CHECK_STR_EQ(meta->series, "音乐人档案");
    REPUB_CHECK_EQ(repub_book_spine_count(book), static_cast<size_t>(9));
    REPUB_CHECK(repub_book_toc_count(book) >= 12);
    REPUB_CHECK(std::strstr(repub_book_cover_href(book), "cover.jpg") != nullptr);

    int32_t spine = -1;
    char anchor[64] = {0};
    REPUB_CHECK_EQ(repub_book_resolve_href(book, "text/bio.xhtml#spring", &spine,
                                           anchor, sizeof(anchor)),
                   REPUB_OK);
    REPUB_CHECK_EQ(spine, 1);
    REPUB_CHECK_STR_EQ(anchor, "spring");
    REPUB_CHECK_EQ(repub_book_resolve_href(book, "text/bio.xhtml#turn", &spine, anchor,
                                           sizeof(anchor)),
                   REPUB_OK);
    REPUB_CHECK_STR_EQ(anchor, "turn");
    repub_book_close(book);
}

REPUB_TEST(wowaka_image_formats_and_dimensions) {
    repub_platform_t platform = repub_platform_default();
    repub_book_t* book = open_wowaka(&platform, false);
    struct Expected {
        const char* href;
        int32_t w;
        int32_t h;
    };
    const Expected expected[] = {
        {"images/cover.jpg", 600, 900},
        {"images/miku.png", 1200, 675},
        {"images/stage.jpg", 1200, 800},
        {"images/guitar.bmp", 800, 1200},
        {"images/tokyo.gif", 800, 510},
        {"images/logo.png", 960, 224},
        {"images/tall.png", 800, 2400},
        {"images/logo-icon.png", 48, 48},
    };
    for (const Expected& e : expected) {
        int32_t w = 0;
        int32_t h = 0;
        REPUB_CHECK_EQ(repub_book_image_dims(book, e.href, &w, &h), REPUB_OK);
        REPUB_CHECK_EQ(w, e.w);
        REPUB_CHECK_EQ(h, e.h);
        repub_image_t* image = nullptr;
        REPUB_CHECK_EQ(repub_book_image_decode(book, e.href, 0, 0, &image), REPUB_OK);
        REPUB_CHECK(image != nullptr);
        REPUB_CHECK_EQ(repub_image_width(image), e.w);
        REPUB_CHECK_EQ(repub_image_height(image), e.h);
        repub_image_free(image);
    }
    repub_book_close(book);
}

REPUB_TEST(wowaka_chapter_features) {
    repub_platform_t platform = repub_platform_default();
    repub_book_t* book = open_wowaka(&platform, false);

    /* bio.xhtml: h1-h6, table, quote, list, hr, footnote, anchors. */
    repub_chapter_t* bio = nullptr;
    REPUB_CHECK_EQ(repub_book_chapter_open(book, 1, &bio), REPUB_OK);
    size_t text_len = 0;
    const char* text = repub_chapter_text(bio, &text_len);
    REPUB_CHECK(text_len > 2000);
    REPUB_CHECK(std::strstr(text, "1987") != nullptr);
    REPUB_CHECK(std::strstr(text, "wowaka") != nullptr);
    REPUB_CHECK(std::strstr(text, "资料核对") == nullptr); /* hidden text */
    int headings[6] = {0};
    int table_rows = 0;
    int quotes = 0;
    int items = 0;
    bool has_hr = false;
    for (size_t i = 0; i < repub_chapter_block_count(bio); ++i) {
        const repub_block_info_t* b = repub_chapter_block(bio, i);
        if (b->type == REPUB_BLOCK_HEADING && b->heading_level >= 1 && b->heading_level <= 6) {
            headings[b->heading_level - 1]++;
        }
        if (b->type == REPUB_BLOCK_TABLE_ROW) ++table_rows;
        if (b->type == REPUB_BLOCK_BLOCKQUOTE) ++quotes;
        if (b->type == REPUB_BLOCK_LIST_ITEM) ++items;
        if (b->type == REPUB_BLOCK_HR) has_hr = true;
    }
    for (int level = 0; level < 6; ++level) REPUB_CHECK(headings[level] >= 1);
    REPUB_CHECK(table_rows >= 8);
    REPUB_CHECK(quotes >= 1);
    REPUB_CHECK(items >= 3);
    REPUB_CHECK(has_hr);
    REPUB_CHECK(repub_chapter_note_count(bio) >= 1);
    REPUB_CHECK(std::strstr(repub_chapter_note(bio, 0)->text, "原创纪念性文字") != nullptr);
    uint32_t offset = 0;
    REPUB_CHECK_EQ(repub_chapter_anchor_offset(bio, "youth", &offset), REPUB_OK);
    REPUB_CHECK(offset > 0);
    /* A second drop cap / inline verse is exercised by the parser. */
    repub_chapter_free(bio);

    /* vocaloid.xhtml: works table, ruby title runs, same-file footnote. */
    repub_chapter_t* vocaloid = nullptr;
    REPUB_CHECK_EQ(repub_book_chapter_open(book, 2, &vocaloid), REPUB_OK);
    int works_rows = 0;
    for (size_t i = 0; i < repub_chapter_block_count(vocaloid); ++i) {
        if (repub_chapter_block(vocaloid, i)->type == REPUB_BLOCK_TABLE_ROW) ++works_rows;
    }
    REPUB_CHECK(works_rows >= 3);
    REPUB_CHECK(repub_chapter_note_count(vocaloid) >= 1);
    repub_chapter_free(vocaloid);

    /* hitorie.xhtml: members list, cross-file footnote, images. */
    repub_chapter_t* hitorie = nullptr;
    REPUB_CHECK_EQ(repub_book_chapter_open(book, 3, &hitorie), REPUB_OK);
    REPUB_CHECK(repub_chapter_image_count(hitorie) >= 2);
    REPUB_CHECK(repub_chapter_note_count(hitorie) >= 1);
    REPUB_CHECK(std::strstr(repub_chapter_note(hitorie, 0)->text, "hitorie 于 2011") != nullptr);
    const char* htext = repub_chapter_text(hitorie, &text_len);
    REPUB_CHECK(std::strstr(htext, "イガラシ") != nullptr);
    REPUB_CHECK(std::strstr(htext, "シノダ") != nullptr);
    repub_chapter_free(hitorie);

    /* sound.xhtml: pre block, sup/sub, album table, BMP image. */
    repub_chapter_t* sound = nullptr;
    REPUB_CHECK_EQ(repub_book_chapter_open(book, 4, &sound), REPUB_OK);
    int pres = 0;
    int albums = 0;
    for (size_t i = 0; i < repub_chapter_block_count(sound); ++i) {
        const repub_block_info_t* b = repub_chapter_block(sound, i);
        if (b->type == REPUB_BLOCK_PRE) ++pres;
        if (b->type == REPUB_BLOCK_TABLE_ROW) ++albums;
    }
    REPUB_CHECK(pres >= 1);
    REPUB_CHECK(albums >= 4);
    REPUB_CHECK(repub_chapter_image_count(sound) >= 1);
    REPUB_CHECK(repub_chapter_note_count(sound) >= 1);
    repub_chapter_free(sound);

    /* images.xhtml: five illustrations + icon-marker footnote. */
    repub_chapter_t* gallery = nullptr;
    REPUB_CHECK_EQ(repub_book_chapter_open(book, 6, &gallery), REPUB_OK);
    REPUB_CHECK(repub_chapter_image_count(gallery) >= 5);
    bool icon_marker = false;
    for (size_t i = 0; i < repub_chapter_note_count(gallery); ++i) {
        if (repub_chapter_note(gallery, i)->marker_image_index >= 0) icon_marker = true;
    }
    REPUB_CHECK(icon_marker);
    repub_chapter_free(gallery);

    /* vertical.xhtml: ruby base kept, rt dropped. */
    repub_chapter_t* vertical = nullptr;
    REPUB_CHECK_EQ(repub_book_chapter_open(book, 7, &vertical), REPUB_OK);
    const char* vtext = repub_chapter_text(vertical, &text_len);
    REPUB_CHECK(std::strstr(vtext, "竖排") != nullptr);
    REPUB_CHECK(std::strstr(vtext, "声音") != nullptr);
    REPUB_CHECK(std::strstr(vtext, "sound") == nullptr);
    repub_chapter_free(vertical);

    repub_book_close(book);
}

REPUB_TEST(wowaka_mixed_text_image_pages) {
    repub_platform_t platform = repub_platform_default();
    repub_book_t* book = open_wowaka(&platform);
    repub_layout_spec_t spec = make_spec(false);
    repub_reader_t* reader = repub_reader_attach(book, &spec, nullptr);
    REPUB_CHECK(reader != nullptr);
    REPUB_CHECK_EQ(repub_reader_goto_chapter(reader, 6), REPUB_OK);
    const repub_layout_t* layout = repub_reader_layout(reader);
    REPUB_CHECK(layout != nullptr);
    REPUB_CHECK(repub_layout_page_count(layout) >= 5);

    int mixed_pages = 0;
    int image_only_pages = 0;
    size_t first_mixed = SIZE_MAX;
    size_t first_image_only = SIZE_MAX;
    for (size_t p = 0; p < repub_layout_page_count(layout); ++p) {
        const repub_page_t* page = repub_layout_page(layout, p);
        const repub_page_info_t* info = repub_page_info(page);
        if (info->image_count > 0 && info->span_count > 0) {
            ++mixed_pages;
            if (first_mixed == SIZE_MAX) first_mixed = p;
        }
        if (info->primary_image_index >= 0) {
            ++image_only_pages;
            if (first_image_only == SIZE_MAX) first_image_only = p;
        }
    }
    REPUB_CHECK(mixed_pages >= 2);
    REPUB_CHECK(image_only_pages >= 1);

    repub_err_t err = REPUB_OK;
    repub_canvas_t* canvas =
        repub_canvas_create(&platform, 480, 800, REPUB_FORMAT_GRAY4, &err);
    REPUB_CHECK(canvas != nullptr);
    repub_render_options_t options;
    repub_render_options_defaults(&options);
    options.background_level = 15;
    for (size_t p : {first_mixed, first_image_only}) {
        repub_canvas_clear(canvas, 15);
        REPUB_CHECK_EQ(repub_layout_render(layout, p, canvas, &options), REPUB_OK);
        REPUB_CHECK(count_non_white(canvas) > 2000);
        const repub_page_t* page = repub_layout_page(layout, p);
        const repub_page_info_t* info = repub_page_info(page);
        REPUB_CHECK(info->image_count >= 1);
        const repub_page_image_t* image = repub_page_image_at(page, 0);
        REPUB_CHECK(image->rect.w > 0 && image->rect.h > 0);
        REPUB_CHECK(image->rect.x >= 0 && image->rect.y >= 0);
        REPUB_CHECK(image->rect.x + image->rect.w <= 480);
        REPUB_CHECK(image->rect.y + image->rect.h <= 800);
    }
    repub_canvas_destroy(canvas);
    repub_reader_close(reader);
}

REPUB_TEST(wowaka_footnote_popup_and_image_zoom) {
    repub_platform_t platform = repub_platform_default();
    repub_book_t* book = open_wowaka(&platform);
    repub_layout_spec_t spec = make_spec(false);
    repub_reader_t* reader = repub_reader_attach(book, &spec, nullptr);
    REPUB_CHECK(reader != nullptr);

    /* Same-file footnote from the biography chapter. */
    REPUB_CHECK_EQ(repub_reader_goto_chapter(reader, 1), REPUB_OK);
    const repub_chapter_t* chapter = repub_reader_chapter(reader);
    REPUB_CHECK(repub_chapter_note_count(chapter) >= 1);
    repub_layout_spec_t popup = make_spec(false);
    popup.page_rect = repub_rect_t{24, 24, 432, 600};
    popup.font_size_px = 18;
    repub_err_t err = REPUB_OK;
    repub_note_popup_t* note = repub_reader_note_popup_open(reader, 0, &popup, &err);
    REPUB_CHECK(note != nullptr);
    REPUB_CHECK(repub_note_popup_page_count(note) >= 1);
    repub_canvas_t* canvas =
        repub_canvas_create(&platform, 480, 800, REPUB_FORMAT_GRAY4, &err);
    repub_render_options_t options;
    repub_render_options_defaults(&options);
    options.background_level = 15;
    repub_canvas_clear(canvas, 15);
    REPUB_CHECK_EQ(repub_note_popup_render(note, 0, canvas, &options), REPUB_OK);
    REPUB_CHECK(count_non_white(canvas) > 500);
    repub_note_popup_free(note);

    /* Image viewer over the tall illustration (bilinear zoom). */
    REPUB_CHECK_EQ(repub_reader_goto_chapter(reader, 6), REPUB_OK);
    const repub_layout_t* layout = repub_reader_layout(reader);
    int32_t image_index = -1;
    for (size_t p = 0; p < repub_layout_page_count(layout) && image_index < 0; ++p) {
        const repub_page_t* page = repub_layout_page(layout, p);
        if (repub_page_info(page)->image_count > 0) {
            image_index = repub_page_image_at(page, 0)->image_index;
        }
    }
    REPUB_CHECK(image_index >= 0);
    repub_image_viewer_t* viewer =
        repub_reader_image_viewer_open(reader, static_cast<size_t>(image_index), &err);
    REPUB_CHECK(viewer != nullptr);
    repub_canvas_clear(canvas, 15);
    repub_rect_t dest = {0, 0, 480, 800};
    REPUB_CHECK_EQ(repub_image_viewer_render(viewer, canvas, dest, 2.0f, 0.0f, 0.0f, &options),
                   REPUB_OK);
    REPUB_CHECK(count_non_white(canvas) > 10000);
    repub_image_viewer_free(viewer);
    repub_canvas_destroy(canvas);
    repub_reader_close(reader);
}

REPUB_TEST(wowaka_bilinear_resampling) {
    repub_platform_t platform = repub_platform_default();
    repub_book_t* book = open_wowaka(&platform, false);

    /* A synthetic black/white checkerboard proves that both reduction and
     * enlargement interpolate instead of point-sampling. */
    repub_image_t* source = repub_image_create(&platform, 4, 4, nullptr);
    REPUB_CHECK(source != nullptr);
    auto* source_impl = reinterpret_cast<repub::repub_image*>(source);
    for (int32_t y = 0; y < 4; ++y) {
        for (int32_t x = 0; x < 4; ++x) {
            const uint8_t value = ((x + y) & 1) ? 255 : 0;
            uint8_t* p = source_impl->rgba.data() + (static_cast<size_t>(y) * 4 + x) * 4;
            p[0] = p[1] = p[2] = value;
            p[3] = 255;
        }
    }
    repub_image_t* small = nullptr;
    REPUB_CHECK_EQ(repub_image_scale(&platform, source, 2, 2, &small), REPUB_OK);
    const uint8_t* mixed = repub_image_pixels(small);
    REPUB_CHECK(mixed[0] > 100 && mixed[0] < 155);
    repub_image_free(small);

    repub_image_t* big = nullptr;
    REPUB_CHECK_EQ(repub_image_scale(&platform, source, 16, 16, &big), REPUB_OK);
    const uint8_t* up = repub_image_pixels(big);
    int blended = 0;
    for (int i = 0; i < 16 * 16; ++i) {
        const int v = up[i * 4];
        if (v > 24 && v < 232) ++blended;
    }
    REPUB_CHECK(blended > 40);
    repub_image_free(big);
    repub_image_free(source);

    /* The real photographs must resample through the canvas without failure. */
    repub_image_t* photo = nullptr;
    REPUB_CHECK_EQ(repub_book_image_decode(book, "images/miku.png", 600, 338, &photo),
                   REPUB_OK);
    repub_canvas_t* canvas = nullptr;
    repub_err_t err = REPUB_OK;
    canvas = repub_canvas_create(&platform, 480, 800, REPUB_FORMAT_GRAY8, &err);
    REPUB_CHECK(canvas != nullptr);
    repub_canvas_clear(canvas, 255);
    repub_image_view_t view{};
    view.struct_size = sizeof(view);
    view.dest = repub_rect_t{0, 0, 480, 270};
    view.fit = REPUB_IMAGE_FIT_CONTAIN;
    view.zoom = 2.0f;
    view.dither = REPUB_DITHER_NONE;
    view.background_level = 255;
    REPUB_CHECK_EQ(repub_canvas_draw_image(canvas, photo, &view), REPUB_OK);
    REPUB_CHECK(count_non_white(canvas) > 10000);
    repub_canvas_destroy(canvas);
    repub_image_free(photo);
    repub_book_close(book);
}

REPUB_TEST(wowaka_vertical_layout) {
    repub_platform_t platform = repub_platform_default();
    repub_book_t* book = open_wowaka(&platform);
    repub_layout_spec_t spec = make_spec(true);
    repub_reader_t* reader = repub_reader_attach(book, &spec, nullptr);
    REPUB_CHECK(reader != nullptr);
    REPUB_CHECK_EQ(repub_reader_goto_chapter(reader, 7), REPUB_OK);
    const repub_layout_t* layout = repub_reader_layout(reader);
    REPUB_CHECK(layout != nullptr);
    REPUB_CHECK(repub_layout_page_count(layout) >= 1);

    const repub_page_t* page = repub_layout_page(layout, 0);
    const repub_page_info_t* info = repub_page_info(page);
    REPUB_CHECK(info->span_count >= 4);

    bool saw_vertical = false;
    int heading_pitch = 0;
    int body_pitch = 0;
    int previous_line = -1;
    int previous_column_left = 0;
    int previous_column_pitch = 0;
    for (size_t i = 0; i < info->span_count; ++i) {
        const repub_span_t* span = repub_page_span(page, i);
        if (span->text_len == 0) continue;
        REPUB_CHECK((span->style & REPUB_SPAN_VERTICAL) != 0);
        saw_vertical = true;
        if (span->line_index != previous_line) {
            if (previous_line >= 0) {
                /* Current (left) column must end before the previous column. */
                REPUB_CHECK(span->x + span->width <= previous_column_left);
            }
            previous_line = span->line_index;
            previous_column_left = span->x;
            previous_column_pitch = span->width;
        }
        if (span->font_size_px >= 32) heading_pitch = span->width;
        else if (span->font_size_px <= 22) body_pitch = span->width;
        REPUB_CHECK(span->x >= 0 && span->x + span->width <= 480);
        REPUB_CHECK(span->y >= 0 && span->y <= 800);
    }
    REPUB_CHECK(saw_vertical);
    REPUB_CHECK(heading_pitch > body_pitch);
    REPUB_CHECK(previous_column_left >= 0);
    REPUB_CHECK(previous_column_pitch > 0);

    repub_err_t err = REPUB_OK;
    repub_canvas_t* canvas =
        repub_canvas_create(&platform, 480, 800, REPUB_FORMAT_GRAY4, &err);
    REPUB_CHECK(canvas != nullptr);
    repub_render_options_t options;
    repub_render_options_defaults(&options);
    options.background_level = 15;
    repub_canvas_clear(canvas, 15);
    REPUB_CHECK_EQ(repub_layout_render(layout, 0, canvas, &options), REPUB_OK);
    REPUB_CHECK(count_non_white(canvas) > 3000);
    repub_canvas_destroy(canvas);
    repub_reader_close(reader);
}
