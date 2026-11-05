/*
 * RollingEPUB public C API: errors/allocation helpers, layout spec and render
 * option defaults, canvas and image wrappers, page accessors/rendering and the
 * layout object API.
 *
 * The container (book) API lives in the container sources; chapter wrappers in
 * api/chapter.cpp; the high level reader/popup/viewer in api/reader.cpp.
 *
 * Threading: no object may be used concurrently (documented in repub.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <new>

#include "api/api_util.h"

/* ------------------------------------------------------------------------- */
/* Errors, allocation, defaults                                              */
/* ------------------------------------------------------------------------- */

const char* repub_err_str(repub_err_t err) {
    switch (err) {
        case REPUB_OK:
            return "ok";
        case REPUB_ERR_IO:
            return "I/O error";
        case REPUB_ERR_FORMAT:
            return "malformed container or markup";
        case REPUB_ERR_MEMORY:
            return "out of memory";
        case REPUB_ERR_UNSUPPORTED:
            return "unsupported feature or format";
        case REPUB_ERR_NOT_FOUND:
            return "not found";
        case REPUB_ERR_INVALID_ARG:
            return "invalid argument";
        case REPUB_ERR_BUFFER_SMALL:
            return "buffer too small";
        case REPUB_ERR_CANCELLED:
            return "cancelled";
        case REPUB_ERR_LIMIT:
            return "configured limit exceeded";
        case REPUB_ERR_PROTECTED:
            return "book is encrypted or DRM protected";
        case REPUB_ERR_INTERNAL:
            return "internal error";
        default:
            return "unknown error";
    }
}

void* repub_alloc(const repub_platform_t* platform, size_t size) {
    return repub::palloc(platform, size);
}

void* repub_realloc(const repub_platform_t* platform, void* ptr, size_t size) {
    return repub::prealloc(platform, ptr, size);
}

void repub_free(const repub_platform_t* platform, void* ptr) {
    repub::pfree(platform, ptr);
}

void repub_layout_spec_defaults(repub_layout_spec_t* spec) {
    if (!spec) return;
    repub::layout_spec_defaults(spec);
    spec->struct_size = static_cast<uint32_t>(sizeof(repub_layout_spec_t));
    if (spec->font_size_px <= 0) spec->font_size_px = 22;
    if (spec->line_height_percent <= 0) spec->line_height_percent = 160;
    if (spec->paragraph_gap_percent <= 0) spec->paragraph_gap_percent = 60;
    if (spec->first_line_indent_em < 0) spec->first_line_indent_em = 2;
    spec->justify = 1;
    spec->images_visible = 1;
    spec->image_full_bleed = 0;
    spec->max_pages = 0;
#if REPUB_ENABLE_HYPHENATION
    spec->hyphenation = 1;
#else
    spec->hyphenation = 0;
#endif
}

void repub_render_options_defaults(repub_render_options_t* options) {
    if (!options) return;
    std::memset(options, 0, sizeof(*options));
    options->struct_size = static_cast<uint32_t>(sizeof(repub_render_options_t));
    options->flags = REPUB_RENDER_UNDERLINE_LINKS | REPUB_RENDER_DRAW_IMAGES |
                     REPUB_RENDER_DRAW_NOTE_ICONS;
    options->text_dither = REPUB_DITHER_THRESHOLD;
    options->text_level = 0;
    options->background_level = -1;
    options->threshold = 128;
}

/* ------------------------------------------------------------------------- */
/* Canvas                                                                    */
/* ------------------------------------------------------------------------- */

repub_canvas_t* repub_canvas_create(const repub_platform_t* platform,
                                    int32_t width, int32_t height,
                                    repub_pixel_format_t format,
                                    repub_err_t* err) {
    if (err) *err = REPUB_OK;
    if (width <= 0 || height <= 0) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
    if (format < REPUB_FORMAT_MONO1 || format > REPUB_FORMAT_GRAY8) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
#if !REPUB_ENABLE_GRAYSCALE
    if (format != REPUB_FORMAT_MONO1) {
        if (err) *err = REPUB_ERR_UNSUPPORTED;
        return nullptr;
    }
#endif
    repub::repub_canvas* canvas = nullptr;
    const repub_err_t rc = repub::canvas_create(repub::effective_platform(platform),
                                                width, height, format, &canvas);
    if (rc != REPUB_OK) {
        if (err) *err = rc;
        return nullptr;
    }
    return reinterpret_cast<repub_canvas_t*>(canvas);
}

repub_canvas_t* repub_canvas_wrap(const repub_platform_t* platform, void* pixels,
                                  int32_t width, int32_t height,
                                  int32_t stride_bytes,
                                  repub_pixel_format_t format, repub_err_t* err) {
    if (err) *err = REPUB_OK;
    if (!pixels || width <= 0 || height <= 0 || stride_bytes <= 0) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
    if (format < REPUB_FORMAT_MONO1 || format > REPUB_FORMAT_GRAY8) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
#if !REPUB_ENABLE_GRAYSCALE
    if (format != REPUB_FORMAT_MONO1) {
        if (err) *err = REPUB_ERR_UNSUPPORTED;
        return nullptr;
    }
#endif
    repub::repub_canvas* canvas = nullptr;
    const repub_err_t rc =
        repub::canvas_wrap(repub::effective_platform(platform), pixels, width,
                           height, stride_bytes, format, &canvas);
    if (rc != REPUB_OK) {
        if (err) *err = rc;
        return nullptr;
    }
    return reinterpret_cast<repub_canvas_t*>(canvas);
}

void repub_canvas_destroy(repub_canvas_t* canvas) {
    if (!canvas) return;
    repub::canvas_destroy(repub::api::cast_canvas(canvas));
}

int32_t repub_canvas_width(const repub_canvas_t* canvas) {
    if (!canvas) return 0;
    return repub::api::cast_canvas(canvas)->width;
}

int32_t repub_canvas_height(const repub_canvas_t* canvas) {
    if (!canvas) return 0;
    return repub::api::cast_canvas(canvas)->height;
}

repub_pixel_format_t repub_canvas_format(const repub_canvas_t* canvas) {
    if (!canvas) return REPUB_FORMAT_MONO1;
    return repub::api::cast_canvas(canvas)->format;
}

void* repub_canvas_data(const repub_canvas_t* canvas) {
    if (!canvas) return nullptr;
    return repub::api::cast_canvas(canvas)->data;
}

int32_t repub_canvas_stride(const repub_canvas_t* canvas) {
    if (!canvas) return 0;
    return repub::api::cast_canvas(canvas)->stride;
}

void repub_canvas_clear(repub_canvas_t* canvas, int32_t level) {
    repub::repub_canvas* c = repub::api::cast_canvas(canvas);
    if (!c) return;
    if (level < 0) level = 0;
    if (level > c->max_level) level = c->max_level;
    c->clear(level);
}

void repub_canvas_fill_rect(repub_canvas_t* canvas, repub_rect_t rect,
                            int32_t level) {
    repub::repub_canvas* c = repub::api::cast_canvas(canvas);
    if (!c || rect.w <= 0 || rect.h <= 0) return;
    if (level < 0) level = 0;
    if (level > c->max_level) level = c->max_level;
    c->fill_rect(repub::from_public(rect), level);
}

void repub_canvas_set_pixel(repub_canvas_t* canvas, int32_t x, int32_t y,
                            int32_t level) {
    repub::repub_canvas* c = repub::api::cast_canvas(canvas);
    if (!c) return;
    if (x < 0 || y < 0 || x >= c->width || y >= c->height) return;
    if (level < 0) level = 0;
    if (level > c->max_level) level = c->max_level;
    c->set_pixel(x, y, level);
}

/* ------------------------------------------------------------------------- */
/* Images                                                                    */
/* ------------------------------------------------------------------------- */

repub_err_t repub_image_decode(const repub_platform_t* platform, const void* data,
                               size_t size, int32_t max_width, int32_t max_height,
                               repub_image_t** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    if (!data || size == 0) return REPUB_ERR_INVALID_ARG;
#if !REPUB_ENABLE_IMAGES
    (void)platform;
    (void)max_width;
    (void)max_height;
    return REPUB_ERR_UNSUPPORTED;
#else
    repub::repub_image* image = nullptr;
    const repub_err_t rc = repub::image_decode(repub::effective_platform(platform),
                                               data, size, max_width, max_height,
                                               &image);
    if (rc != REPUB_OK) return rc;
    *out = reinterpret_cast<repub_image_t*>(image);
    return REPUB_OK;
#endif
}

repub_image_t* repub_image_create(const repub_platform_t* platform, int32_t width,
                                  int32_t height, repub_err_t* err) {
    if (err) *err = REPUB_OK;
    if (width <= 0 || height <= 0) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
#if !REPUB_ENABLE_IMAGES
    (void)platform;
    if (err) *err = REPUB_ERR_UNSUPPORTED;
    return nullptr;
#else
    repub::repub_image* image =
        repub::image_create(repub::effective_platform(platform), width, height, err);
    return reinterpret_cast<repub_image_t*>(image);
#endif
}

void repub_image_free(repub_image_t* image) {
    if (!image) return;
    repub::image_free(repub::api::cast_image(image));
}

int32_t repub_image_width(const repub_image_t* image) {
    if (!image) return 0;
    return repub::api::cast_image(image)->width;
}

int32_t repub_image_height(const repub_image_t* image) {
    if (!image) return 0;
    return repub::api::cast_image(image)->height;
}

const uint8_t* repub_image_pixels(const repub_image_t* image) {
    if (!image) return nullptr;
    const repub::repub_image* img = repub::api::cast_image(image);
    return img->rgba.empty() ? nullptr : img->rgba.data();
}

repub_err_t repub_image_scale(const repub_platform_t* platform,
                              const repub_image_t* src, int32_t width,
                              int32_t height, repub_image_t** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    if (!src || width <= 0 || height <= 0) return REPUB_ERR_INVALID_ARG;
#if !REPUB_ENABLE_IMAGES
    (void)platform;
    return REPUB_ERR_UNSUPPORTED;
#else
    repub::repub_image* image = nullptr;
    const repub_err_t rc = repub::image_scale(
        repub::effective_platform(platform), repub::api::cast_image(src), width,
        height, &image);
    if (rc != REPUB_OK) return rc;
    *out = reinterpret_cast<repub_image_t*>(image);
    return REPUB_OK;
#endif
}

repub_err_t repub_image_probe(const void* data, size_t size, int32_t* width,
                              int32_t* height) {
    if (!data || size == 0) return REPUB_ERR_INVALID_ARG;
    return repub::image_probe(data, size, width, height);
}

repub_err_t repub_canvas_draw_image(repub_canvas_t* canvas,
                                    const repub_image_t* image,
                                    const repub_image_view_t* view) {
    repub::repub_canvas* c = repub::api::cast_canvas(canvas);
    const repub::repub_image* img = repub::api::cast_image(image);
    if (!c || !img) return REPUB_ERR_INVALID_ARG;
#if !REPUB_ENABLE_IMAGES
    return REPUB_ERR_UNSUPPORTED;
#else
    repub_image_view_t prepared{};
    const repub_err_t rc = repub::api::copy_caller_struct(
        view, &prepared, sizeof(prepared), repub::api::kImageViewMinSize);
    if (rc != REPUB_OK) return rc;
    if (prepared.dest.w <= 0 || prepared.dest.h <= 0) {
        prepared.dest = repub_rect_t{0, 0, c->width, c->height};
    }
    if (!(prepared.zoom > 0.0f)) prepared.zoom = 1.0f;
    if (prepared.zoom < 0.01f) prepared.zoom = 0.01f;
    if (prepared.zoom > 64.0f) prepared.zoom = 64.0f;
    if (prepared.fit < REPUB_IMAGE_FIT_NONE || prepared.fit > REPUB_IMAGE_FIT_COVER) {
        prepared.fit = REPUB_IMAGE_FIT_CONTAIN;
    }
    if (prepared.background_level < 0) prepared.background_level = 0;
    if (prepared.background_level > c->max_level) {
        prepared.background_level = c->max_level;
    }
    return repub::canvas_draw_image(c, img, &prepared);
#endif
}

/* ------------------------------------------------------------------------- */
/* Page accessors                                                            */
/* ------------------------------------------------------------------------- */

const repub_page_info_t* repub_page_info(const repub_page_t* page) {
    const repub::repub_page* p = repub::api::cast_page(page);
    if (!p) return nullptr;
    return &p->info;
}

const repub_span_t* repub_page_span(const repub_page_t* page, size_t index) {
    const repub::repub_page* p = repub::api::cast_page(page);
    if (!p || index >= p->spans.size()) return nullptr;
    return &p->spans[index];
}

const repub_page_image_t* repub_page_image_at(const repub_page_t* page,
                                              size_t index) {
    const repub::repub_page* p = repub::api::cast_page(page);
    if (!p || index >= p->images.size()) return nullptr;
    return &p->images[index];
}

const repub_page_note_marker_t* repub_page_note_at(const repub_page_t* page,
                                                   size_t index) {
    const repub::repub_page* p = repub::api::cast_page(page);
    if (!p || index >= p->notes.size()) return nullptr;
    return &p->notes[index];
}

const repub_page_link_t* repub_page_link_at(const repub_page_t* page,
                                            size_t index) {
    const repub::repub_page* p = repub::api::cast_page(page);
    if (!p || index >= p->links.size()) return nullptr;
    return &p->links[index];
}

size_t repub_page_text(const repub_page_t* page, char* buf, size_t cap) {
    const repub::repub_page* p = repub::api::cast_page(page);
    if (!p || !buf || cap == 0) return 0;
    buf[0] = '\0';
    if (!p->chapter) return 0;
    const std::string& text = p->chapter->flow.text;
    const size_t start = p->info.start_offset < text.size()
                             ? static_cast<size_t>(p->info.start_offset)
                             : text.size();
    size_t end = p->info.end_offset < text.size()
                     ? static_cast<size_t>(p->info.end_offset)
                     : text.size();
    if (end < start) end = start;
    size_t count = end - start;
    if (count > cap - 1) count = cap - 1;
    std::memcpy(buf, text.data() + start, count);
    buf[count] = '\0';
    return count;
}

/* ------------------------------------------------------------------------- */
/* Page rendering                                                            */
/* ------------------------------------------------------------------------- */

repub_err_t repub_page_render(const repub_page_t* page, repub_canvas_t* canvas,
                              const repub_render_options_t* options) {
    const repub::repub_page* p = repub::api::cast_page(page);
    repub::repub_canvas* c = repub::api::cast_canvas(canvas);
    if (!p || !c) return REPUB_ERR_INVALID_ARG;
    if (!p->chapter || !p->chapter->book) return REPUB_ERR_INVALID_ARG;
    const repub::repub_book& book = *p->chapter->book;
    if (!book.fonts) return REPUB_ERR_INTERNAL;
    repub_render_options_t opts;
    repub_err_t rc = repub::api::prepare_render_options(options, &opts);
    if (rc != REPUB_OK) return rc;
    return repub::render_page(*p, book, *c, *book.fonts, opts);
}

repub_err_t repub_layout_render(const repub_layout_t* layout, size_t page,
                                repub_canvas_t* canvas,
                                const repub_render_options_t* options) {
    const repub::repub_layout* l = repub::api::cast_layout(layout);
    if (!l || !canvas) return REPUB_ERR_INVALID_ARG;
    if (page >= l->pages.size()) return REPUB_ERR_NOT_FOUND;
    return repub_page_render(repub::api::uncast_page(&l->pages[page]), canvas,
                             options);
}

/* ------------------------------------------------------------------------- */
/* Layout object                                                             */
/* ------------------------------------------------------------------------- */

namespace {

/* Layouts created by repub_book_layout_text() own a synthetic chapter that is
 * not reachable from the public API; keep it here so repub_layout_free() can
 * release it.  The API is documented as single-threaded, but a mutex costs
 * nothing on the host and keeps separate readers independent. */
std::mutex g_synthetic_layout_mutex;
std::map<const ::repub_layout*, repub::repub_chapter*> g_synthetic_layouts;

}  // namespace

repub_err_t repub_layout_create(const repub_chapter_t* chapter,
                                const repub_layout_spec_t* spec,
                                repub_layout_t** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    if (!ch || !ch->book) return REPUB_ERR_INVALID_ARG;
    if (!ch->book->fonts) return REPUB_ERR_INTERNAL;

    repub_layout_spec_t prepared;
    repub_err_t rc = repub::api::prepare_layout_spec(spec, nullptr, &prepared);
    if (rc != REPUB_OK) return rc;

    std::unique_ptr<repub::repub_layout> layout(new (std::nothrow) repub::repub_layout());
    if (!layout) return REPUB_ERR_MEMORY;
    rc = repub::layout_build(*ch, prepared, *ch->book->fonts, layout.get());
    if (rc != REPUB_OK) return rc;
    if (prepared.hyphenation_lang && *prepared.hyphenation_lang) {
        layout->hyphen_lang = prepared.hyphenation_lang;
        layout->spec.hyphenation_lang = layout->hyphen_lang.c_str();
    }
    *out = repub::api::uncast_layout(layout.release());
    return REPUB_OK;
}

void repub_layout_free(repub_layout_t* layout) {
    if (!layout) return;
    repub::repub_layout* l = repub::api::cast_layout(layout);
    repub::repub_chapter* synthetic = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_synthetic_layout_mutex);
        auto it = g_synthetic_layouts.find(layout);
        if (it != g_synthetic_layouts.end()) {
            synthetic = it->second;
            g_synthetic_layouts.erase(it);
        }
    }
    delete synthetic;
    delete l;
}

const repub_chapter_t* repub_layout_chapter(const repub_layout_t* layout) {
    const repub::repub_layout* l = repub::api::cast_layout(layout);
    if (!l || !l->chapter) return nullptr;
    return repub::api::uncast_chapter(const_cast<repub::repub_chapter*>(l->chapter));
}

const repub_layout_spec_t* repub_layout_spec(const repub_layout_t* layout) {
    const repub::repub_layout* l = repub::api::cast_layout(layout);
    if (!l) return nullptr;
    return &l->spec;
}

size_t repub_layout_page_count(const repub_layout_t* layout) {
    return repub::api::layout_page_count(repub::api::cast_layout(layout));
}

const repub_page_t* repub_layout_page(const repub_layout_t* layout, size_t index) {
    const repub::repub_page* page =
        repub::api::layout_page(repub::api::cast_layout(layout), index);
    return repub::api::uncast_page(page);
}

uint32_t repub_layout_page_start_offset(const repub_layout_t* layout, size_t page) {
    const repub::repub_layout* l = repub::api::cast_layout(layout);
    if (!l || page >= l->pages.size()) return 0;
    return repub::api::layout_page_start_offset(l, page);
}

size_t repub_layout_page_for_offset(const repub_layout_t* layout, uint32_t offset) {
    const repub::repub_layout* l = repub::api::cast_layout(layout);
    if (!l || l->pages.empty()) return 0;
    return repub::api::layout_page_for_offset(l, offset);
}

/* ------------------------------------------------------------------------- */
/* Ad-hoc text layout                                                        */
/* ------------------------------------------------------------------------- */

repub_err_t repub_book_layout_text(repub_book_t* book, const char* utf8, size_t len,
                                   const repub_layout_spec_t* spec,
                                   repub_layout_t** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    repub::repub_book* b = repub::api::cast_book(book);
    if (!b || !b->fonts) return REPUB_ERR_INVALID_ARG;
    if (!utf8 && len != 0) return REPUB_ERR_INVALID_ARG;

    repub_layout_spec_t prepared;
    repub_err_t rc = repub::api::prepare_layout_spec(spec, nullptr, &prepared);
    if (rc != REPUB_OK) return rc;

    const std::string raw =
        len ? std::string(utf8, len) : std::string();
    std::unique_ptr<repub::repub_chapter> chapter(new (std::nothrow) repub::repub_chapter());
    if (!chapter) return REPUB_ERR_MEMORY;
    chapter->book = b;
    chapter->spine_index = 0;
    repub::api::build_synthetic_flow(repub::api::normalize_line_endings(raw),
                                     &chapter->flow);

    std::unique_ptr<repub::repub_layout> layout(new (std::nothrow) repub::repub_layout());
    if (!layout) return REPUB_ERR_MEMORY;
    rc = repub::layout_build_flow(*chapter, chapter->flow, prepared, *b->fonts,
                                  layout.get());
    if (rc != REPUB_OK) return rc;
    layout->chapter = chapter.get();
    if (prepared.hyphenation_lang && *prepared.hyphenation_lang) {
        layout->hyphen_lang = prepared.hyphenation_lang;
        layout->spec.hyphenation_lang = layout->hyphen_lang.c_str();
    }

    ::repub_layout* handle = repub::api::uncast_layout(layout.get());
    {
        std::lock_guard<std::mutex> lock(g_synthetic_layout_mutex);
        g_synthetic_layouts[handle] = chapter.release();
    }
    *out = repub::api::uncast_layout(layout.release());
    return REPUB_OK;
}
