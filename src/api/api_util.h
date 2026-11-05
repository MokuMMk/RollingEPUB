/*
 * RollingEPUB API layer private helpers.
 *
 * Shared by the API translation units: casting between the public opaque C
 * types (global
 * namespace) and the internal C++ definitions (namespace repub), struct_size
 * aware option/spec preparation, parse-option derivation and synthetic flows
 * for popups / text layout.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_API_API_UTIL_H
#define REPUB_SRC_API_API_UTIL_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "api/api_internal.h"
#include "container/book.h"
#include "content/flow.h"
#include "core/base.h"
#include "layout/layout.h"
#include "render/font.h"
#include "render/image.h"
#include "render/page_render.h"
#include "rolling_epub/repub.h"

namespace repub {
namespace api {

/* Default page geometry used by repub_reader_open when no layout is given. */
constexpr int32_t kDefaultPageWidth = 480;
constexpr int32_t kDefaultPageHeight = 800;

/* Minimal acceptable sizes for public struct_size'd structures.  A size of 0
 * means "all defaults"; any nonzero size smaller than the minimum is invalid
 * (the caller built against a different/older ABI). */
constexpr size_t kLayoutSpecMinSize =
    offsetof(repub_layout_spec_t, page_rect) + sizeof(repub_rect_t);
constexpr size_t kRenderOptionsMinSize = offsetof(repub_render_options_t, flags);
constexpr size_t kImageViewMinSize =
    offsetof(repub_image_view_t, dest) + sizeof(repub_rect_t);

/* ---- Opaque type casts ------------------------------------------------- */

inline repub_book* cast_book(::repub_book* book) {
    return reinterpret_cast<repub_book*>(book);
}
inline const repub_book* cast_book(const ::repub_book* book) {
    return reinterpret_cast<const repub_book*>(book);
}
inline repub_chapter* cast_chapter(::repub_chapter* chapter) {
    return reinterpret_cast<repub_chapter*>(chapter);
}
inline const repub_chapter* cast_chapter(const ::repub_chapter* chapter) {
    return reinterpret_cast<const repub_chapter*>(chapter);
}
inline repub_page* cast_page(::repub_page* page) {
    return reinterpret_cast<repub_page*>(page);
}
inline const repub_page* cast_page(const ::repub_page* page) {
    return reinterpret_cast<const repub_page*>(page);
}
inline repub_layout* cast_layout(::repub_layout* layout) {
    return reinterpret_cast<repub_layout*>(layout);
}
inline const repub_layout* cast_layout(const ::repub_layout* layout) {
    return reinterpret_cast<const repub_layout*>(layout);
}
inline repub_canvas* cast_canvas(::repub_canvas* canvas) {
    return reinterpret_cast<repub_canvas*>(canvas);
}
inline const repub_canvas* cast_canvas(const ::repub_canvas* canvas) {
    return reinterpret_cast<const repub_canvas*>(canvas);
}
inline repub_image* cast_image(::repub_image* image) {
    return reinterpret_cast<repub_image*>(image);
}
inline const repub_image* cast_image(const ::repub_image* image) {
    return reinterpret_cast<const repub_image*>(image);
}
inline repub_reader* cast_reader(::repub_reader* reader) {
    return reinterpret_cast<repub_reader*>(reader);
}
inline const repub_reader* cast_reader(const ::repub_reader* reader) {
    return reinterpret_cast<const repub_reader*>(reader);
}
inline repub_note_popup* cast_popup(::repub_note_popup* popup) {
    return reinterpret_cast<repub_note_popup*>(popup);
}
inline const repub_note_popup* cast_popup(const ::repub_note_popup* popup) {
    return reinterpret_cast<const repub_note_popup*>(popup);
}
inline repub_image_viewer* cast_viewer(::repub_image_viewer* viewer) {
    return reinterpret_cast<repub_image_viewer*>(viewer);
}
inline const repub_image_viewer* cast_viewer(const ::repub_image_viewer* viewer) {
    return reinterpret_cast<const repub_image_viewer*>(viewer);
}

inline ::repub_page* uncast_page(repub_page* page) {
    return reinterpret_cast<::repub_page*>(page);
}
inline const ::repub_page* uncast_page(const repub_page* page) {
    return reinterpret_cast<const ::repub_page*>(page);
}
inline ::repub_layout* uncast_layout(repub_layout* layout) {
    return reinterpret_cast<::repub_layout*>(layout);
}
inline ::repub_chapter* uncast_chapter(repub_chapter* chapter) {
    return reinterpret_cast<::repub_chapter*>(chapter);
}
inline ::repub_canvas* uncast_canvas(repub_canvas* canvas) {
    return reinterpret_cast<::repub_canvas*>(canvas);
}
inline ::repub_image* uncast_image(repub_image* image) {
    return reinterpret_cast<::repub_image*>(image);
}
inline ::repub_reader* uncast_reader(repub_reader* reader) {
    return reinterpret_cast<::repub_reader*>(reader);
}
inline ::repub_note_popup* uncast_popup(repub_note_popup* popup) {
    return reinterpret_cast<::repub_note_popup*>(popup);
}
inline ::repub_image_viewer* uncast_viewer(repub_image_viewer* viewer) {
    return reinterpret_cast<::repub_image_viewer*>(viewer);
}

/* ---- struct_size aware copies ----------------------------------------- */

/*
 * Copy a caller struct into a zeroed destination.
 *  - src == NULL, or src->struct_size == 0: destination stays zeroed.
 *  - 0 < struct_size < min_size: REPUB_ERR_INVALID_ARG.
 *  - otherwise min(struct_size, dst_size) bytes are copied.
 * The first field of every public struct is struct_size.
 */
template <typename T>
inline repub_err_t copy_caller_struct(const T* src, void* dst, size_t dst_size,
                                      size_t min_size) {
    std::memset(dst, 0, dst_size);
    if (!src) return REPUB_OK;
    const size_t size = static_cast<size_t>(src->struct_size);
    if (size == 0) return REPUB_OK;
    if (size < min_size) return REPUB_ERR_INVALID_ARG;
    std::memcpy(dst, src, size < dst_size ? size : dst_size);
    return REPUB_OK;
}

/*
 * Fill *out with the documented layout defaults (page_rect left untouched)
 * and overlay any explicitly requested values.  Zero numeric/boolean fields in
 * the request mean "use the default".  page_rect must end up positive; when it
 * is not, fallback_rect (when non-null) is used instead, otherwise
 * REPUB_ERR_INVALID_ARG is returned.
 */
inline repub_err_t prepare_layout_spec(const repub_layout_spec_t* requested,
                                       const repub_rect_t* fallback_rect,
                                       repub_layout_spec_t* out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    std::memset(out, 0, sizeof(*out));
    repub_layout_spec_defaults(out);

    repub_layout_spec_t req{};
    const repub_err_t err =
        copy_caller_struct(requested, &req, sizeof(req), kLayoutSpecMinSize);
    if (err != REPUB_OK) return err;

    if (req.page_rect.w > 0 && req.page_rect.h > 0) out->page_rect = req.page_rect;
    if (req.font_size_px > 0) out->font_size_px = req.font_size_px;
    if (req.line_height_percent > 0) out->line_height_percent = req.line_height_percent;
    if (req.paragraph_gap_percent > 0) {
        out->paragraph_gap_percent = req.paragraph_gap_percent;
    }
    if (req.tracking_px != 0) out->tracking_px = req.tracking_px;
    if (req.first_line_indent_em > 0) out->first_line_indent_em = req.first_line_indent_em;
    if (req.margin_left_px > 0) out->margin_left_px = req.margin_left_px;
    if (req.margin_right_px > 0) out->margin_right_px = req.margin_right_px;
    if (req.margin_top_px > 0) out->margin_top_px = req.margin_top_px;
    if (req.margin_bottom_px > 0) out->margin_bottom_px = req.margin_bottom_px;
    if (req.writing_mode == REPUB_WRITING_VERTICAL_RL) {
        out->writing_mode = REPUB_WRITING_VERTICAL_RL;
    }
    if (req.image_full_bleed != 0) out->image_full_bleed = req.image_full_bleed;
    if (req.chapter_heading_height_px > 0) {
        out->chapter_heading_height_px = req.chapter_heading_height_px;
    }
    if (req.max_pages > 0) out->max_pages = req.max_pages;
    if (req.hyphenation_lang != nullptr) out->hyphenation_lang = req.hyphenation_lang;

    if (out->page_rect.w <= 0 || out->page_rect.h <= 0) {
        if (fallback_rect && fallback_rect->w > 0 && fallback_rect->h > 0) {
            out->page_rect = *fallback_rect;
        }
    }
    if (out->page_rect.w <= 0 || out->page_rect.h <= 0) return REPUB_ERR_INVALID_ARG;

    /* Clamp documented ranges so the layout engine never sees nonsense. */
    if (out->font_size_px < 1) out->font_size_px = 22;
    if (out->font_size_px > 512) out->font_size_px = 512;
    if (out->line_height_percent < 100) out->line_height_percent = 100;
    if (out->line_height_percent > 250) out->line_height_percent = 250;
    if (out->paragraph_gap_percent < 0) out->paragraph_gap_percent = 0;
    if (out->paragraph_gap_percent > 200) out->paragraph_gap_percent = 200;
    if (out->tracking_px < -8) out->tracking_px = -8;
    if (out->tracking_px > 8) out->tracking_px = 8;
    if (out->first_line_indent_em < 0) out->first_line_indent_em = 0;
    if (out->first_line_indent_em > 3) out->first_line_indent_em = 3;
    if (out->max_pages < 0) out->max_pages = 0;
    if (out->max_pages == 0) out->max_pages = static_cast<int32_t>(REPUB_MAX_PAGES);
    out->struct_size = static_cast<uint32_t>(sizeof(*out));
    return REPUB_OK;
}

/*
 * Fill *out with documented render-option defaults and overlay explicit
 * request values (zero means default).
 */
inline repub_err_t prepare_render_options(const repub_render_options_t* requested,
                                          repub_render_options_t* out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    repub_render_options_defaults(out);

    repub_render_options_t req{};
    const repub_err_t err = copy_caller_struct(requested, &req, sizeof(req),
                                               kRenderOptionsMinSize);
    if (err != REPUB_OK) return err;

    if (req.flags != 0) out->flags = req.flags;
    if (req.text_dither != REPUB_DITHER_NONE) out->text_dither = req.text_dither;
    if (req.text_level != 0) out->text_level = req.text_level;
    if (req.background_level != 0) out->background_level = req.background_level;
    if (req.threshold != 0) out->threshold = req.threshold;
    if (out->text_level < 0) out->text_level = 0;
    if (out->threshold <= 0) out->threshold = 128;
    if (out->threshold > 255) out->threshold = 255;
    out->struct_size = static_cast<uint32_t>(sizeof(*out));
    return REPUB_OK;
}

/* ---- Parsing options --------------------------------------------------- */

/* Build the chapter parse options from the book's open options + features. */
inline ParseOptions make_parse_options(const repub_book& book) {
    ParseOptions options;
#if REPUB_ENABLE_CSS
    options.css_enabled =
        book.options.struct_size == 0 ? true : (book.options.enable_css != 0);
#else
    options.css_enabled = false;
    (void)book;
#endif
#if REPUB_ENABLE_FOOTNOTES
    options.footnotes_enabled = true;
#else
    options.footnotes_enabled = false;
#endif
#if REPUB_ENABLE_IMAGES
    options.images_enabled = true;
#else
    options.images_enabled = false;
#endif
#if REPUB_ENABLE_TABLES
    options.tables_enabled = true;
#else
    options.tables_enabled = false;
#endif
#if REPUB_ENABLE_DROP_CAPS
    options.drop_caps_enabled = true;
#else
    options.drop_caps_enabled = false;
#endif
#if REPUB_ENABLE_RUBY
    options.ruby_enabled = true;
#else
    options.ruby_enabled = false;
#endif
    return options;
}

/* ---- Layout helpers ---------------------------------------------------- */

inline uint32_t layout_page_start_offset(const repub_layout* layout, size_t page) {
    if (!layout) return 0;
    if (page < layout->page_start_offsets.size()) {
        return layout->page_start_offsets[page];
    }
    if (page < layout->pages.size()) return layout->pages[page].info.start_offset;
    return 0;
}

inline size_t layout_page_for_offset(const repub_layout* layout, uint32_t offset) {
    if (!layout || layout->pages.empty()) return 0;
    const std::vector<uint32_t>& offsets = layout->page_start_offsets;
    if (offsets.size() == layout->pages.size()) {
        size_t lo = 0;
        size_t hi = offsets.size();
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (offsets[mid] <= offset) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo == 0 ? 0 : lo - 1;
    }
    size_t best = 0;
    for (size_t i = 0; i < layout->pages.size(); ++i) {
        if (layout->pages[i].info.start_offset <= offset) {
            best = i;
        } else {
            break;
        }
    }
    return best;
}

inline size_t layout_page_count(const repub_layout* layout) {
    return layout ? layout->pages.size() : 0;
}

inline const repub_page* layout_page(const repub_layout* layout, size_t page) {
    if (!layout || page >= layout->pages.size()) return nullptr;
    return &layout->pages[page];
}

/* True when a chapter has no readable content (nothing to navigate to). */
inline bool chapter_is_empty(const repub_chapter* chapter) {
    if (!chapter) return true;
    return chapter->flow.text.empty() && chapter->flow.images.empty();
}

/* ---- Synthetic flows (note popups, standalone text layout) ------------ */

/* Normalize CRLF/CR to LF; other bytes are preserved. */
inline std::string normalize_line_endings(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            out.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

/*
 * Build a paragraph-per-line Flow from plain UTF-8 text.  Used for note
 * popups and repub_book_layout_text().  Lines keep their '\n' separation by
 * becoming separate paragraph blocks (a single line yields one block).
 */
inline void build_synthetic_flow(const std::string& text, Flow* flow) {
    if (!flow) return;
    flow->text = text;
    flow->blocks.clear();
    flow->runs.clear();
    flow->notes.clear();
    flow->images.clear();
    flow->links.clear();
    flow->anchors.clear();
    flow->families.clear();
    flow->block_offsets.clear();

    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        const size_t stop = end == std::string::npos ? text.size() : end;
        if (stop > start) {
            FlowBlock block;
            block.offset = static_cast<uint32_t>(start);
            block.len = static_cast<uint32_t>(stop - start);
            block.type = REPUB_BLOCK_PARAGRAPH;
            block.flags = FBLOCK_NO_INDENT;
            block.font_scale_percent = 100;
            block.run_first = 0;
            block.run_count = 0;
            flow->blocks.push_back(block);
            flow->block_offsets.push_back(block.offset);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

/* Platform used for image decoding etc. (never NULL). */
inline const repub_platform_t* platform_of(const repub_book* book) {
    if (book && book->platform) return book->platform;
    return effective_platform(nullptr);
}

}  // namespace api
}  // namespace repub

#endif  // REPUB_SRC_API_API_UTIL_H
