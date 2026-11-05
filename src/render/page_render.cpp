/*
 * RollingEPUB page renderer.
 *
 * Draws a laid-out page into a canvas: background, text spans (horizontal and
 * vertical, with per-span face/size/style), images decoded from the book,
 * footnote icon markers, link underlines and optional debug boxes.
 *
 * Text antialiasing follows the canvas format:
 *   - GRAY4/GRAY8: glyph coverage is blended directly through
 *     repub_canvas::blend_pixel.
 *   - MONO1/GRAY2: each span is rasterized into a small 8-bit scratch canvas
 *     and then quantized with the requested dither mode/threshold, so
 *     grayscale antialiasing survives on 1-bit and 2-bit e-ink panels.
 *
 * Image decode failures are non-fatal: the reserved rectangle stays empty.
 *
 * SPDX-License-Identifier: MIT
 */

#include "render/page_render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "container/book.h"
#include "render/canvas.h"
#include "render/image.h"

namespace repub {

/* Per-pixel luminance quantizer shared with dither.cpp (the render headers
 * are Lead-owned, so this internal helper is declared here and defined in
 * dither.cpp rather than exposed in image.h). */
int dither_quantize_gray(int gray, repub_pixel_format_t format, int max_level, int x, int y,
                         repub_dither_t mode, int threshold);

namespace {

struct TextDraw {
    int face = 0;
    int px = 0;
    /* Horizontal: x = pen x, y = baseline.  Vertical: x = column left,
     * y = run top. */
    int x = 0;
    int y = 0;
    bool vertical = false;
    std::string_view text;
    uint32_t flags = 0;
    int color_level = 0;
};

/* Count UTF-8 codepoints (skip continuation bytes; a run's vertical extent is
 * one px cell per codepoint). */
int count_codepoints(std::string_view text) {
    int count = 0;
    for (char ch : text) {
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++count;
    }
    return count;
}

/* Render one text run through a coverage scratch canvas and dither it into
 * the destination.  Returns false when the scratch canvas cannot be created,
 * in which case the caller falls back to direct (un-dithered) drawing. */
bool draw_text_dithered(repub_canvas& canvas, const FontManager& fonts, const TextDraw& run,
                        repub_dither_t dither, int32_t threshold) {
    const int px = run.px;
    if (px <= 0) return false;
    const int asc = fonts.ascender(run.face, px);
    const int desc = fonts.descender(run.face, px);
    const int pad = px / 3 + 2;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    if (!run.vertical) {
        const int advance = fonts.text_width(run.face, px, run.text);
        bx = run.x - pad;
        by = run.y - asc - pad;
        bw = advance + pad * 2 + 2;
        bh = asc + desc + pad * 2 + 2;
    } else {
        bx = run.x - pad;
        by = run.y - pad;
        bw = px + pad * 2 + 2;
        bh = count_codepoints(run.text) * px + pad * 2 + 2;
    }
    if (bw <= 0 || bh <= 0) return false;
    if (bw > 8192 || bh > 8192) return false;

    repub_canvas* scratch = nullptr;
    const repub_err_t err = canvas_create(canvas.platform, bw, bh, REPUB_FORMAT_GRAY8, &scratch);
    if (err != REPUB_OK || !scratch) return false;
    scratch->clear(0);

    if (!run.vertical) {
        fonts.draw_text(scratch, run.face, px, run.x - bx, run.y - by, run.text, 255, run.flags);
    } else {
        fonts.draw_text_vertical(scratch, run.face, px, run.x - bx, run.y - by, run.text, 255,
                                 run.flags);
    }

    const int max_level = canvas.max_level;
    const int target_level = std::clamp(run.color_level, 0, max_level);
    const int target_gray = target_level * 255 / max_level;
    for (int y = 0; y < bh; ++y) {
        const int canvas_y = by + y;
        if (canvas_y < 0 || canvas_y >= canvas.height) continue;
        for (int x = 0; x < bw; ++x) {
            const int coverage = scratch->get_pixel(x, y);
            if (coverage <= 0) continue;
            const int canvas_x = bx + x;
            if (canvas_x < 0 || canvas_x >= canvas.width) continue;
            const int old = canvas.get_pixel(canvas_x, canvas_y);
            const int old_gray = old * 255 / max_level;
            const int diff = target_gray - old_gray;
            int value = old_gray + (diff * coverage + (diff >= 0 ? 127 : -127)) / 255;
            value = std::clamp(value, 0, 255);
            const int level = dither_quantize_gray(value, canvas.format, max_level, canvas_x,
                                                   canvas_y, dither, threshold);
            canvas.set_pixel(canvas_x, canvas_y, level);
        }
    }
    canvas_destroy(scratch);
    return true;
}

/* Decode one chapter image and draw it into `rect` (non-fatal on failure). */
void draw_chapter_image(repub_canvas& canvas, const repub_book& book, std::string_view href,
                        const Rect& rect, repub_dither_t dither, int32_t background_level) {
    if (rect.empty() || href.empty()) return;
    std::vector<uint8_t> bytes;
    if (book_read_resource(book, href, &bytes) != REPUB_OK || bytes.empty()) return;
    repub_image* image = nullptr;
    const size_t max_bytes = static_cast<size_t>(REPUB_MAX_IMAGE_BYTES);
    if (bytes.size() > max_bytes) return;
    if (image_decode(canvas.platform, bytes.data(), bytes.size(), rect.w, rect.h, &image) !=
        REPUB_OK) {
        return;
    }
    repub_image_view view{};
    view.struct_size = sizeof(view);
    view.dest = to_public(rect);
    view.zoom = 1.0f;
    view.pan_x = 0.0f;
    view.pan_y = 0.0f;
    view.fit = REPUB_IMAGE_FIT_CONTAIN;
    view.dither = dither;
    view.background_level = background_level;
    canvas_draw_image(&canvas, image, &view);
    image_free(image);
}

repub_dither_t image_dither_for(repub_canvas& canvas, const repub_render_options_t& options) {
    repub_dither_t dither = options.text_dither;
    if (canvas.format == REPUB_FORMAT_MONO1 || canvas.format == REPUB_FORMAT_GRAY2) {
        /* The default text dither (threshold) would destroy photo tones on
         * 1/2-bit panels; use ordered dithering for images instead. */
        if (dither == REPUB_DITHER_NONE || dither == REPUB_DITHER_THRESHOLD) {
            dither = REPUB_DITHER_BAYER4;
        }
    } else if (dither == REPUB_DITHER_THRESHOLD) {
        dither = REPUB_DITHER_NONE;
    }
    return dither;
}

void draw_outline(repub_canvas& canvas, const Rect& rect, int level) {
    if (rect.empty()) return;
    const Rect clip = rect.intersect(Rect{0, 0, canvas.width, canvas.height});
    if (clip.empty()) return;
    canvas.draw_hline(rect.x, rect.y, rect.w, level);
    canvas.draw_hline(rect.x, rect.bottom() - 1, rect.w, level);
    canvas.draw_vline(rect.x, rect.y, rect.h, level);
    canvas.draw_vline(rect.right() - 1, rect.y, rect.h, level);
}

uint32_t draw_flags_for(const FontManager& fonts, const repub_span_t& span, int face) {
    uint32_t flags = 0;
    if (span.style & REPUB_SPAN_UNDERLINE) flags |= DRAW_UNDERLINE;
    if (span.style & REPUB_SPAN_STRIKE) flags |= DRAW_STRIKE;
    /* The built-in bitmap face has no bold/italic variants: synthesize them. */
    if (!fonts.face_is_ttf(face)) {
        if (span.style & REPUB_SPAN_BOLD) flags |= DRAW_FAKE_BOLD;
        if (span.style & REPUB_SPAN_ITALIC) flags |= DRAW_FAKE_ITALIC;
    }
    return flags;
}

}  // namespace

void render_options_defaults(repub_render_options_t* options) {
    if (!options) return;
    options->struct_size = sizeof(repub_render_options_t);
    options->flags = REPUB_RENDER_UNDERLINE_LINKS | REPUB_RENDER_DRAW_IMAGES |
                     REPUB_RENDER_DRAW_NOTE_ICONS;
    options->text_dither = REPUB_DITHER_THRESHOLD; /* MONO1 default; gray ignores it */
    options->text_level = 0;
    options->background_level = -1;
    options->threshold = 128;
}

repub_err_t render_page(const repub_page& page, const repub_book& book, repub_canvas& canvas,
                        const FontManager& fonts, const repub_render_options_t& options) {
    if (canvas.width <= 0 || canvas.height <= 0) return REPUB_ERR_INVALID_ARG;
    const repub_chapter* chapter = page.chapter;

    if (options.background_level >= 0) {
        canvas.clear(options.background_level);
    }

    /* ---- Text spans --------------------------------------------------- */
    const bool dither_text =
        (canvas.format == REPUB_FORMAT_MONO1 || canvas.format == REPUB_FORMAT_GRAY2) &&
        options.text_dither != REPUB_DITHER_NONE;

    for (size_t i = 0; i < page.spans.size(); ++i) {
        const repub_span_t& span = page.spans[i];
        if (span.text_len == 0 || span.font_size_px == 0) continue;
        if (!chapter) continue;
        const std::string& chapter_text = chapter->flow.text;
        if (span.text_offset >= chapter_text.size()) continue;
        const size_t length =
            std::min<size_t>(span.text_len, chapter_text.size() - span.text_offset);
        if (length == 0) continue;

        int face = 0;
        if (i < page.span_extra.size()) {
            face = page.span_extra[i].face;
        } else {
            face = fonts.resolve_face(std::string_view(), span.style);
        }
        if (face < 0) continue;

        const int px = span.font_size_px;
        const std::string_view text(chapter_text.data() + span.text_offset, length);
        const bool vertical = (span.style & REPUB_SPAN_VERTICAL) != 0;
        const uint32_t flags = draw_flags_for(fonts, span, face);

        /* Public span semantics: horizontal x = left edge, y = baseline;
         * vertical x = top edge, y = right edge. */
        TextDraw run;
        run.face = face;
        run.px = px;
        run.vertical = vertical;
        run.text = text;
        run.flags = flags;
        run.color_level = options.text_level;
        /* Vertical runs produced by the layout engine use the natural
         * rectangle convention: x = column left edge, y = run top edge,
         * width = column pitch, height = run length.  (draw_text_vertical
         * takes the same left/top anchor.) */
        run.x = span.x;
        run.y = span.y;

        bool drawn = false;
        if (dither_text) {
            drawn = draw_text_dithered(canvas, fonts, run, options.text_dither,
                                       options.threshold);
        }
        if (!drawn) {
            if (!vertical) {
                fonts.draw_text(&canvas, run.face, px, run.x, run.y, text, run.color_level,
                                flags);
            } else {
                fonts.draw_text_vertical(&canvas, run.face, px, run.x, run.y, text,
                                         run.color_level, flags);
            }
        }
    }

    /* ---- Images ------------------------------------------------------- */
    const repub_dither_t image_dither = image_dither_for(canvas, options);
    if ((options.flags & REPUB_RENDER_DRAW_IMAGES) != 0 && chapter != nullptr) {
        for (const repub_page_image_t& image : page.images) {
            if (image.image_index < 0) continue;
            const size_t index = static_cast<size_t>(image.image_index);
            if (index >= chapter->flow.images.size()) continue;
            draw_chapter_image(canvas, book, chapter->flow.images[index].href,
                               from_public(image.rect), image_dither,
                               options.background_level);
        }
    }

    /* ---- Footnote icon markers --------------------------------------- */
    if ((options.flags & REPUB_RENDER_DRAW_NOTE_ICONS) != 0 && chapter != nullptr) {
        for (const repub_page_note_marker_t& note : page.notes) {
            if (note.icon < 0) continue;
            const size_t index = static_cast<size_t>(note.icon);
            if (index >= chapter->flow.images.size()) continue;
            draw_chapter_image(canvas, book, chapter->flow.images[index].href,
                               from_public(note.rect), image_dither,
                               options.background_level);
        }
    }

    /* ---- Link underlines ---------------------------------------------- */
    if ((options.flags & REPUB_RENDER_UNDERLINE_LINKS) != 0) {
        const int level = std::clamp(options.text_level, 0, canvas.max_level);
        for (const repub_page_link_t& link : page.links) {
            const Rect rect = from_public(link.rect);
            if (rect.empty()) continue;
            const int y = rect.bottom() - 1;
            canvas.draw_hline(rect.x, y, rect.w, level);
        }
    }

    /* ---- Debug boxes --------------------------------------------------- */
    if ((options.flags & REPUB_RENDER_DEBUG_BOXES) != 0) {
        const int level = std::clamp(options.text_level, 0, canvas.max_level);
        for (size_t i = 0; i < page.spans.size(); ++i) {
            const repub_span_t& span = page.spans[i];
            Rect rect;
            if ((span.style & REPUB_SPAN_VERTICAL) != 0) {
                /* Vertical runs: x/y are the column's left/top edge. */
                rect = Rect{span.x, span.y,
                            span.width > 0 ? span.width : span.font_size_px,
                            span.height > 0 ? span.height : span.font_size_px};
            } else {
                const int asc = fonts.ascender(
                    i < page.span_extra.size() ? page.span_extra[i].face : 0, span.font_size_px);
                rect = Rect{span.x, span.y - asc, span.width > 0 ? span.width : 0,
                            span.height > 0 ? span.height : asc};
            }
            draw_outline(canvas, rect, level);
        }
        for (const repub_page_image_t& image : page.images) {
            draw_outline(canvas, from_public(image.rect), level);
        }
        for (const repub_page_note_marker_t& note : page.notes) {
            draw_outline(canvas, from_public(note.rect), level);
        }
        for (const repub_page_link_t& link : page.links) {
            draw_outline(canvas, from_public(link.rect), level);
        }
    }

    return REPUB_OK;
}

}  // namespace repub
