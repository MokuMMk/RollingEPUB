/*
 * RollingEPUB layout engine: flow -> pages.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_LAYOUT_LAYOUT_H
#define REPUB_SRC_LAYOUT_LAYOUT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "content/css.h"
#include "content/flow.h"
#include "core/base.h"
#include "render/font.h"

namespace repub {

/* Per-span data not exposed by repub_span_t (font face chosen by the
 * font manager for the span's family/style). */
struct SpanExtra {
    int16_t face = 0;
    int16_t reserved = 0;
};

/* Public opaque types defined here so API casts are zero-cost. */
struct repub_page {
    repub_page_info_t info{};
    /* Chapter that owns Flow::images/notes/links referenced by this page.
     * Set by the layout builder; used by the page renderer to resolve image
     * and note-icon resources without an extra parameter. */
    const repub_chapter* chapter = nullptr;
    std::vector<repub_span_t> spans;
    std::vector<SpanExtra> span_extra;
    std::vector<repub_page_image_t> images;
    std::vector<repub_page_note_marker_t> notes;
    std::vector<repub_page_link_t> links;
    std::vector<std::string> link_hrefs; /* storage for links[].href */
};

struct repub_layout {
    const repub_chapter* chapter = nullptr;
    repub_layout_spec_t spec{};
    std::string hyphen_lang;
    std::vector<repub_page> pages;
    std::vector<uint32_t> page_start_offsets;
    uint32_t generation = 0;
    bool vertical = false;
};

/* Default-initialize a layout spec (page_rect left untouched). */
void layout_spec_defaults(repub_layout_spec_t* spec);

/* Build pages for a parsed chapter.  `fonts` must outlive the layout. */
repub_err_t layout_build(const repub_chapter& chapter,
                         const repub_layout_spec_t& spec,
                         const FontMetrics& fonts, repub_layout* out);

void layout_clear(repub_layout* layout);

/* Layout a synthetic flow (footnote popup, excerpt, search result). */
repub_err_t layout_build_flow(const repub_chapter& owner, const Flow& flow,
                              const repub_layout_spec_t& spec,
                              const FontMetrics& fonts, repub_layout* out);

}  // namespace repub

#endif  // REPUB_SRC_LAYOUT_LAYOUT_H
