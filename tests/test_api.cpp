/*
 * RollingEPUB public API tests: C API wrappers, reader navigation, footnote
 * popups, image viewer, layout/render and the C++17 facade.
 *
 * Tests are written to be resilient: optional features (TOC, notes, images)
 * are skipped when the fixture does not contain them.
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rolling_epub/repub.h"
#include "rolling_epub/rolling_epub.hpp"
#include "test_harness.h"

namespace {

const char* kMainFixture = "readpico_trail-marker.epub";

repub_open_options_t open_options() {
    repub_open_options_t options{};
    options.struct_size = sizeof(options);
    options.enable_css = 1;
    return options;
}

repub_layout_spec_t make_spec(int32_t width, int32_t height, int32_t font_size) {
    repub_layout_spec_t spec{};
    repub_layout_spec_defaults(&spec);
    spec.page_rect = repub_rect_t{0, 0, width, height};
    spec.font_size_px = font_size;
    spec.line_height_percent = 160;
    return spec;
}

/* 1x1 transparent PNG used for dependency-free image decoder tests. */
const uint8_t kOnePixelPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

size_t count_black_bits(const repub_canvas_t* canvas) {
    const uint8_t* data = static_cast<const uint8_t*>(repub_canvas_data(canvas));
    if (!data) return 0;
    const int32_t stride = repub_canvas_stride(canvas);
    const int32_t height = repub_canvas_height(canvas);
    size_t black = 0;
    for (int32_t y = 0; y < height; ++y) {
        const uint8_t* row = data + static_cast<size_t>(y) * stride;
        for (int32_t x = 0; x < repub_canvas_width(canvas); ++x) {
            if (((row[x / 8] >> (7 - (x % 8))) & 1u) == 0) ++black;
        }
    }
    return black;
}

size_t count_non_white_gray(const repub_canvas_t* canvas) {
    const uint8_t* data = static_cast<const uint8_t*>(repub_canvas_data(canvas));
    if (!data) return 0;
    const int32_t stride = repub_canvas_stride(canvas);
    const int32_t height = repub_canvas_height(canvas);
    const int32_t width = repub_canvas_width(canvas);
    size_t non_white = 0;
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            if (data[static_cast<size_t>(y) * stride + x] < 250) ++non_white;
        }
    }
    return non_white;
}

}  // namespace

/* ------------------------------------------------------------------------- */
/* Defaults, build info, canvas and image wrappers                           */
/* ------------------------------------------------------------------------- */

REPUB_TEST(api_build_info_and_defaults) {
    const repub_build_info_t* info = repub_build_info();
    REPUB_CHECK(info != nullptr);
    REPUB_CHECK(info->abi_version == REPUB_ABI_VERSION);
    REPUB_CHECK(info->version != nullptr);
    REPUB_CHECK(std::strcmp(info->version, REPUB_VERSION_STRING) == 0);
    REPUB_CHECK(info->platform != nullptr);
    REPUB_CHECK(repub_feature_enabled(REPUB_FEATURE_IMAGES) == 1);

    REPUB_CHECK_STR_EQ(repub_err_str(REPUB_OK), "ok");
    REPUB_CHECK(std::strstr(repub_err_str(REPUB_ERR_INVALID_ARG), "invalid") != nullptr);
    REPUB_CHECK(repub_err_str(REPUB_ERR_NOT_FOUND) != nullptr);

    repub_layout_spec_t spec{};
    spec.page_rect = repub_rect_t{1, 2, 300, 400};
    repub_layout_spec_defaults(&spec);
    REPUB_CHECK_EQ(spec.struct_size, sizeof(repub_layout_spec_t));
    REPUB_CHECK_EQ(spec.page_rect.w, 300);
    REPUB_CHECK_EQ(spec.page_rect.h, 400);
    REPUB_CHECK_EQ(spec.font_size_px, 22);
    REPUB_CHECK_EQ(spec.line_height_percent, 160);
    REPUB_CHECK_EQ(spec.paragraph_gap_percent, 60);
    REPUB_CHECK_EQ(spec.first_line_indent_em, 2);
    REPUB_CHECK_EQ(spec.images_visible, 1);
    REPUB_CHECK_EQ(spec.writing_mode, REPUB_WRITING_HORIZONTAL);

    repub_layout_spec_t null_spec{};
    repub_layout_spec_defaults(&null_spec);
    REPUB_CHECK_EQ(null_spec.font_size_px, 22);

    repub_render_options_t render_options{};
    repub_render_options_defaults(&render_options);
    REPUB_CHECK_EQ(render_options.struct_size, sizeof(repub_render_options_t));
    REPUB_CHECK_EQ(render_options.background_level, -1);
    REPUB_CHECK_EQ(render_options.text_level, 0);
    REPUB_CHECK_EQ(render_options.threshold, 128);
    REPUB_CHECK((render_options.flags & REPUB_RENDER_DRAW_IMAGES) != 0);

    /* NULL platform means the built-in stdio/malloc platform. */
    void* memory = repub_alloc(nullptr, 128);
    REPUB_CHECK(memory != nullptr);
    memory = repub_realloc(nullptr, memory, 256);
    REPUB_CHECK(memory != nullptr);
    repub_free(nullptr, memory);
    repub_free(nullptr, nullptr);
}

REPUB_TEST(api_canvas_wrappers) {
    repub_err_t err = REPUB_OK;
    repub_canvas_t* canvas =
        repub_canvas_create(nullptr, 16, 8, REPUB_FORMAT_GRAY8, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(canvas != nullptr);
    REPUB_CHECK_EQ(repub_canvas_width(canvas), 16);
    REPUB_CHECK_EQ(repub_canvas_height(canvas), 8);
    REPUB_CHECK_EQ(repub_canvas_format(canvas), REPUB_FORMAT_GRAY8);
    REPUB_CHECK(repub_canvas_data(canvas) != nullptr);
    REPUB_CHECK(repub_canvas_stride(canvas) >= 16);

    repub_canvas_clear(canvas, 255);
    repub_canvas_set_pixel(canvas, 3, 2, 0);
    repub_canvas_set_pixel(canvas, -1, 0, 0); /* ignored */
    repub_canvas_set_pixel(canvas, 100, 100, 0); /* ignored */
    const uint8_t* data = static_cast<const uint8_t*>(repub_canvas_data(canvas));
    REPUB_CHECK_EQ(static_cast<int>(data[2 * repub_canvas_stride(canvas) + 3]), 0);
    REPUB_CHECK_EQ(static_cast<int>(data[0]), 255);

    repub_canvas_fill_rect(canvas, repub_rect_t{0, 0, 2, 2}, 0);
    REPUB_CHECK_EQ(static_cast<int>(data[0]), 0);
    repub_canvas_destroy(canvas);

    /* MONO1 packed pixels, MSB first, 1 = white. */
    repub_canvas_t* mono =
        repub_canvas_create(nullptr, 8, 2, REPUB_FORMAT_MONO1, &err);
    REPUB_CHECK(mono != nullptr);
    repub_canvas_clear(mono, 1);
    const uint8_t* mono_data = static_cast<const uint8_t*>(repub_canvas_data(mono));
    REPUB_CHECK_EQ(static_cast<int>(mono_data[0]), 0xFF);
    repub_canvas_set_pixel(mono, 0, 0, 0);
    REPUB_CHECK_EQ(static_cast<int>(mono_data[0]), 0x7F);
    repub_canvas_destroy(mono);

    /* Wrapping an external buffer must not free it. */
    std::vector<uint8_t> buffer(4 * 4, 0xAA);
    repub_canvas_t* wrapped = repub_canvas_wrap(
        nullptr, buffer.data(), 4, 4, 4, REPUB_FORMAT_GRAY8, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(wrapped != nullptr);
    REPUB_CHECK(repub_canvas_data(wrapped) == buffer.data());
    repub_canvas_destroy(wrapped);
    REPUB_CHECK_EQ(static_cast<int>(buffer[0]), 0xAA);

    /* Invalid arguments return an error and NULL. */
    err = REPUB_OK;
    REPUB_CHECK(repub_canvas_create(nullptr, 0, 10, REPUB_FORMAT_MONO1, &err) == nullptr);
    REPUB_CHECK_EQ(err, REPUB_ERR_INVALID_ARG);
    REPUB_CHECK_EQ(repub_canvas_width(nullptr), 0);
    REPUB_CHECK_EQ(repub_canvas_height(nullptr), 0);
    repub_canvas_destroy(nullptr);
}

REPUB_TEST(api_image_wrappers) {
    int32_t width = 0;
    int32_t height = 0;
    REPUB_CHECK_EQ(repub_image_probe(kOnePixelPng, sizeof(kOnePixelPng), &width,
                                     &height),
                   REPUB_OK);
    REPUB_CHECK_EQ(width, 1);
    REPUB_CHECK_EQ(height, 1);

    repub_err_t err = REPUB_OK;
    repub_image_t* decoded = nullptr;
    REPUB_CHECK_EQ(repub_image_decode(nullptr, kOnePixelPng, sizeof(kOnePixelPng),
                                      0, 0, &decoded),
                   REPUB_OK);
    REPUB_CHECK(decoded != nullptr);
    REPUB_CHECK_EQ(repub_image_width(decoded), 1);
    REPUB_CHECK_EQ(repub_image_height(decoded), 1);
    REPUB_CHECK(repub_image_pixels(decoded) != nullptr);

    repub_image_t* scaled = nullptr;
    REPUB_CHECK_EQ(repub_image_scale(nullptr, decoded, 4, 3, &scaled), REPUB_OK);
    REPUB_CHECK_EQ(repub_image_width(scaled), 4);
    REPUB_CHECK_EQ(repub_image_height(scaled), 3);

    repub_image_t* created = repub_image_create(nullptr, 2, 2, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(created != nullptr);
    const uint8_t* pixels = repub_image_pixels(created);
    REPUB_CHECK(pixels != nullptr);
    for (int i = 0; i < 16; ++i) REPUB_CHECK_EQ(static_cast<int>(pixels[i]), 0);

    repub_image_free(decoded);
    repub_image_free(scaled);
    repub_image_free(created);
    repub_image_free(nullptr);
    REPUB_CHECK(repub_image_width(nullptr) == 0);

    /* Invalid input must not crash. */
    REPUB_CHECK_EQ(repub_image_decode(nullptr, "not an image", 12, 0, 0, &decoded),
                   REPUB_ERR_FORMAT);
    REPUB_CHECK(decoded == nullptr);
}

/* ------------------------------------------------------------------------- */
/* Book / chapter / layout                                                   */
/* ------------------------------------------------------------------------- */

REPUB_TEST(api_open_book_metadata) {
    const std::string path = fixture_path(kMainFixture);
    repub_err_t err = REPUB_ERR_INTERNAL;
    repub_open_options_t options = open_options();
    repub_book_t* book = repub_book_open(nullptr, path.c_str(), &options, &err);
    REPUB_CHECK(book != nullptr);
    REPUB_CHECK_EQ(err, REPUB_OK);

    REPUB_CHECK(repub_book_platform(book) != nullptr);
    REPUB_CHECK(repub_book_path(book) != nullptr);
    REPUB_CHECK_STR_EQ(repub_book_path(book), path);

    const repub_metadata_t* metadata = repub_book_metadata(book);
    REPUB_CHECK(metadata != nullptr);
    REPUB_CHECK(metadata->title != nullptr);
    REPUB_CHECK(repub_book_spine_count(book) > 0);

    const repub_spine_item_t* item = repub_book_spine_item(book, 0);
    REPUB_CHECK(item != nullptr);
    REPUB_CHECK(item->href != nullptr);
    REPUB_CHECK(repub_book_spine_item(book, 100000) == nullptr);

    /* TOC may be absent in minimal fixtures; when present it must be sane. */
    const size_t toc_count = repub_book_toc_count(book);
    if (toc_count > 0) {
        const repub_toc_item_t* toc = repub_book_toc_item(book, 0);
        REPUB_CHECK(toc != nullptr);
        REPUB_CHECK(toc->title != nullptr);
        REPUB_CHECK(repub_book_toc_item(book, toc_count + 10) == nullptr);
    }

    /* NULL book accessors are safe. */
    REPUB_CHECK(repub_book_metadata(nullptr) == nullptr);
    REPUB_CHECK_EQ(repub_book_spine_count(nullptr), 0);
    REPUB_CHECK(repub_book_cover_href(nullptr) == nullptr);
    repub_err_t bad = REPUB_OK;
    REPUB_CHECK(repub_book_open(nullptr, "does/not/exist.epub", nullptr, &bad) == nullptr);
    REPUB_CHECK(bad != REPUB_OK);

    repub_book_close(book);
    repub_book_close(nullptr);
}

REPUB_TEST(api_chapter_open_and_text) {
    const std::string path = fixture_path(kMainFixture);
    repub_err_t err = REPUB_OK;
    repub_open_options_t options = open_options();
    repub_book_t* book = repub_book_open(nullptr, path.c_str(), &options, &err);
    REPUB_CHECK(book != nullptr);

    repub_chapter_t* chapter = nullptr;
    REPUB_CHECK_EQ(repub_book_chapter_open(book, 0, &chapter), REPUB_OK);
    REPUB_CHECK(chapter != nullptr);

    size_t len = 0;
    const char* text = repub_chapter_text(chapter, &len);
    REPUB_CHECK(text != nullptr);
    REPUB_CHECK(len > 0);
    REPUB_CHECK_EQ(std::strlen(text), len);

    REPUB_CHECK(repub_chapter_block_count(chapter) > 0);
    REPUB_CHECK(repub_chapter_block(chapter, 0) != nullptr);
    REPUB_CHECK(repub_chapter_block(chapter, repub_chapter_block_count(chapter) + 5) ==
                nullptr);

    /* The first block must lie inside the text. */
    const repub_block_info_t* block = repub_chapter_block(chapter, 0);
    REPUB_CHECK(block->text_offset + block->text_len <= len);

    REPUB_CHECK(repub_chapter_title(chapter) != nullptr);
    REPUB_CHECK_EQ(repub_chapter_spine_index(chapter), 0);

    uint32_t anchor_offset = 123;
    const repub_err_t anchor_rc =
        repub_chapter_anchor_offset(chapter, "definitely-not-an-anchor", &anchor_offset);
    REPUB_CHECK_EQ(anchor_rc, REPUB_ERR_NOT_FOUND);
    REPUB_CHECK_EQ(repub_chapter_anchor_offset(chapter, nullptr, &anchor_offset),
                   REPUB_ERR_INVALID_ARG);

    /* Out of range spine item. */
    repub_chapter_t* missing = nullptr;
    REPUB_CHECK_EQ(repub_book_chapter_open(book, 100000, &missing), REPUB_ERR_NOT_FOUND);
    REPUB_CHECK(missing == nullptr);
    REPUB_CHECK_EQ(repub_book_chapter_open(nullptr, 0, &missing), REPUB_ERR_INVALID_ARG);

    repub_chapter_free(chapter);
    repub_chapter_free(nullptr);

    /* Chapter accessors are NULL safe. */
    REPUB_CHECK(repub_chapter_title(nullptr) != nullptr);
    REPUB_CHECK_EQ(repub_chapter_block_count(nullptr), 0);
    REPUB_CHECK(repub_chapter_block(nullptr, 0) == nullptr);
    REPUB_CHECK_EQ(repub_chapter_note_count(nullptr), 0);

    repub_book_close(book);
}

REPUB_TEST(api_layout_create_and_pages) {
    const std::string path = fixture_path(kMainFixture);
    repub_err_t err = REPUB_OK;
    repub_book_t* book = repub_book_open(nullptr, path.c_str(), nullptr, &err);
    REPUB_CHECK(book != nullptr);
    repub_chapter_t* chapter = nullptr;
    REPUB_CHECK_EQ(repub_book_chapter_open(book, 0, &chapter), REPUB_OK);

    repub_layout_spec_t spec = make_spec(320, 480, 20);
    repub_layout_t* layout = nullptr;
    REPUB_CHECK_EQ(repub_layout_create(chapter, &spec, &layout), REPUB_OK);
    REPUB_CHECK(layout != nullptr);
    REPUB_CHECK_EQ(repub_layout_chapter(layout), chapter);
    REPUB_CHECK(repub_layout_spec(layout) != nullptr);
    REPUB_CHECK(repub_layout_page_count(layout) > 0);

    const repub_page_t* first = repub_layout_page(layout, 0);
    REPUB_CHECK(first != nullptr);
    const repub_page_info_t* info = repub_page_info(first);
    REPUB_CHECK(info != nullptr);
    REPUB_CHECK_EQ(info->start_offset, 0u);
    REPUB_CHECK(info->width > 0);
    REPUB_CHECK(info->height > 0);
    REPUB_CHECK(repub_layout_page(layout, repub_layout_page_count(layout)) == nullptr);
    REPUB_CHECK_EQ(repub_layout_page_start_offset(layout, 0), 0u);
    REPUB_CHECK_EQ(repub_layout_page_for_offset(layout, 0), 0u);
    REPUB_CHECK(repub_layout_page_for_offset(layout, 0xFFFFFFFFu) <
                repub_layout_page_count(layout));

    /* Page text must fit the page's byte range. */
    const uint32_t text_len = static_cast<uint32_t>(repub_chapter_text(chapter, nullptr) != nullptr
                                                        ? std::strlen(repub_chapter_text(chapter, nullptr))
                                                        : 0);
    REPUB_CHECK(info->end_offset <= text_len);
    std::vector<char> buffer(64, '\0');
    repub_page_text(first, buffer.data(), buffer.size());
    REPUB_CHECK(std::strlen(buffer.data()) <= buffer.size() - 1);

    /* Spec struct_size handling: too small is rejected. */
    repub_layout_spec_t broken = spec;
    broken.struct_size = 3;
    repub_layout_t* bad_layout = nullptr;
    REPUB_CHECK_EQ(repub_layout_create(chapter, &broken, &bad_layout),
                   REPUB_ERR_INVALID_ARG);
    REPUB_CHECK(bad_layout == nullptr);

    /* Missing page_rect is rejected. */
    repub_layout_spec_t no_rect{};
    repub_layout_spec_defaults(&no_rect);
    REPUB_CHECK_EQ(repub_layout_create(chapter, &no_rect, &bad_layout),
                   REPUB_ERR_INVALID_ARG);

    /* Ad-hoc text layout. */
    repub_layout_t* text_layout = nullptr;
    REPUB_CHECK_EQ(repub_book_layout_text(book, "Hello popup\nSecond line", 25, &spec,
                                          &text_layout),
                   REPUB_OK);
    REPUB_CHECK(text_layout != nullptr);
    REPUB_CHECK(repub_layout_page_count(text_layout) > 0);
    const repub_chapter_t* synthetic = repub_layout_chapter(text_layout);
    REPUB_CHECK(synthetic != nullptr);
    REPUB_CHECK(std::strstr(repub_chapter_text(synthetic, nullptr), "Hello popup") != nullptr);

    repub_layout_free(text_layout);
    repub_layout_free(layout);
    repub_layout_free(nullptr);
    repub_chapter_free(chapter);
    repub_book_close(book);
}

/* ------------------------------------------------------------------------- */
/* Reader: pagination, rendering, navigation, progress                       */
/* ------------------------------------------------------------------------- */

REPUB_TEST(api_reader_render_and_navigate) {
    const std::string path = fixture_path(kMainFixture);
    repub_layout_spec_t spec = make_spec(480, 800, 24);
    repub_err_t err = REPUB_ERR_INTERNAL;
    repub_reader_t* reader = repub_reader_open(nullptr, path.c_str(), &spec, nullptr, &err);
    REPUB_CHECK(reader != nullptr);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(repub_reader_book(reader) != nullptr);
    REPUB_CHECK_EQ(repub_reader_chapter_index(reader), 0);
    REPUB_CHECK(repub_reader_chapter_count(reader) > 0);
    REPUB_CHECK(repub_reader_page_count(reader) > 0);
    REPUB_CHECK_EQ(repub_reader_page_index(reader), 0);
    REPUB_CHECK(repub_reader_page(reader) != nullptr);
    REPUB_CHECK(repub_reader_layout(reader) != nullptr);
    REPUB_CHECK(repub_reader_chapter(reader) != nullptr);
    REPUB_CHECK(repub_reader_layout_spec(reader) != nullptr);

    repub_canvas_t* canvas =
        repub_canvas_create(nullptr, 480, 800, REPUB_FORMAT_MONO1, &err);
    REPUB_CHECK(canvas != nullptr);
    repub_canvas_clear(canvas, 1);
    REPUB_CHECK_EQ(repub_reader_render(reader, canvas, nullptr), REPUB_OK);
    REPUB_CHECK(count_black_bits(canvas) > 0);

    /* Render through the page handle as well. */
    repub_canvas_clear(canvas, 1);
    REPUB_CHECK_EQ(repub_page_render(repub_reader_page(reader), canvas, nullptr),
                   REPUB_OK);
    REPUB_CHECK(count_black_bits(canvas) > 0);

    /* Progress and navigation. */
    const float progress = repub_reader_progress(reader);
    REPUB_CHECK(progress >= 0.0f && progress <= 1.0f);

    REPUB_CHECK_EQ(repub_reader_goto_progress(reader, 0.5f), REPUB_OK);
    REPUB_CHECK(repub_reader_page(reader) != nullptr);
    REPUB_CHECK(repub_reader_page_index(reader) < repub_reader_page_count(reader));
    const float middle = repub_reader_progress(reader);
    REPUB_CHECK(middle >= 0.0f && middle <= 1.0f);

    REPUB_CHECK_EQ(repub_reader_goto_progress(reader, 0.0f), REPUB_OK);
    REPUB_CHECK_EQ(repub_reader_progress(reader) >= 0.0f, true);
    REPUB_CHECK_EQ(repub_reader_goto_progress(reader, 1.0f), REPUB_OK);
    REPUB_CHECK(repub_reader_progress(reader) <= 1.0f);
    /* Out-of-range fraction is clamped, not rejected. */
    REPUB_CHECK_EQ(repub_reader_goto_progress(reader, 5.0f), REPUB_OK);

    /* Chapter jumping. */
    REPUB_CHECK_EQ(repub_reader_goto_chapter(reader, 0), REPUB_OK);
    REPUB_CHECK_EQ(repub_reader_page_index(reader), 0);
    const size_t chapter_count = repub_reader_chapter_count(reader);
    REPUB_CHECK_EQ(repub_reader_goto_chapter(reader, chapter_count + 10),
                   REPUB_ERR_NOT_FOUND);

    /* Cross-chapter next/prev walk. */
    if (repub_reader_page_count(reader) > 1) {
        REPUB_CHECK_EQ(repub_reader_next_page(reader), REPUB_OK);
        REPUB_CHECK_EQ(repub_reader_page_index(reader), 1);
        REPUB_CHECK_EQ(repub_reader_prev_page(reader), REPUB_OK);
        REPUB_CHECK_EQ(repub_reader_page_index(reader), 0);
    }
    size_t steps = 0;
    while (repub_reader_next_page(reader) == REPUB_OK && steps < 100000) ++steps;
    if (steps > 0) {
        REPUB_CHECK_EQ(repub_reader_prev_page(reader), REPUB_OK);
    }

    /* Layout change reflows and keeps a valid page. */
    repub_layout_spec_t wider = make_spec(320, 480, 18);
    REPUB_CHECK_EQ(repub_reader_set_layout(reader, &wider), REPUB_OK);
    REPUB_CHECK(repub_reader_page_count(reader) > 0);
    REPUB_CHECK(repub_reader_page(reader) != nullptr);
    REPUB_CHECK_EQ(repub_reader_set_layout(reader, nullptr), REPUB_ERR_INVALID_ARG);

    /* Defensive accessors. */
    REPUB_CHECK_EQ(repub_reader_page_count(nullptr), 0);
    REPUB_CHECK(repub_reader_page(nullptr) == nullptr);
    REPUB_CHECK_EQ(repub_reader_progress(nullptr), 0.0f);
    REPUB_CHECK_EQ(repub_reader_next_page(nullptr), REPUB_ERR_INVALID_ARG);
    REPUB_CHECK_EQ(repub_reader_render(reader, nullptr, nullptr), REPUB_ERR_INVALID_ARG);

    repub_canvas_destroy(canvas);
    repub_reader_close(reader);
    repub_reader_close(nullptr);

    /* Invalid open path fails cleanly. */
    err = REPUB_OK;
    repub_reader_t* bad = repub_reader_open(nullptr, "no/such/book.epub", &spec,
                                            nullptr, &err);
    REPUB_CHECK(bad == nullptr);
    REPUB_CHECK(err != REPUB_OK);
}

/* ------------------------------------------------------------------------- */
/* Footnotes                                                                 */
/* ------------------------------------------------------------------------- */

REPUB_TEST(api_note_popup) {
    const std::string path = fixture_path(kMainFixture);
    repub_err_t err = REPUB_OK;
    repub_book_t* book = repub_book_open(nullptr, path.c_str(), nullptr, &err);
    REPUB_CHECK(book != nullptr);

    /* Find a chapter that carries notes. */
    size_t note_chapter = static_cast<size_t>(-1);
    size_t note_index_in_chapter = 0;
    const size_t spine_count = repub_book_spine_count(book);
    for (size_t i = 0; i < spine_count && i < 40; ++i) {
        repub_chapter_t* chapter = nullptr;
        if (repub_book_chapter_open(book, i, &chapter) != REPUB_OK) continue;
        if (repub_chapter_note_count(chapter) > 0) {
            note_chapter = i;
            note_index_in_chapter = 0;
            const repub_note_info_t* note = repub_chapter_note(chapter, 0);
            REPUB_CHECK(note != nullptr);
            if (note->text != nullptr && note->text_len > 0) {
                repub_chapter_free(chapter);
                break;
            }
        }
        repub_chapter_free(chapter);
    }
    repub_book_close(book);
    if (note_chapter == static_cast<size_t>(-1)) return; /* fixture has no notes */

    repub_layout_spec_t spec = make_spec(480, 800, 24);
    repub_reader_t* reader = repub_reader_open(nullptr, path.c_str(), &spec, nullptr, &err);
    REPUB_CHECK(reader != nullptr);
    REPUB_CHECK_EQ(repub_reader_goto_chapter(reader, note_chapter), REPUB_OK);

    const char* note_text = nullptr;
    size_t note_len = 0;
    REPUB_CHECK_EQ(repub_reader_note_text(reader, note_index_in_chapter, &note_text,
                                          &note_len),
                   REPUB_OK);
    REPUB_CHECK(note_text != nullptr);
    REPUB_CHECK(note_len > 0);
    REPUB_CHECK_EQ(repub_reader_note_text(reader, 99999, &note_text, &note_len),
                   REPUB_ERR_NOT_FOUND);

    /* Hit test through the page marker table (guarded: markers may be absent). */
    const repub_page_t* page = repub_reader_page(reader);
    REPUB_CHECK(page != nullptr);
    for (size_t i = 0; i < repub_page_info(page)->note_count; ++i) {
        const repub_page_note_marker_t* marker = repub_page_note_at(page, i);
        if (!marker) continue;
        const int32_t cx = marker->rect.x + marker->rect.w / 2;
        const int32_t cy = marker->rect.y + marker->rect.h / 2;
        REPUB_CHECK_EQ(repub_reader_note_at(reader, cx, cy), marker->note_index);
    }
    REPUB_CHECK_EQ(repub_reader_note_at(reader, -5000, -5000), -1);

    repub_layout_spec_t popup_spec = make_spec(200, 300, 16);
    repub_note_popup_t* popup =
        repub_reader_note_popup_open(reader, note_index_in_chapter, &popup_spec, &err);
    REPUB_CHECK(popup != nullptr);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(repub_note_popup_page_count(popup) > 0);
    REPUB_CHECK(repub_note_popup_layout(popup) != nullptr);

    repub_canvas_t* canvas =
        repub_canvas_create(nullptr, 200, 300, REPUB_FORMAT_MONO1, &err);
    REPUB_CHECK(canvas != nullptr);
    repub_canvas_clear(canvas, 1);
    REPUB_CHECK_EQ(repub_note_popup_render(popup, 0, canvas, nullptr), REPUB_OK);
    REPUB_CHECK(count_black_bits(canvas) > 0);
    REPUB_CHECK_EQ(repub_note_popup_render(popup, 9999, canvas, nullptr),
                   REPUB_ERR_NOT_FOUND);

    /* Popup defaults to the reader page rect when the spec has none. */
    repub_layout_spec_t no_rect{};
    repub_layout_spec_defaults(&no_rect);
    repub_note_popup_t* fallback_popup =
        repub_reader_note_popup_open(reader, note_index_in_chapter, &no_rect, &err);
    REPUB_CHECK(fallback_popup != nullptr);
    REPUB_CHECK(repub_note_popup_page_count(fallback_popup) > 0);
    repub_note_popup_free(fallback_popup);

    repub_canvas_destroy(canvas);
    repub_note_popup_free(popup);
    repub_note_popup_free(nullptr);
    repub_reader_close(reader);
}

/* ------------------------------------------------------------------------- */
/* Images                                                                    */
/* ------------------------------------------------------------------------- */

REPUB_TEST(api_image_viewer) {
    const std::string path = fixture_path("test_png_images.epub");
    repub_err_t err = REPUB_OK;
    repub_book_t* book = repub_book_open(nullptr, path.c_str(), nullptr, &err);
    if (!book) return; /* fixture optional */

    size_t image_chapter = static_cast<size_t>(-1);
    const size_t spine_count = repub_book_spine_count(book);
    for (size_t i = 0; i < spine_count && i < 40; ++i) {
        repub_chapter_t* chapter = nullptr;
        if (repub_book_chapter_open(book, i, &chapter) != REPUB_OK) continue;
        if (repub_chapter_image_count(chapter) > 0) {
            image_chapter = i;
            repub_chapter_free(chapter);
            break;
        }
        repub_chapter_free(chapter);
    }
    repub_book_close(book);
    if (image_chapter == static_cast<size_t>(-1)) return;

    repub_layout_spec_t spec = make_spec(400, 600, 20);
    repub_reader_t* reader = repub_reader_open(nullptr, path.c_str(), &spec, nullptr, &err);
    REPUB_CHECK(reader != nullptr);
    REPUB_CHECK_EQ(repub_reader_goto_chapter(reader, image_chapter), REPUB_OK);
    REPUB_CHECK(repub_reader_chapter(reader) != nullptr);
    REPUB_CHECK(repub_chapter_image_count(repub_reader_chapter(reader)) > 0);

    repub_image_t* decoded = nullptr;
    REPUB_CHECK_EQ(repub_reader_image_decode(reader, 0, 0, 0, &decoded), REPUB_OK);
    REPUB_CHECK(decoded != nullptr);
    REPUB_CHECK(repub_image_width(decoded) > 0);
    REPUB_CHECK(repub_image_height(decoded) > 0);
    repub_image_free(decoded);
    REPUB_CHECK_EQ(repub_reader_image_decode(reader, 99999, 0, 0, &decoded),
                   REPUB_ERR_NOT_FOUND);
    REPUB_CHECK(decoded == nullptr);

    repub_image_viewer_t* viewer = repub_reader_image_viewer_open(reader, 0, &err);
    REPUB_CHECK(viewer != nullptr);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(repub_image_viewer_image(viewer) != nullptr);

    repub_canvas_t* canvas =
        repub_canvas_create(nullptr, 400, 600, REPUB_FORMAT_GRAY8, &err);
    REPUB_CHECK(canvas != nullptr);
    repub_canvas_clear(canvas, 255);
    REPUB_CHECK_EQ(repub_image_viewer_render(viewer, canvas,
                                             repub_rect_t{0, 0, 400, 600}, 1.0f,
                                             0.0f, 0.0f, nullptr),
                   REPUB_OK);
    REPUB_CHECK(count_non_white_gray(canvas) > 0);

    /* Zoom/pan clamping must not fail. */
    repub_canvas_clear(canvas, 255);
    REPUB_CHECK_EQ(repub_image_viewer_render(viewer, canvas,
                                             repub_rect_t{0, 0, 400, 600}, 99.0f,
                                             -9.0f, 9.0f, nullptr),
                   REPUB_OK);

    /* Image hit test (guarded: image pages may not be page 0). */
    const repub_page_t* page = repub_reader_page(reader);
    if (page) {
        for (size_t i = 0; i < repub_page_info(page)->image_count; ++i) {
            const repub_page_image_t* image = repub_page_image_at(page, i);
            if (!image) continue;
            const int32_t cx = image->rect.x + image->rect.w / 2;
            const int32_t cy = image->rect.y + image->rect.h / 2;
            REPUB_CHECK_EQ(repub_reader_image_at(reader, cx, cy), image->image_index);
        }
    }

    repub_canvas_destroy(canvas);
    repub_image_viewer_free(viewer);
    repub_image_viewer_free(nullptr);
    repub_reader_close(reader);
}

/* ------------------------------------------------------------------------- */
/* C++ facade                                                                */
/* ------------------------------------------------------------------------- */

REPUB_TEST(api_cpp_facade) {
    const std::string path = fixture_path(kMainFixture);
    repub_err_t err = REPUB_OK;

    repub::Book book = repub::Book::open(path, nullptr, nullptr, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(book.valid());
    REPUB_CHECK(!book.title().empty());
    REPUB_CHECK(book.spine_count() > 0);
    REPUB_CHECK(book.metadata() != nullptr);

    repub::Chapter chapter = book.open_chapter(0, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(chapter.valid());
    REPUB_CHECK(!chapter.text().empty());
    REPUB_CHECK(chapter.block_count() > 0);
    REPUB_CHECK(chapter.block(0) != nullptr);

    repub_layout_spec_t spec = repub::default_layout_spec(
        repub_rect_t{0, 0, 320, 480}, 20);
    repub::Layout layout = repub::Layout::create(chapter, &spec, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(layout.valid());
    REPUB_CHECK(layout.page_count() > 0);
    repub::Page page = layout.page(0);
    REPUB_CHECK(page.valid());
    REPUB_CHECK(page.info() != nullptr);
    REPUB_CHECK_EQ(page.info()->start_offset, 0u);

    repub::Canvas canvas = repub::Canvas::create(320, 480, REPUB_FORMAT_MONO1,
                                                 nullptr, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(canvas.valid());
    canvas.clear(1);
    REPUB_CHECK_EQ(layout.render(0, canvas, nullptr), REPUB_OK);
    REPUB_CHECK(count_black_bits(canvas.get()) > 0);

    /* Reader opens its own book and owns it. */
    repub::Reader reader = repub::Reader::open(path, &spec, nullptr, nullptr, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(reader.valid());
    REPUB_CHECK(reader.page_count() > 0);
    REPUB_CHECK(reader.book() != nullptr);
    REPUB_CHECK(reader.chapter().valid());
    REPUB_CHECK(reader.page().valid());
    canvas.clear(1);
    REPUB_CHECK_EQ(reader.render(canvas, nullptr), REPUB_OK);
    REPUB_CHECK(count_black_bits(canvas.get()) > 0);
    REPUB_CHECK_EQ(reader.goto_progress(0.5f), REPUB_OK);
    REPUB_CHECK(reader.page_index() < reader.page_count());
    REPUB_CHECK(reader.progress() >= 0.0f);

    /* Attach keeps a pre-configured book. */
    repub::Book attach_book = repub::Book::open(path, nullptr, nullptr, &err);
    REPUB_CHECK(attach_book.valid());
    repub::Reader attached = repub::Reader::attach(std::move(attach_book), &spec, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(attached.valid());
    REPUB_CHECK(attached.page_count() > 0);

    /* Layout a snippet through the facade. */
    repub_open_options_t options{};
    options.struct_size = sizeof(options);
    repub::Book snippet_book = repub::Book::open(path, nullptr, &options, &err);
    REPUB_CHECK(snippet_book.valid());
    repub::Layout snippet =
        snippet_book.layout_text("A quick popup", &spec, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(snippet.valid());
    REPUB_CHECK(snippet.page_count() > 0);

    /* Facade image wrappers. */
    repub::Image image = repub::Image::decode(kOnePixelPng, sizeof(kOnePixelPng),
                                              0, 0, nullptr, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK(image.valid());
    REPUB_CHECK_EQ(image.width(), 1);
    repub::Image scaled = image.scale(3, 3, nullptr, &err);
    REPUB_CHECK_EQ(err, REPUB_OK);
    REPUB_CHECK_EQ(scaled.width(), 3);

    repub::Book invalid = repub::Book::open("no/such/book.epub", nullptr, nullptr, &err);
    REPUB_CHECK(!invalid.valid());
    REPUB_CHECK(err != REPUB_OK);
}
