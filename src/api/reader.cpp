/*
 * RollingEPUB public C API: high level reader, footnote popups and image
 * viewer.
 *
 * The reader owns the book, the currently laid out chapter and its layout.
 * Navigation re-parses only when the spine item changes; crossing chapter
 * boundaries on next/prev skips empty or unreadable spine items.
 *
 * Threading: a reader and everything obtained from it must not be used
 * concurrently.  Separate readers may be used from separate tasks.
 *
 * SPDX-License-Identifier: MIT
 */
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "api/api_util.h"

namespace {

using repub::api::uncast_chapter;
using repub::api::uncast_layout;
using repub::api::uncast_page;
using repub::api::uncast_popup;
using repub::api::uncast_reader;
using repub::api::uncast_viewer;

/* Build and parse one chapter plus its layout without touching the reader's
 * current state.  Caller owns both results. */
repub_err_t build_chapter_layout(repub::repub_reader* reader, size_t chapter_index,
                                 repub::repub_chapter** out_chapter,
                                 repub::repub_layout** out_layout) {
    *out_chapter = nullptr;
    *out_layout = nullptr;
    if (!reader || !reader->book) return REPUB_ERR_INVALID_ARG;
    if (!reader->book->fonts) return REPUB_ERR_INTERNAL;
    if (chapter_index >= reader->book->spine.size()) return REPUB_ERR_NOT_FOUND;

    repub::repub_chapter* chapter = nullptr;
    repub_err_t rc = repub::book_chapter_open(reader->book, chapter_index,
                                              reader->parse_options, &chapter);
    if (rc != REPUB_OK) {
        delete chapter;
        return rc;
    }
    if (!chapter) return REPUB_ERR_INTERNAL;
    repub::chapter_finalize(chapter);

    std::unique_ptr<repub::repub_layout> layout(new (std::nothrow) repub::repub_layout());
    if (!layout) {
        delete chapter;
        return REPUB_ERR_MEMORY;
    }
    repub_layout_spec_t spec = reader->spec;
    spec.struct_size = static_cast<uint32_t>(sizeof(spec));
    spec.hyphenation_lang =
        reader->hyphen_lang.empty() ? nullptr : reader->hyphen_lang.c_str();
    rc = repub::layout_build(*chapter, spec, *reader->book->fonts, layout.get());
    if (rc != REPUB_OK) {
        delete chapter;
        return rc;
    }
    layout->chapter = chapter;
    if (!reader->hyphen_lang.empty()) {
        layout->hyphen_lang = reader->hyphen_lang;
        layout->spec.hyphenation_lang = layout->hyphen_lang.c_str();
    }
    *out_chapter = chapter;
    *out_layout = layout.release();
    return REPUB_OK;
}

void install_chapter(repub::repub_reader* reader, repub::repub_chapter* chapter,
                     repub::repub_layout* layout, size_t chapter_index, size_t page) {
    delete reader->chapter;
    delete reader->layout;
    reader->chapter = chapter;
    reader->layout = layout;
    reader->chapter_index = chapter_index;
    reader->layout_ready = true;
    if (!layout || layout->pages.empty()) {
        reader->page = 0;
    } else if (page >= layout->pages.size()) {
        reader->page = layout->pages.size() - 1;
    } else {
        reader->page = page;
    }
}

size_t page_for_text_offset(const repub::repub_layout* layout, uint32_t offset) {
    if (!layout || layout->pages.empty()) return 0;
    const size_t page = repub::api::layout_page_for_offset(layout, offset);
    return page < layout->pages.size() ? page : layout->pages.size() - 1;
}

const repub::repub_page* current_page(const repub::repub_reader* reader) {
    if (!reader || !reader->layout) return nullptr;
    if (reader->page >= reader->layout->pages.size()) return nullptr;
    return &reader->layout->pages[reader->page];
}

uint32_t find_anchor(const repub::repub_chapter* chapter, std::string anchor) {
    if (!chapter || anchor.empty()) return 0;
    while (!anchor.empty() && anchor.front() == '#') anchor.erase(anchor.begin());
    if (anchor.empty()) return 0;
    for (const repub::FlowAnchor& entry : chapter->flow.anchors) {
        if (entry.id == anchor) return entry.offset;
    }
    return 0;
}

bool rect_contains(const repub_rect_t& rect, int32_t x, int32_t y) {
    if (rect.w <= 0 || rect.h <= 0) return false;
    return x >= rect.x && y >= rect.y && x - rect.x < rect.w && y - rect.y < rect.h;
}

float clamp_fraction(float value) {
    if (!(value >= 0.0f)) value = 0.0f; /* also catches NaN */
    if (value > 1.0f) value = 1.0f;
    return value;
}

/* A synthetic chapter whose Flow holds plain note/popup text. */
std::unique_ptr<repub::repub_chapter> make_text_chapter(const repub::repub_reader* reader,
                                                        const std::string& text) {
    std::unique_ptr<repub::repub_chapter> chapter(new (std::nothrow) repub::repub_chapter());
    if (!chapter) return nullptr;
    chapter->book = reader->book;
    chapter->spine_index = reader->chapter ? reader->chapter->spine_index : 0;
    repub::api::build_synthetic_flow(repub::api::normalize_line_endings(text),
                                     &chapter->flow);
    return chapter;
}

void resolve_layout_hyphenation(const std::string& lang, repub::repub_layout* layout) {
    if (!layout) return;
    if (!lang.empty()) {
        layout->hyphen_lang = lang;
        layout->spec.hyphenation_lang = layout->hyphen_lang.c_str();
    } else {
        layout->hyphen_lang.clear();
        layout->spec.hyphenation_lang = nullptr;
    }
}

}  // namespace

/* ------------------------------------------------------------------------- */
/* Reader lifecycle                                                          */
/* ------------------------------------------------------------------------- */

namespace repub {

repub_err_t reader_prepare_layout_spec(repub_reader* reader,
                                       const repub_layout_spec_t* requested) {
    if (!reader) return REPUB_ERR_INVALID_ARG;
    repub_rect_t fallback{};
    fallback.x = 0;
    fallback.y = 0;
    fallback.w = api::kDefaultPageWidth;
    fallback.h = api::kDefaultPageHeight;

    repub_layout_spec_t prepared{};
    const repub_err_t rc = api::prepare_layout_spec(
        requested, requested ? nullptr : &fallback, &prepared);
    if (rc != REPUB_OK) return rc;

    reader->spec = prepared;
    if (prepared.hyphenation_lang && *prepared.hyphenation_lang) {
        reader->hyphen_lang = prepared.hyphenation_lang;
        reader->spec.hyphenation_lang = reader->hyphen_lang.c_str();
    } else {
        reader->hyphen_lang.clear();
        reader->spec.hyphenation_lang = nullptr;
    }
    return REPUB_OK;
}

repub_err_t reader_load_chapter(repub_reader* reader, size_t chapter_index,
                                uint32_t text_offset) {
    if (!reader) return REPUB_ERR_INVALID_ARG;
    repub_chapter* chapter = nullptr;
    repub_layout* layout = nullptr;
    const repub_err_t rc =
        build_chapter_layout(reader, chapter_index, &chapter, &layout);
    if (rc != REPUB_OK) return rc;
    const size_t page = page_for_text_offset(layout, text_offset);
    install_chapter(reader, chapter, layout, chapter_index, page);
    return REPUB_OK;
}

repub_err_t reader_ensure_layout(repub_reader* reader) {
    if (!reader) return REPUB_ERR_INVALID_ARG;
    if (reader->layout && reader->chapter) return REPUB_OK;
    if (!reader->chapter) return reader_load_chapter(reader, 0, 0);
    if (!reader->book || !reader->book->fonts) return REPUB_ERR_INTERNAL;
    std::unique_ptr<repub_layout> layout(new (std::nothrow) repub_layout());
    if (!layout) return REPUB_ERR_MEMORY;
    repub_layout_spec_t spec = reader->spec;
    spec.struct_size = static_cast<uint32_t>(sizeof(spec));
    spec.hyphenation_lang =
        reader->hyphen_lang.empty() ? nullptr : reader->hyphen_lang.c_str();
    const repub_err_t rc =
        layout_build(*reader->chapter, spec, *reader->book->fonts, layout.get());
    if (rc != REPUB_OK) return rc;
    layout->chapter = reader->chapter;
    resolve_layout_hyphenation(reader->hyphen_lang, layout.get());
    reader->layout = layout.release();
    reader->layout_ready = true;
    reader->page = 0;
    return REPUB_OK;
}

}  // namespace repub

repub_reader_t* repub_reader_attach(repub_book_t* book,
                                    const repub_layout_spec_t* layout,
                                    repub_err_t* err) {
    if (err) *err = REPUB_OK;
    repub::repub_book* b = repub::api::cast_book(book);
    if (!b) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
    std::unique_ptr<repub::repub_reader> reader(new (std::nothrow) repub::repub_reader());
    if (!reader) {
        repub::book_close(b); /* attach owns the book even on failure */
        if (err) *err = REPUB_ERR_MEMORY;
        return nullptr;
    }
    reader->book = b;
    reader->platform =
        b->platform ? b->platform : repub::effective_platform(nullptr);
    reader->parse_options = repub::api::make_parse_options(*b);

    repub_err_t rc = repub::reader_prepare_layout_spec(reader.get(), layout);
    if (rc != REPUB_OK) {
        repub::book_close(b);
        if (err) *err = rc;
        return nullptr;
    }
    if (b->spine.empty()) {
        repub::book_close(b);
        if (err) *err = REPUB_ERR_NOT_FOUND;
        return nullptr;
    }
    rc = repub::reader_load_chapter(reader.get(), 0, 0);
    if (rc != REPUB_OK) {
        repub::book_close(b);
        if (err) *err = rc;
        return nullptr;
    }
    return uncast_reader(reader.release());
}

repub_reader_t* repub_reader_open(const repub_platform_t* platform, const char* path,
                                  const repub_layout_spec_t* layout,
                                  const repub_open_options_t* options,
                                  repub_err_t* err) {
    if (err) *err = REPUB_OK;
    if (!path) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
    repub_err_t rc = REPUB_OK;
    repub_book_t* book = repub_book_open(platform, path, options, &rc);
    if (!book) {
        if (err) *err = rc != REPUB_OK ? rc : REPUB_ERR_INTERNAL;
        return nullptr;
    }
    return repub_reader_attach(book, layout, err);
}

void repub_reader_close(repub_reader_t* reader) {
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r) return;
    repub::repub_book* book = r->book;
    r->book = nullptr;
    delete r->layout;
    delete r->chapter;
    r->layout = nullptr;
    r->chapter = nullptr;
    if (book) repub::book_close(book);
    delete r;
}

repub_book_t* repub_reader_book(repub_reader_t* reader) {
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !r->book) return nullptr;
    return reinterpret_cast<repub_book_t*>(r->book);
}

/* ------------------------------------------------------------------------- */
/* Layout / navigation                                                       */
/* ------------------------------------------------------------------------- */

repub_err_t repub_reader_set_layout(repub_reader_t* reader,
                                    const repub_layout_spec_t* layout) {
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r) return REPUB_ERR_INVALID_ARG;
    if (!layout) return REPUB_ERR_INVALID_ARG;

    const repub_layout_spec_t old_spec = r->spec;
    const std::string old_lang = r->hyphen_lang;
    const uint32_t offset = r->layout ? repub::api::layout_page_start_offset(r->layout, r->page) : 0;

    repub_err_t rc = repub::reader_prepare_layout_spec(r, layout);
    if (rc != REPUB_OK) {
        r->spec = old_spec;
        r->hyphen_lang = old_lang;
        r->spec.hyphenation_lang =
            r->hyphen_lang.empty() ? nullptr : r->hyphen_lang.c_str();
        return rc;
    }
    if (!r->chapter) return REPUB_OK;

    rc = repub::reader_load_chapter(r, r->chapter_index, offset);
    if (rc != REPUB_OK) {
        r->spec = old_spec;
        r->hyphen_lang = old_lang;
        r->spec.hyphenation_lang =
            r->hyphen_lang.empty() ? nullptr : r->hyphen_lang.c_str();
        return rc;
    }
    if (r->layout) r->layout->generation++;
    return REPUB_OK;
}

const repub_layout_spec_t* repub_reader_layout_spec(const repub_reader_t* reader) {
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r) return nullptr;
    return &r->spec;
}

size_t repub_reader_chapter_index(const repub_reader_t* reader) {
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    return r ? r->chapter_index : 0;
}

size_t repub_reader_chapter_count(const repub_reader_t* reader) {
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !r->book) return 0;
    return r->book->spine.size();
}

size_t repub_reader_page_count(const repub_reader_t* reader) {
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    return repub::api::layout_page_count(r ? r->layout : nullptr);
}

size_t repub_reader_page_index(const repub_reader_t* reader) {
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    return r ? r->page : 0;
}

repub_err_t repub_reader_goto_chapter(repub_reader_t* reader, size_t chapter) {
    if (!reader) return REPUB_ERR_INVALID_ARG;
    return repub::reader_load_chapter(repub::api::cast_reader(reader), chapter, 0);
}

repub_err_t repub_reader_goto_offset(repub_reader_t* reader, size_t chapter,
                                     uint32_t text_offset) {
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r) return REPUB_ERR_INVALID_ARG;
    if (r->chapter && r->layout && chapter == r->chapter_index) {
        r->page = page_for_text_offset(r->layout, text_offset);
        return REPUB_OK;
    }
    return repub::reader_load_chapter(r, chapter, text_offset);
}

repub_err_t repub_reader_goto_href(repub_reader_t* reader, const char* href) {
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !href) return REPUB_ERR_INVALID_ARG;
    if (!r->book) return REPUB_ERR_INTERNAL;

    int32_t spine_index = -1;
    std::string anchor;
    repub_err_t rc = repub::book_resolve_href(*r->book, href, &spine_index, &anchor);
    if (rc != REPUB_OK) return rc;
    if (spine_index < 0) return REPUB_ERR_NOT_FOUND;
    const size_t target = static_cast<size_t>(spine_index);

    if (!r->chapter || target != r->chapter_index) {
        repub::repub_chapter* chapter = nullptr;
        repub::repub_layout* layout = nullptr;
        rc = build_chapter_layout(r, target, &chapter, &layout);
        if (rc != REPUB_OK) return rc;
        const uint32_t offset = find_anchor(chapter, anchor);
        const size_t page = page_for_text_offset(layout, offset);
        install_chapter(r, chapter, layout, target, page);
        return REPUB_OK;
    }
    const uint32_t offset = find_anchor(r->chapter, anchor);
    r->page = page_for_text_offset(r->layout, offset);
    return REPUB_OK;
}

repub_err_t repub_reader_goto_progress(repub_reader_t* reader, float fraction) {
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !r->book) return REPUB_ERR_INVALID_ARG;
    if (r->book->spine.empty()) return REPUB_ERR_NOT_FOUND;
    const float clamped = clamp_fraction(fraction);
    const size_t count = r->book->spine.size();

    size_t target_chapter = 0;
    double within = 0.0;
    if (r->book->content_bytes == 0) {
        target_chapter = static_cast<size_t>(clamped * static_cast<double>(count));
        if (target_chapter >= count) target_chapter = count - 1;
    } else {
        const double target = static_cast<double>(clamped) *
                              static_cast<double>(r->book->content_bytes);
        target_chapter = count - 1;
        for (size_t i = 0; i < count; ++i) {
            const uint32_t start = repub::book_spine_byte_offset(*r->book, i);
            const uint32_t end =
                i + 1 < count ? repub::book_spine_byte_offset(*r->book, i + 1)
                              : static_cast<uint32_t>(r->book->content_bytes);
            if (end <= start) continue;
            if (target < static_cast<double>(end) || i + 1 == count) {
                target_chapter = i;
                const double relative = target - static_cast<double>(start);
                within = relative <= 0.0 ? 0.0 : relative / static_cast<double>(end - start);
                if (within > 1.0) within = 1.0;
                break;
            }
        }
    }

    repub::repub_chapter* chapter = nullptr;
    repub::repub_layout* layout = nullptr;
    repub_err_t rc = build_chapter_layout(r, target_chapter, &chapter, &layout);
    if (rc != REPUB_OK) return rc;
    size_t offset = static_cast<size_t>(within * static_cast<double>(chapter->flow.text.size()));
    if (offset > chapter->flow.text.size()) offset = chapter->flow.text.size();
    const size_t page = page_for_text_offset(layout, static_cast<uint32_t>(offset));
    install_chapter(r, chapter, layout, target_chapter, page);
    return REPUB_OK;
}

repub_err_t repub_reader_next_page(repub_reader_t* reader) {
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r) return REPUB_ERR_INVALID_ARG;
    if (!r->layout || !r->book) return REPUB_ERR_INVALID_ARG;
    if (r->page + 1 < r->layout->pages.size()) {
        ++r->page;
        return REPUB_OK;
    }
    const size_t count = r->book->spine.size();
    for (size_t i = r->chapter_index + 1; i < count; ++i) {
        repub::repub_chapter* chapter = nullptr;
        repub::repub_layout* layout = nullptr;
        const repub_err_t rc = build_chapter_layout(r, i, &chapter, &layout);
        if (rc != REPUB_OK) continue; /* skip unreadable spine items */
        if (repub::api::chapter_is_empty(chapter)) {
            delete chapter;
            delete layout;
            continue;
        }
        install_chapter(r, chapter, layout, i, 0);
        return REPUB_OK;
    }
    return REPUB_ERR_NOT_FOUND;
}

repub_err_t repub_reader_prev_page(repub_reader_t* reader) {
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r) return REPUB_ERR_INVALID_ARG;
    if (!r->layout || !r->book) return REPUB_ERR_INVALID_ARG;
    if (r->page > 0) {
        --r->page;
        return REPUB_OK;
    }
    size_t i = r->chapter_index;
    while (i > 0) {
        --i;
        repub::repub_chapter* chapter = nullptr;
        repub::repub_layout* layout = nullptr;
        const repub_err_t rc = build_chapter_layout(r, i, &chapter, &layout);
        if (rc != REPUB_OK) continue;
        if (repub::api::chapter_is_empty(chapter)) {
            delete chapter;
            delete layout;
            continue;
        }
        const size_t last = layout->pages.empty() ? 0 : layout->pages.size() - 1;
        install_chapter(r, chapter, layout, i, last);
        return REPUB_OK;
    }
    return REPUB_ERR_NOT_FOUND;
}

const repub_page_t* repub_reader_page(const repub_reader_t* reader) {
    const repub::repub_page* page = current_page(repub::api::cast_reader(reader));
    return repub::api::uncast_page(page);
}

const repub_layout_t* repub_reader_layout(const repub_reader_t* reader) {
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !r->layout) return nullptr;
    return repub::api::uncast_layout(r->layout);
}

const repub_chapter_t* repub_reader_chapter(const repub_reader_t* reader) {
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !r->chapter) return nullptr;
    return uncast_chapter(r->chapter);
}

repub_err_t repub_reader_render(repub_reader_t* reader, repub_canvas_t* canvas,
                                const repub_render_options_t* options) {
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !canvas) return REPUB_ERR_INVALID_ARG;
    const repub::repub_page* page = current_page(r);
    if (!page) return REPUB_ERR_INVALID_ARG;
    return repub_page_render(repub::api::uncast_page(page), canvas, options);
}

float repub_reader_progress(const repub_reader_t* reader) {
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !r->book || !r->layout) return 0.0f;
    if (r->book->content_bytes == 0) return 0.0f;
    const uint64_t before =
        repub::book_spine_byte_offset(*r->book, r->chapter_index);
    const uint32_t page_offset = repub::api::layout_page_start_offset(r->layout, r->page);
    double value = (static_cast<double>(before) + static_cast<double>(page_offset)) /
                   static_cast<double>(r->book->content_bytes);
    if (!(value >= 0.0)) value = 0.0;
    if (value > 1.0) value = 1.0;
    return static_cast<float>(value);
}

/* ------------------------------------------------------------------------- */
/* Footnotes                                                                 */
/* ------------------------------------------------------------------------- */

int32_t repub_reader_note_at(const repub_reader_t* reader, int32_t x, int32_t y) {
    const repub::repub_page* page = current_page(repub::api::cast_reader(reader));
    if (!page) return -1;
    int32_t best = -1;
    long long best_area = 0;
    for (const repub_page_note_marker_t& marker : page->notes) {
        if (!rect_contains(marker.rect, x, y)) continue;
        const long long area =
            static_cast<long long>(marker.rect.w) * static_cast<long long>(marker.rect.h);
        if (best < 0 || area <= best_area) {
            best = marker.note_index;
            best_area = area;
        }
    }
    return best;
}

repub_err_t repub_reader_note_text(const repub_reader_t* reader, size_t note_index,
                                   const char** text, size_t* len) {
    if (!text || !len) return REPUB_ERR_INVALID_ARG;
    *text = nullptr;
    *len = 0;
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !r->chapter) return REPUB_ERR_INVALID_ARG;
    if (note_index >= r->chapter->flow.notes.size()) return REPUB_ERR_NOT_FOUND;
    const repub::FlowNote& note = r->chapter->flow.notes[note_index];
    *text = note.text.c_str();
    *len = note.text.size();
    return REPUB_OK;
}

repub_note_popup_t* repub_reader_note_popup_open(repub_reader_t* reader,
                                                 size_t note_index,
                                                 const repub_layout_spec_t* popup_spec,
                                                 repub_err_t* err) {
    if (err) *err = REPUB_OK;
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !r->book || !r->book->fonts) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
    if (!r->chapter) {
        if (err) *err = REPUB_ERR_NOT_FOUND;
        return nullptr;
    }
    if (note_index >= r->chapter->flow.notes.size()) {
        if (err) *err = REPUB_ERR_NOT_FOUND;
        return nullptr;
    }

    repub_layout_spec_t spec{};
    repub_err_t rc = REPUB_OK;
    if (!popup_spec) {
        spec = r->spec;
    } else {
        const repub_rect_t fallback = r->spec.page_rect;
        rc = repub::api::prepare_layout_spec(popup_spec, &fallback, &spec);
        if (rc != REPUB_OK) {
            if (err) *err = rc;
            return nullptr;
        }
    }
    /* The popup layout needs its hyphenation language to outlive this call. */
    std::string lang = spec.hyphenation_lang ? std::string(spec.hyphenation_lang)
                                             : std::string();
    if (lang.empty() && !r->hyphen_lang.empty()) lang = r->hyphen_lang;
    spec.hyphenation_lang = lang.empty() ? nullptr : lang.c_str();

    const std::string note_text = r->chapter->flow.notes[note_index].text;
    std::unique_ptr<repub::repub_chapter> chapter = make_text_chapter(r, note_text);
    if (!chapter) {
        if (err) *err = REPUB_ERR_MEMORY;
        return nullptr;
    }
    std::unique_ptr<repub::repub_layout> layout(new (std::nothrow) repub::repub_layout());
    if (!layout) {
        if (err) *err = REPUB_ERR_MEMORY;
        return nullptr;
    }
    rc = repub::layout_build_flow(*chapter, chapter->flow, spec, *r->book->fonts,
                                  layout.get());
    if (rc != REPUB_OK) {
        if (err) *err = rc;
        return nullptr;
    }
    layout->chapter = chapter.get();
    resolve_layout_hyphenation(lang, layout.get());

    std::unique_ptr<repub::repub_note_popup> popup(
        new (std::nothrow) repub::repub_note_popup());
    if (!popup) {
        if (err) *err = REPUB_ERR_MEMORY;
        return nullptr;
    }
    popup->reader = r;
    popup->note_index = note_index;
    popup->chapter = chapter.release();
    popup->layout = layout.release();
    return uncast_popup(popup.release());
}

void repub_note_popup_free(repub_note_popup_t* popup_handle) {
    repub::repub_note_popup* popup = repub::api::cast_popup(popup_handle);
    if (!popup) return;
    delete popup->layout;
    delete popup->chapter;
    delete popup;
}

size_t repub_note_popup_page_count(const repub_note_popup_t* popup_handle) {
    const repub::repub_note_popup* popup = repub::api::cast_popup(popup_handle);
    return repub::api::layout_page_count(popup ? popup->layout : nullptr);
}

repub_err_t repub_note_popup_render(const repub_note_popup_t* popup_handle,
                                    size_t page, repub_canvas_t* canvas,
                                    const repub_render_options_t* options) {
    const repub::repub_note_popup* popup = repub::api::cast_popup(popup_handle);
    if (!popup || !popup->layout || !canvas) return REPUB_ERR_INVALID_ARG;
    if (page >= popup->layout->pages.size()) return REPUB_ERR_NOT_FOUND;
    return repub_page_render(
        repub::api::uncast_page(&popup->layout->pages[page]), canvas, options);
}

const repub_layout_t* repub_note_popup_layout(const repub_note_popup_t* popup_handle) {
    const repub::repub_note_popup* popup = repub::api::cast_popup(popup_handle);
    if (!popup || !popup->layout) return nullptr;
    return repub::api::uncast_layout(popup->layout);
}

/* ------------------------------------------------------------------------- */
/* Images                                                                    */
/* ------------------------------------------------------------------------- */

int32_t repub_reader_image_at(const repub_reader_t* reader, int32_t x, int32_t y) {
    const repub::repub_page* page = current_page(repub::api::cast_reader(reader));
    if (!page) return -1;
    int32_t best = -1;
    long long best_area = 0;
    for (const repub_page_image_t& image : page->images) {
        if (!rect_contains(image.rect, x, y)) continue;
        const long long area =
            static_cast<long long>(image.rect.w) * static_cast<long long>(image.rect.h);
        if (best < 0 || area <= best_area) {
            best = image.image_index;
            best_area = area;
        }
    }
    return best;
}

repub_err_t repub_reader_image_decode(const repub_reader_t* reader,
                                      size_t image_index, int32_t max_width,
                                      int32_t max_height, repub_image_t** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    const repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r || !r->chapter || !r->book) return REPUB_ERR_INVALID_ARG;
    if (image_index >= r->chapter->flow.images.size()) return REPUB_ERR_NOT_FOUND;
    const std::string& href = r->chapter->flow.images[image_index].href;
    if (href.empty()) return REPUB_ERR_NOT_FOUND;

    std::vector<uint8_t> data;
    repub_err_t rc = repub::book_read_resource(*r->book, href, &data,
                                               REPUB_MAX_IMAGE_BYTES);
    if (rc != REPUB_OK) return rc;
    repub::repub_image* image = nullptr;
    rc = repub::image_decode(repub::api::platform_of(r->book), data.data(), data.size(),
                             max_width, max_height, &image);
    if (rc != REPUB_OK) return rc;
    *out = repub::api::uncast_image(image);
    return REPUB_OK;
}

repub_image_viewer_t* repub_reader_image_viewer_open(repub_reader_t* reader,
                                                     size_t image_index,
                                                     repub_err_t* err) {
    if (err) *err = REPUB_OK;
    repub::repub_reader* r = repub::api::cast_reader(reader);
    if (!r) {
        if (err) *err = REPUB_ERR_INVALID_ARG;
        return nullptr;
    }
    repub_image_t* image = nullptr;
    repub_err_t rc = repub_reader_image_decode(reader, image_index, 0, 0, &image);
    if (rc != REPUB_OK) {
        if (err) *err = rc;
        return nullptr;
    }
    std::unique_ptr<repub::repub_image_viewer> viewer(
        new (std::nothrow) repub::repub_image_viewer());
    if (!viewer) {
        repub_image_free(image);
        if (err) *err = REPUB_ERR_MEMORY;
        return nullptr;
    }
    viewer->reader = r;
    viewer->image_index = static_cast<int32_t>(image_index);
    viewer->image = repub::api::cast_image(image);
    return uncast_viewer(viewer.release());
}

void repub_image_viewer_free(repub_image_viewer_t* viewer_handle) {
    repub::repub_image_viewer* viewer = repub::api::cast_viewer(viewer_handle);
    if (!viewer) return;
    if (viewer->image) repub::image_free(viewer->image);
    delete viewer;
}

const repub_image_t* repub_image_viewer_image(const repub_image_viewer_t* viewer_handle) {
    const repub::repub_image_viewer* viewer = repub::api::cast_viewer(viewer_handle);
    if (!viewer || !viewer->image) return nullptr;
    return repub::api::uncast_image(viewer->image);
}

repub_err_t repub_image_viewer_render(repub_image_viewer_t* viewer_handle,
                                      repub_canvas_t* canvas, repub_rect_t dest,
                                      float zoom, float pan_x, float pan_y,
                                      const repub_render_options_t* options) {
    repub::repub_image_viewer* viewer = repub::api::cast_viewer(viewer_handle);
    repub::repub_canvas* c = repub::api::cast_canvas(canvas);
    if (!viewer || !viewer->image || !c) return REPUB_ERR_INVALID_ARG;
#if !REPUB_ENABLE_IMAGE_ZOOM
    return REPUB_ERR_UNSUPPORTED;
#else
    repub_render_options_t opts;
    repub_err_t rc = repub::api::prepare_render_options(options, &opts);
    if (rc != REPUB_OK) return rc;

    repub_image_view_t view{};
    view.struct_size = static_cast<uint32_t>(sizeof(view));
    if (dest.w > 0 && dest.h > 0) {
        view.dest = dest;
    } else {
        view.dest = repub_rect_t{0, 0, c->width, c->height};
    }
    float z = zoom;
    if (!(z >= 1.0f)) z = 1.0f; /* also catches NaN */
    if (z > 16.0f) z = 16.0f;
    view.zoom = z;
    if (!(pan_x >= -1.0f)) pan_x = -1.0f;
    if (!(pan_x <= 1.0f)) pan_x = 1.0f;
    if (!(pan_y >= -1.0f)) pan_y = -1.0f;
    if (!(pan_y <= 1.0f)) pan_y = 1.0f;
    view.pan_x = pan_x;
    view.pan_y = pan_y;
    view.fit = REPUB_IMAGE_FIT_CONTAIN;
    view.dither = opts.text_dither;
    view.background_level = opts.background_level >= 0 ? opts.background_level
                                                       : c->max_level;
    return repub::canvas_draw_image(c, viewer->image, &view);
#endif
}
