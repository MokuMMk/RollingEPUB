/*
 * RollingEPUB chapter content model.
 *
 * The HTML/CSS parser turns one spine item into a flat normalized UTF-8 text,
 * a block table (paragraphs, headings, list items, images, table rows) and
 * inline style runs, plus footnote, link, image and anchor tables.  Layout
 * consumes this model; the public API exposes it for text extraction,
 * custom renderers and navigation.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_CONTENT_FLOW_H
#define REPUB_SRC_CONTENT_FLOW_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "content/css.h"
#include "core/base.h"

namespace repub {

struct repub_book;

/* Inline run style bits (match REPUB_SPAN_* values used by the public API). */
enum RunFlags : uint16_t {
    RUN_BOLD = 1u << 0,
    RUN_ITALIC = 1u << 1,
    RUN_UNDERLINE = 1u << 2,
    RUN_STRIKE = 1u << 3,
    RUN_SUPERSCRIPT = 1u << 4,
    RUN_SUBSCRIPT = 1u << 5
};

struct Run {
    uint32_t offset = 0; /* into Flow::text */
    uint32_t len = 0;
    uint16_t style = 0;        /* RunFlags */
    uint16_t scale_percent = 100;
    int16_t family_id = -1;
};

enum FlowBlockFlags : uint16_t {
    FBLOCK_BOLD = 1u << 0,
    FBLOCK_ITALIC = 1u << 1,
    FBLOCK_UNDERLINE = 1u << 2,
    FBLOCK_STRIKE = 1u << 3,
    FBLOCK_CENTER = 1u << 4,
    FBLOCK_RIGHT = 1u << 5,
    FBLOCK_JUSTIFY = 1u << 6,
    FBLOCK_NO_INDENT = 1u << 7,
    FBLOCK_LINK = 1u << 8,
    FBLOCK_CODE = 1u << 9
};

struct FlowBlock {
    uint32_t offset = 0;
    uint32_t len = 0;
    uint8_t type = REPUB_BLOCK_PARAGRAPH; /* enum repub_block_type */
    uint8_t heading_level = 0;
    uint16_t flags = 0;
    int16_t margin_before_px = 0;
    int16_t margin_after_px = 0;
    int16_t indent_px = 0;      /* at 100% body size */
    int16_t font_scale_percent = 100;
    int16_t family_id = -1;
    int32_t image_index = -1;
    int32_t link_index = -1;
    uint16_t run_first = 0;
    uint16_t run_count = 0;
    bool preserve_whitespace = false;
};

struct FlowNote {
    uint32_t marker_offset = 0;
    std::string text;
    std::string href;
    int32_t marker_image = -1;
};

struct FlowImage {
    std::string href;     /* normalized package-relative path */
    std::string raw_src;  /* verbatim attribute value */
    int32_t width = 0;    /* probed dimensions, 0 unknown */
    int32_t height = 0;
};

struct FlowLink {
    std::string href;
    uint8_t kind = 0;     /* 0 internal, 1 external, 2 footnote */
    int32_t note_index = -1;
};

struct FlowAnchor {
    std::string id;
    uint32_t offset = 0;
};

struct Flow {
    std::string text;             /* normalized UTF-8 chapter text */
    std::vector<FlowBlock> blocks;
    std::vector<Run> runs;
    std::vector<FlowNote> notes;
    std::vector<FlowImage> images;
    std::vector<FlowLink> links;
    std::vector<FlowAnchor> anchors;
    std::vector<std::string> families; /* font-family intern table */
    std::vector<uint32_t> block_offsets; /* sorted block start offsets (search/anchoring) */
};

struct ParseOptions {
    bool css_enabled = true;
    bool footnotes_enabled = true;
    bool images_enabled = true;
    bool tables_enabled = true;
    bool drop_caps_enabled = true;
    bool ruby_enabled = true;
    int max_blocks = 0;   /* 0 = REPUB_MAX_BLOCKS */
    size_t max_text_bytes = 0;
};

/* Public opaque type defined here so the API can hand out direct pointers. */
struct repub_chapter {
    const repub_book* book = nullptr;
    uint32_t spine_index = 0;
    std::string title;
    Flow flow;

    /* Public API mirrors, rebuilt by chapter_finalize(). */
    std::vector<repub_block_info_t> public_blocks;
    std::vector<repub_note_info_t> public_notes;
    std::vector<repub_chapter_image_info_t> public_images;
    std::vector<repub_chapter_link_info_t> public_links;
};

/* Parse chapter HTML into `out`.  `base_dir` is the package-relative folder
 * of the chapter file, used to resolve relative image/CSS hrefs.
 * Returns REPUB_ERR_FORMAT on unrecoverable markup problems; most malformed
 * markup is recovered from silently. */
repub_err_t parse_chapter(const repub_book& book, size_t spine_index,
                          std::string_view html, std::string_view base_dir,
                          const ParseOptions& options, repub_chapter* out);

/* Rebuild public mirrors after parsing. */
void chapter_finalize(repub_chapter* chapter);
void chapter_clear(repub_chapter* chapter);

}  // namespace repub

#endif  // REPUB_SRC_CONTENT_FLOW_H
