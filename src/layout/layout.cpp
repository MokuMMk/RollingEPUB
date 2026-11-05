/*
 * RollingEPUB layout engine: Flow -> paginated pages.
 *
 * SPDX-License-Identifier: MIT
 *
 * Algorithm provenance
 * --------------------
 * Horizontal pagination, CJK per-codepoint line breaking, kinsoku
 * (line-start/line-end prohibition) rules, hanging punctuation, drop-cap
 * reserve and the vertical column model are ported/adapted from Read Pico
 * `main/book/book_layout.c` (Apache-2.0).  The page/anchor model follows
 * CrossMux `Epub/Section.cpp` + `Epub/Page.h` (MIT).  See NOTICE.
 *
 * Everything here is deterministic: identical Flow + spec + font metrics
 * always produce identical pages.  No time, random or environment input.
 */

#include "layout/layout.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "container/book.h"
#include "layout/hyphen.h"
#include "rolling_epub/repub_config.h"

namespace repub {
namespace {

/* ----------------------------------------------------------------------- */
/* Local UTF-8 helpers (self-contained: layout must not depend on the      */
/* container module's text_util at link time).                             */
/* ----------------------------------------------------------------------- */

size_t utf8_seq(const char* p, size_t remaining) {
    if (!remaining) return 0;
    const unsigned char* u = reinterpret_cast<const unsigned char*>(p);
    if (u[0] > 0 && u[0] < 0x80) return 1;
    size_t n = 0;
    if (u[0] >= 0xC2 && u[0] <= 0xDF) {
        n = 2;
    } else if (u[0] >= 0xE0 && u[0] <= 0xEF) {
        n = 3;
    } else if (u[0] >= 0xF0 && u[0] <= 0xF4) {
        n = 4;
    } else {
        return 1; /* invalid lead byte: consume one, treated as U+FFFD */
    }
    if (n > remaining) return 0;
    for (size_t i = 1; i < n; ++i) {
        if ((u[i] & 0xC0) != 0x80) return 0;
    }
    if ((u[0] == 0xE0 && u[1] < 0xA0) || (u[0] == 0xED && u[1] >= 0xA0) ||
        (u[0] == 0xF0 && u[1] < 0x90) || (u[0] == 0xF4 && u[1] >= 0x90)) {
        return 0;
    }
    return n;
}

uint32_t utf8_cp(const char* p, size_t n) {
    const unsigned char* u = reinterpret_cast<const unsigned char*>(p);
    if (n <= 1) return u[0];
    uint32_t cp = static_cast<uint32_t>(u[0] & (0x7F >> n));
    for (size_t i = 1; i < n; ++i) cp = (cp << 6) | static_cast<uint32_t>(u[i] & 0x3F);
    return cp;
}

/* Decode one codepoint; returns byte length (>= 1 for non-empty input). */
size_t decode_cp(const char* data, size_t size, uint32_t* cp) {
    size_t n = utf8_seq(data, size);
    if (!n) {
        n = 1;
        *cp = 0xFFFDu;
        return n;
    }
    *cp = utf8_cp(data, n);
    return n;
}

bool is_space_cp(uint32_t cp) {
    switch (cp) {
        case ' ':
        case '\t':
        case 0x00A0: /* no-break space */
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202F:
        case 0x205F:
        case 0x3000:
            return true;
        default:
            return cp >= 0x2000 && cp <= 0x200B;
    }
}

bool is_nbsp(uint32_t cp) { return cp == 0x00A0 || cp == 0x202F; }

bool is_combining_cp(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F);
}

bool is_cjk_cp(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x11FF) ||  /* Hangul jamo */
           (cp >= 0x2E80 && cp <= 0x2FFF) ||  /* radicals, CJK punctuation */
           (cp >= 0x3000 && cp <= 0x303F) ||  /* CJK symbols/punctuation */
           (cp >= 0x3040 && cp <= 0x30FF) ||  /* kana */
           (cp >= 0x3100 && cp <= 0x31FF) ||  /* bopomofo, hangul compat */
           (cp >= 0x3400 && cp <= 0x4DBF) ||  /* ext A */
           (cp >= 0x4E00 && cp <= 0x9FFF) ||  /* unified */
           (cp >= 0xA000 && cp <= 0xA4CF) ||  /* Yi */
           (cp >= 0xAC00 && cp <= 0xD7AF) ||  /* Hangul syllables */
           (cp >= 0xF900 && cp <= 0xFAFF) ||  /* compat ideographs */
           (cp >= 0xFE30 && cp <= 0xFE4F) ||  /* compat punctuation */
           (cp >= 0xFF00 && cp <= 0xFFEF) ||  /* fullwidth forms */
           (cp >= 0x1B000 && cp <= 0x1B16F) || (cp >= 0x20000 && cp <= 0x2FA1F);
}

bool is_latin_word_cp(uint32_t cp) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
           (cp >= '0' && cp <= '9') || (cp >= 0x00C0 && cp <= 0x024F) ||
           (cp >= 0x0400 && cp <= 0x04FF) || (cp >= 0x1E00 && cp <= 0x1EFF);
}

bool is_word_cp(uint32_t cp) { return is_latin_word_cp(cp) || is_combining_cp(cp); }

bool is_apostrophe_cp(uint32_t cp) {
    return cp == '\'' || cp == 0x2018 || cp == 0x2019;
}

bool is_explicit_hyphen_cp(uint32_t cp) {
    switch (cp) {
        case '-':
        case 0x058A:
        case 0x2010:
        case 0x2011:
        case 0x2012:
        case 0x2013:
        case 0x2014:
        case 0x2015:
        case 0x2043:
        case 0x2E17:
        case 0xFE58:
        case 0xFE63:
        case 0xFF0D:
            return true;
        default:
            return false;
    }
}

bool is_soft_hyphen_cp(uint32_t cp) { return cp == 0x00AD; }

/* CJK kinsoku: closing punctuation may not start a line. */
bool prohibited_line_start(uint32_t cp) {
    switch (cp) {
        case 0x0021: /* ! */
        case 0x0025: /* % */
        case 0x0029: /* ) */
        case 0x002C: /* , */
        case 0x002E: /* . */
        case 0x003A: /* : */
        case 0x003B: /* ; */
        case 0x003F: /* ? */
        case 0x005D: /* ] */
        case 0x007D: /* } */
        case 0x00BB: /* » */
        case 0x2019: /* ’ */
        case 0x201D: /* ” */
        case 0x2026: /* … */
        case 0x203A: /* › */
        case 0x3001: /* 、 */
        case 0x3002: /* 。 */
        case 0x3005: /* 々 */
        case 0x3009: /* 〉 */
        case 0x300B: /* 》 */
        case 0x300D: /* 」 */
        case 0x300F: /* 』 */
        case 0x3011: /* 】 */
        case 0x3015: /* 〕 */
        case 0x3017: /* 〗 */
        case 0x3019: /* 〙 */
        case 0x301B: /* 〛 */
        case 0x301E:
        case 0x301F:
        case 0x303C:
        case 0x303D:
        case 0x30FB: /* ・ */
        case 0xFE50:
        case 0xFE52:
        case 0xFE54:
        case 0xFE56:
        case 0xFE57:
        case 0xFF01: /* ！ */
        case 0xFF05: /* ％ */
        case 0xFF09: /* ） */
        case 0xFF0C: /* ， */
        case 0xFF0E: /* ． */
        case 0xFF1A: /* ： */
        case 0xFF1B: /* ； */
        case 0xFF1F: /* ？ */
        case 0xFF3D: /* ］ */
        case 0xFF5D: /* ｝ */
        case 0xFF60:
        case 0xFF64:
            return true;
        default:
            return false;
    }
}

/* CJK kinsoku: opening punctuation may not end a line. */
bool prohibited_line_end(uint32_t cp) {
    switch (cp) {
        case 0x0028: /* ( */
        case 0x005B: /* [ */
        case 0x007B: /* { */
        case 0x00AB: /* « */
        case 0x2018: /* ‘ */
        case 0x201C: /* “ */
        case 0x2039: /* ‹ */
        case 0x3008: /* 〈 */
        case 0x300A: /* 《 */
        case 0x300C: /* 「 */
        case 0x300E: /* 『 */
        case 0x3010: /* 【 */
        case 0x3014: /* 〔 */
        case 0x3016: /* 〖 */
        case 0x3018: /* 〘 */
        case 0x301A: /* 〚 */
        case 0x301D:
        case 0xFE59:
        case 0xFE5B:
        case 0xFE5D:
        case 0xFF08: /* （ */
        case 0xFF3B: /* ［ */
        case 0xFF5B: /* ｛ */
        case 0xFF62:
            return true;
        default:
            return false;
    }
}

constexpr uint8_t CELL_SPACE = 1u << 0;
constexpr uint8_t CELL_CJK = 1u << 1;
constexpr uint8_t CELL_WORD = 1u << 2;
constexpr uint8_t CELL_HYPHEN = 1u << 3;
constexpr uint8_t CELL_ICON = 1u << 4;
constexpr uint8_t CELL_NOBREAK = 1u << 5;
constexpr uint8_t CELL_SOFT_HYPHEN = 1u << 6;

struct Cell {
    uint32_t offset = 0;
    uint16_t len = 0;
    uint16_t px = 0;
    int16_t face = 0;
    int16_t asc = 0;
    int16_t desc = 0;
    int16_t delta = 0; /* baseline offset: negative superscript, positive subscript */
    int32_t advance = 0;
    int16_t note = -1; /* footnote note index when this cell is a note marker */
    int16_t icon = -1; /* image index for icon markers */
    uint8_t flags = 0;
    uint8_t style = 0; /* repub_span_flags */
    uint32_t cp = 0;
};

/* One laid-out line (horizontal) or column (vertical). */
struct LineBuild {
    std::vector<Cell> cells;
    int width = 0;
    int line_height = 0;
    int ascent = 0;
    int indent = 0;
    size_t end_offset = 0;
    size_t next = 0;
    bool hard_end = false;
    bool empty = true;
};

struct BlockStyle {
    int px = 16;
    int face = 0;
    uint8_t span_style = 0;
    bool heading = false;
    bool centered = false;
    bool right = false;
    bool justify = false;
    bool no_indent = false;
    bool preserve = false;
    int margin_before = 0;
    int margin_after = 0;
    int first_indent = 0;
    int inset_left = 0;  /* block-level left/right content inset (blockquote) */
    int inset_right = 0;
    int16_t family_id = -1;
    int32_t link_index = -1;
    bool is_link = false;
    int drop_cap_lines = 0;
};

struct CharStyle {
    int face = 0;
    int px = 16;
    uint16_t note_px = 16;
    uint8_t span_style = 0;
    int16_t asc = 0;
    int16_t desc = 0;
    int16_t delta = 0;
};

struct PendingLink {
    std::string href;
    repub_rect_t rect{};
};

class Builder {
   public:
    Builder(const Flow& flow, const repub_layout_spec_t& spec, const FontMetrics& fonts,
            const repub_chapter* chapter, repub_layout& out)
        : flow_(flow), spec_(spec), fonts_(fonts), chapter_(chapter), out_(out) {}

    bool run();

    /* Configuration applied by the driver before run(). */
    void configure(bool hyphenate, std::string lang, int body_px, int line_percent, int gap_percent,
                   bool vertical) {
        hyphenate_ = hyphenate;
        hyphen_lang_ = std::move(lang);
        body_px_ = body_px > 0 ? body_px : 16;
        line_percent_ = line_percent;
        gap_percent_ = gap_percent;
        vertical_ = vertical;
    }
    void set_max_pages(size_t max_pages) {
        max_pages_ = max_pages < 1 ? 1 : max_pages;
        if (max_pages_ > REPUB_MAX_PAGES) max_pages_ = REPUB_MAX_PAGES;
    }
    bool vertical() const { return vertical_; }
    void prepare() {
        configure_geometry();
        configure_notes();
    }

   private:
    void configure_geometry();
    void configure_notes();

    bool new_page(uint32_t start_offset);
    void finish_page();
    bool ensure_space(int leading, int height, uint32_t offset);
    repub_page& current_page() { return out_.pages[page_index_]; }
    bool page_empty() const;
    void cover(uint32_t end_offset);
    int default_gap() const { return body_px_ / 2; }

    BlockStyle block_style(const FlowBlock& block) const;
    int resolve_face(int16_t family_id, uint8_t span_style) const;
    int scaled(int value, int px) const;
    CharStyle char_style(const BlockStyle& style, size_t offset, size_t limit);
    Cell make_cell(size_t offset, size_t n, uint32_t cp, const CharStyle& cs);
    void attach_note(Cell& cell) const;

    LineBuild build_line(size_t start, size_t limit, const BlockStyle& style, int available,
                         bool first_line);
    bool break_opportunity(const Cell& prev, const Cell& cur) const;
    int adjust_break(const LineBuild& line, const Cell& overflow, int candidate) const;
    int try_hyphen_break(const LineBuild& line, size_t limit, int available) const;
    int measured_width(const std::vector<Cell>& cells, int count) const;
    void emit_line(const LineBuild& line, const BlockStyle& style, bool is_last);

    void emit_block(const FlowBlock& block);
    bool layout_text_block(const FlowBlock& block);
    bool layout_image_block(const FlowBlock& block);
    bool layout_hr_block(const FlowBlock& block);

    bool layout_vertical();
    bool layout_vertical_block(const FlowBlock& block);

    const Flow& flow_;
    const repub_layout_spec_t& spec_;
    const FontMetrics& fonts_;
    const repub_chapter* chapter_;
    repub_layout& out_;

    int content_x_ = 0;
    int content_y_ = 0;
    int content_w_ = 0;
    int content_h_ = 0;
    int bottom_ = 0;
    int page_top_ = 0;
    int body_px_ = 16;
    int line_percent_ = 160;
    int gap_percent_ = 60;
    int first_page_extra_ = 0;
    bool vertical_ = false;
    bool hyphenate_ = false;
    std::string hyphen_lang_;

    size_t page_index_ = 0;
    int y_ = 0;
    int line_index_ = 0;
    uint32_t covered_end_ = 0;
    size_t max_pages_ = REPUB_MAX_PAGES;
    bool stopped_ = false;

    std::vector<PendingLink> pending_links_;
    std::vector<std::pair<uint32_t, int>> note_offsets_;

    size_t run_cursor_ = 0;
    size_t run_end_ = 0;
    size_t block_limit_ = 0;
    size_t current_block_index_ = 0;

    /* A short centred paragraph right after an image is treated as its
     * caption: keep the image and caption together instead of leaving an
     * orphan caption at the top of the next page. */
    bool caption_follows() const {
        const size_t next = current_block_index_ + 1;
        if (next >= flow_.blocks.size()) return false;
        const FlowBlock& b = flow_.blocks[next];
        return b.type == REPUB_BLOCK_PARAGRAPH && b.len > 0 && b.len < 160 &&
               (b.flags & (FBLOCK_CENTER | FBLOCK_RIGHT)) != 0;
    }

    /* Vertical writing continuation state (persists across blocks). */
    int v_column_ = 0;
    int v_column_top_ = 0;
    /* x of the next column's left edge, advancing right-to-left.  Kept across
     * blocks so a large heading column is followed by body columns without
     * overlap (blocks may have different line pitch). */
    int v_cursor_x_ = -1;

    void reset_vertical_columns() {
        v_column_ = 0;
        v_cursor_x_ = content_x_ + content_w_;
        v_column_top_ = page_index_ == 0 ? page_top_ + first_page_extra_ : page_top_;
    }
};

int Builder::scaled(int value, int px) const {
    if (!value) return 0;
    int64_t v = static_cast<int64_t>(value) * px;
    v /= body_px_ > 0 ? body_px_ : 1;
    return static_cast<int>(v);
}

void Builder::configure_geometry() {
    const repub_rect_t& r = spec_.page_rect;
    int left = spec_.margin_left_px > 0 ? spec_.margin_left_px : 0;
    int right = spec_.margin_right_px > 0 ? spec_.margin_right_px : 0;
    int top = spec_.margin_top_px > 0 ? spec_.margin_top_px : 0;
    int bottom = spec_.margin_bottom_px > 0 ? spec_.margin_bottom_px : 0;
    content_x_ = r.x + left;
    content_y_ = r.y + top;
    content_w_ = std::max(1, r.w - left - right);
    content_h_ = std::max(1, r.h - top - bottom);
    bottom_ = content_y_ + content_h_;
    page_top_ = content_y_;
    first_page_extra_ = spec_.chapter_heading_height_px > 0 ? spec_.chapter_heading_height_px : 0;
    if (first_page_extra_ > content_h_ - 1) first_page_extra_ = 0;
}

void Builder::configure_notes() {
    note_offsets_.clear();
    note_offsets_.reserve(flow_.notes.size());
    for (size_t i = 0; i < flow_.notes.size(); ++i) {
        note_offsets_.emplace_back(flow_.notes[i].marker_offset, static_cast<int>(i));
    }
    std::stable_sort(note_offsets_.begin(), note_offsets_.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
}

int Builder::resolve_face(int16_t family_id, uint8_t span_style) const {
    uint32_t bits = 0;
    if (span_style & REPUB_SPAN_BOLD) bits |= REPUB_FONT_BOLD;
    if (span_style & REPUB_SPAN_ITALIC) bits |= REPUB_FONT_ITALIC;
    std::string_view family;
    if (family_id >= 0 && static_cast<size_t>(family_id) < flow_.families.size()) {
        family = flow_.families[static_cast<size_t>(family_id)];
    }
    int face = fonts_.resolve_face(family, static_cast<uint16_t>(bits));
    if (face < 0) face = fonts_.resolve_face(std::string_view(), 0);
    return face;
}

BlockStyle Builder::block_style(const FlowBlock& block) const {
    BlockStyle st;
    int scale = block.font_scale_percent;
    if (scale <= 0) scale = 100;
    if (scale > 800) scale = 800;
    st.px = (body_px_ * scale + 50) / 100;
    st.heading = block.type == REPUB_BLOCK_HEADING;
    if (st.heading && scale == 100) {
        /* Parser normally provides the heading scale; give a sane fallback for
         * synthetic flows (h1..h6). */
        static const int kHeadingScale[6] = {200, 170, 145, 125, 112, 100};
        int level = block.heading_level;
        if (level < 1) level = 1;
        if (level > 6) level = 6;
        st.px = (body_px_ * kHeadingScale[level - 1] + 50) / 100;
    }
    if (st.px < 1) st.px = 1;
    if (block.flags & FBLOCK_BOLD) st.span_style |= REPUB_SPAN_BOLD;
    if (block.flags & FBLOCK_ITALIC) st.span_style |= REPUB_SPAN_ITALIC;
    if (block.flags & FBLOCK_UNDERLINE) st.span_style |= REPUB_SPAN_UNDERLINE;
    if (block.flags & FBLOCK_STRIKE) st.span_style |= REPUB_SPAN_STRIKE;
    st.centered = (block.flags & FBLOCK_CENTER) != 0;
    st.right = (block.flags & FBLOCK_RIGHT) != 0;
    st.justify = (block.flags & FBLOCK_JUSTIFY) != 0 ||
                 (spec_.justify && !st.centered && !st.right && !st.heading);
    st.no_indent = (block.flags & FBLOCK_NO_INDENT) != 0;
    st.preserve = block.preserve_whitespace || (block.flags & FBLOCK_CODE) != 0;
    st.family_id = block.family_id;
    st.margin_before = scaled(block.margin_before_px, st.px);
    st.margin_after = scaled(block.margin_after_px, st.px);
    if (st.heading && st.margin_before == 0) st.margin_before = st.px / 2;
    /* Blockquote insets: FlowBlock currently has no CSS margin-left/right
     * fields, so use a modest 2em default on both sides (see FlowBlock
     * contract note in the report). */
    if (block.type == REPUB_BLOCK_BLOCKQUOTE) {
        st.inset_left = 2 * st.px;
        st.inset_right = 2 * st.px;
        if (st.inset_left + st.inset_right >= content_w_) {
            st.inset_left = st.inset_right = content_w_ / 4;
        }
    }
    st.link_index = block.link_index;
    st.is_link = (block.flags & FBLOCK_LINK) != 0 && block.link_index >= 0 &&
                 static_cast<size_t>(block.link_index) < flow_.links.size();
    if (st.is_link) st.span_style |= REPUB_SPAN_LINK;
#if REPUB_ENABLE_DROP_CAPS
    st.drop_cap_lines = static_cast<int>(block.drop_cap_lines);
#else
    st.drop_cap_lines = 0;
#endif
    if (st.drop_cap_lines > 3) st.drop_cap_lines = 3;
    if (vertical_) st.drop_cap_lines = 0;
    if (!st.heading && !st.centered && !st.right) {
        int em = spec_.first_line_indent_em;
        if (em < 0) em = 0;
        if (em > 3) em = 3;
        int indent = 0;
        if (block.indent_px > 0) {
            /* Explicit CSS text-indent (already relative to the body size). */
            indent = scaled(block.indent_px, st.px);
        } else if (!st.no_indent) {
            indent = em * st.px;
        }
        if (indent < 0) indent = 0;
        st.first_indent = indent;
    }
    if (st.first_indent >= content_w_) st.first_indent = content_w_ > 1 ? content_w_ - 1 : 0;
    return st;
}

CharStyle Builder::char_style(const BlockStyle& style, size_t offset, size_t limit) {
    (void)limit;
    CharStyle cs;
    cs.face = style.face;
    cs.px = style.px;
    cs.span_style = style.span_style;
    int16_t family_id = style.family_id;
    while (run_cursor_ < run_end_ && offset >= flow_.runs[run_cursor_].offset + flow_.runs[run_cursor_].len) {
        ++run_cursor_;
    }
    if (run_cursor_ < run_end_) {
        const Run& run = flow_.runs[run_cursor_];
        if (offset >= run.offset && offset < run.offset + run.len) {
            uint16_t rs = run.style;
            if (rs & RUN_BOLD) cs.span_style |= REPUB_SPAN_BOLD;
            if (rs & RUN_ITALIC) cs.span_style |= REPUB_SPAN_ITALIC;
            if (rs & RUN_UNDERLINE) cs.span_style |= REPUB_SPAN_UNDERLINE;
            if (rs & RUN_STRIKE) cs.span_style |= REPUB_SPAN_STRIKE;
            if (rs & RUN_LINK) cs.span_style |= REPUB_SPAN_LINK;
            bool sup = (rs & RUN_SUPERSCRIPT) != 0;
            bool sub = (rs & RUN_SUBSCRIPT) != 0;
            if (sup) cs.span_style |= REPUB_SPAN_SUPERSCRIPT;
            if (sub) cs.span_style |= REPUB_SPAN_SUBSCRIPT;
            /* Run scale is an absolute percentage of the body size (the same
             * space as FlowBlock::font_scale_percent); 100 inherits the block
             * size.  Applying it on top of the block scale would double-scale
             * headings (parser stores the inherited size on the run too). */
            int run_scale = run.scale_percent > 0 ? run.scale_percent : 100;
            if (run_scale > 800) run_scale = 800;
            int px = (run_scale == 100) ? style.px : (body_px_ * run_scale + 50) / 100;
            if (px < 1) px = 1;
            if (sup || sub) {
                int delta = (px * (sup ? 30 : 25) + 50) / 100;
                cs.delta = static_cast<int16_t>(sup ? -delta : delta);
                px = (px * 6 + 5) / 10;
                if (px < 1) px = 1;
            }
            cs.px = px;
            if (run.family_id >= 0) family_id = run.family_id;
        }
    }
    cs.face = resolve_face(family_id, cs.span_style);
    cs.asc = static_cast<int16_t>(fonts_.ascender(cs.face, cs.px));
    cs.desc = static_cast<int16_t>(fonts_.descender(cs.face, cs.px));
    cs.note_px = static_cast<uint16_t>(cs.px > 0xFFFF ? 0xFFFF : cs.px);
    return cs;
}

void Builder::attach_note(Cell& cell) const {
    auto it = std::lower_bound(note_offsets_.begin(), note_offsets_.end(), cell.offset,
                               [](const std::pair<uint32_t, int>& a, uint32_t value) {
                                   return a.first < value;
                               });
    if (it == note_offsets_.end() || it->first != cell.offset) return;
    const FlowNote& note = flow_.notes[static_cast<size_t>(it->second)];
    cell.note = static_cast<int16_t>(it->second);
    if (note.marker_image >= 0) {
        cell.icon = static_cast<int16_t>(note.marker_image);
        cell.flags |= CELL_ICON;
        cell.advance = std::max(1, (static_cast<int>(cell.px) * 9 + 5) / 10);
    }
}

Cell Builder::make_cell(size_t offset, size_t n, uint32_t cp, const CharStyle& cs) {
    Cell c;
    c.offset = static_cast<uint32_t>(offset);
    c.len = static_cast<uint16_t>(n);
    c.cp = cp;
    c.face = static_cast<int16_t>(cs.face);
    c.px = static_cast<uint16_t>(cs.px > 0xFFFF ? 0xFFFF : cs.px);
    c.style = cs.span_style;
    c.asc = cs.asc;
    c.desc = cs.desc;
    c.delta = cs.delta;
    c.advance = fonts_.glyph_advance(cs.face, cs.px, cp);
    if (c.advance <= 0) {
        if (is_space_cp(cp))
            c.advance = std::max(1, cs.px / 2);
        else if (is_cjk_cp(cp))
            c.advance = std::max(1, cs.px);
        else
            c.advance = std::max(1, cs.px / 2);
    }
    if (is_cjk_cp(cp)) c.flags |= CELL_CJK;
    if (is_space_cp(cp)) {
        c.flags |= CELL_SPACE;
        if (is_nbsp(cp)) c.flags |= CELL_NOBREAK;
    } else if (is_word_cp(cp)) {
        c.flags |= CELL_WORD;
    } else if (is_apostrophe_cp(cp)) {
        c.flags |= CELL_NOBREAK;
    } else if (is_explicit_hyphen_cp(cp)) {
        c.flags |= CELL_HYPHEN | CELL_WORD;
    } else if (is_soft_hyphen_cp(cp)) {
        c.flags |= CELL_HYPHEN | CELL_SOFT_HYPHEN;
        c.advance = 0;
    }
    attach_note(c);
    return c;
}

bool Builder::break_opportunity(const Cell& prev, const Cell& cur) const {
    if (prev.flags & CELL_NOBREAK) return false;
    if (cur.flags & CELL_SPACE) return false;
    if (prev.flags & CELL_SOFT_HYPHEN) return false;
    if (prev.flags & CELL_HYPHEN) return true;
    if (prev.flags & CELL_SPACE) return true;
    if (prev.flags & CELL_CJK) return true;
    if ((cur.flags & CELL_WORD) && !(prev.flags & CELL_WORD)) return true;
    if ((cur.flags & CELL_CJK) && !(prev.flags & CELL_CJK)) return true;
    return false;
}

int Builder::measured_width(const std::vector<Cell>& cells, int count) const {
    int64_t w = 0;
    for (int i = 0; i < count; ++i) {
        w += cells[static_cast<size_t>(i)].advance;
        if (i) w += spec_.tracking_px;
    }
    if (w < 0) w = 0;
    return static_cast<int>(w);
}

int Builder::adjust_break(const LineBuild& line, const Cell& overflow, int candidate) const {
    int bp = candidate;
    int guard = 0;
    while (bp > 0 && guard++ < 64) {
        const Cell& next = (bp < static_cast<int>(line.cells.size()))
                               ? line.cells[static_cast<size_t>(bp)]
                               : overflow;
        if (prohibited_line_start(next.cp)) {
            --bp;
            continue;
        }
        const Cell& last = line.cells[static_cast<size_t>(bp - 1)];
        if (!(last.flags & CELL_SPACE) && prohibited_line_end(last.cp)) {
            --bp;
            continue;
        }
        break;
    }
    return bp;
}

int Builder::try_hyphen_break(const LineBuild& line, size_t limit, int available) const {
    if (!hyphenate_ || line.cells.empty()) return -1;
    int ws = static_cast<int>(line.cells.size());
    while (ws > 0 && (line.cells[static_cast<size_t>(ws - 1)].flags & CELL_WORD)) --ws;
    if (ws >= static_cast<int>(line.cells.size())) return -1;
    uint32_t word_start = line.cells[static_cast<size_t>(ws)].offset;
    size_t word_end = word_start;
    uint32_t prev_cp = 0;
    while (word_end < limit) {
        uint32_t cp = 0;
        size_t n = decode_cp(flow_.text.data() + word_end, limit - word_end, &cp);
        if (!n) break;
        bool member = is_latin_word_cp(cp) || is_combining_cp(cp) ||
                      (is_apostrophe_cp(cp) && is_latin_word_cp(prev_cp)) ||
                      (is_explicit_hyphen_cp(cp) && is_latin_word_cp(prev_cp));
        if (!member) break;
        word_end += n;
        prev_cp = cp;
    }
    if (word_end <= word_start) return -1;
    std::string_view word(flow_.text.data() + word_start, word_end - word_start);
    HyphenBreak breaks[32];
    int count = hyphenate_word(word, hyphen_lang_, breaks, 32);
    if (count <= 0) return -1;
    int best = -1;
    for (int i = 0; i < count; ++i) {
        size_t target = word_start + breaks[i].byte_offset;
        for (int j = ws + 1; j < static_cast<int>(line.cells.size()); ++j) {
            if (line.cells[static_cast<size_t>(j)].offset == target) {
                if (measured_width(line.cells, j) <= available) best = j;
                break;
            }
        }
    }
    return best;
}

LineBuild Builder::build_line(size_t start, size_t limit, const BlockStyle& style, int available,
                               bool first_line) {
    LineBuild line;
    line.indent = first_line ? style.first_indent : 0;
    if (line.indent >= available) line.indent = available > 1 ? available - 1 : 0;
    const int inner = available - line.indent;
    line.cells.reserve(64);
    size_t pos = start;
    int width = 0;
    int candidate = -1;
    bool hard_end = false;

    while (pos < limit) {
        uint32_t cp = 0;
        size_t n = decode_cp(flow_.text.data() + pos, limit - pos, &cp);
        if (!n) break;
        if (cp == '\r') {
            pos += n;
            continue;
        }
        if (cp == '\n') {
            hard_end = true;
            break;
        }
        if (!style.preserve && line.cells.empty() && is_space_cp(cp)) {
            pos += n;
            continue;
        }
        CharStyle cs = char_style(style, pos, limit);
        Cell cell = make_cell(pos, n, cp, cs);

        if (!line.cells.empty() && is_combining_cp(cp)) {
            Cell& prev = line.cells.back();
            if (prev.len <= 0xFFF0) prev.len = static_cast<uint16_t>(prev.len + n);
            pos += n;
            continue;
        }

        const int track = line.cells.empty() ? 0 : spec_.tracking_px;
        const int cand_width = width + cell.advance + track;
        const bool opportunity = !line.cells.empty() && break_opportunity(line.cells.back(), cell);
        if (cand_width > inner && !line.cells.empty()) {
            const int old_size = static_cast<int>(line.cells.size());
            int bp = opportunity ? old_size : candidate;
            if (bp > 0) bp = adjust_break(line, cell, bp);
            if (bp <= 0) {
                int hy = try_hyphen_break(line, limit, inner);
                bp = hy > 0 ? hy : old_size;
            }
            if (bp <= 0) bp = old_size;
            if (bp > old_size) bp = old_size;
            /* Resume after the break: at the first dropped cell (hyphenated /
             * emergency split) or at the current character when the break is
             * exactly at the overflow point.  Never skip accumulated cells. */
            const size_t resume =
                (bp < old_size) ? static_cast<size_t>(line.cells[static_cast<size_t>(bp)].offset)
                                : pos;
            line.cells.resize(static_cast<size_t>(bp));
            line.end_offset = line.cells.empty()
                                  ? pos
                                  : static_cast<size_t>(line.cells.back().offset) + line.cells.back().len;
            line.next = resume;
            line.hard_end = false;
            break;
        }
        if (opportunity) candidate = static_cast<int>(line.cells.size());
        width = cand_width;
        line.cells.push_back(cell);
        pos += n;
    }

    if (line.cells.empty()) {
        line.empty = true;
        if (pos >= limit) {
            line.hard_end = true;
            line.end_offset = limit;
            line.next = limit;
        } else if (hard_end) {
            line.hard_end = true;
            line.end_offset = pos;
            line.next = pos + 1;
        } else {
            line.end_offset = pos;
            line.next = pos + 1;
        }
        return line;
    }

    if (pos >= limit) {
        line.hard_end = true;
        line.end_offset = limit;
        line.next = limit;
    } else if (hard_end) {
        line.hard_end = true;
        line.end_offset = pos;
        size_t next = pos + 1;
        if (flow_.text[pos] == '\r' && next < limit && flow_.text[next] == '\n') ++next;
        line.next = next;
    }
    if (!line.hard_end) {
        line.end_offset = static_cast<size_t>(line.cells.back().offset) + line.cells.back().len;
        line.next = line.end_offset;
    }

    /* Trailing spaces are not part of a line's ink and must not affect
     * alignment/justification or the right margin.  Drop them from the span
     * list; the next line starts at the following non-space character. */
    if (!line.cells.empty()) {
        size_t ink_cells = line.cells.size();
        while (ink_cells > 0 && (line.cells[ink_cells - 1].flags & CELL_SPACE)) --ink_cells;
        if (ink_cells > 0 && ink_cells < line.cells.size()) {
            line.cells.resize(ink_cells);
            line.end_offset =
                static_cast<size_t>(line.cells.back().offset) + line.cells.back().len;
            /* line.next already points at the first character of the next
             * line; the trimmed spaces simply do not get a span. */
        }
    }

    line.empty = false;
    line.width = measured_width(line.cells, static_cast<int>(line.cells.size()));
    int ascent = 0;
    int descent = 0;
    int lh = 0;
    for (const Cell& c : line.cells) {
        int a = static_cast<int>(c.asc) - c.delta;
        int d = static_cast<int>(c.desc) + c.delta;
        if (a > ascent) ascent = a;
        if (d > descent) descent = d;
        int cell_lh = (c.px * line_percent_ + 50) / 100;
        int glyph = static_cast<int>(c.asc) + static_cast<int>(c.desc);
        if (cell_lh < glyph) cell_lh = glyph;
        if (cell_lh > lh) lh = cell_lh;
    }
    if (lh < ascent + descent) lh = ascent + descent;
    if (lh < 1) lh = 1;
    line.ascent = ascent;
    line.line_height = lh;
    return line;
}

bool Builder::page_empty() const {
    const repub_page& p = out_.pages[page_index_];
    return p.spans.empty() && p.images.empty();
}

bool Builder::new_page(uint32_t start_offset) {
    if (out_.pages.size() >= max_pages_) {
        stopped_ = true;
        return false;
    }
    /* Close the page that was just filled before starting the next one. */
    if (!out_.pages.empty()) finish_page();
    repub_page page;
    page.chapter = chapter_;
    page.info.start_offset = start_offset;
    page.info.end_offset = start_offset;
    page.info.width = spec_.page_rect.w;
    page.info.height = spec_.page_rect.h;
    page.info.primary_image_index = -1;
    out_.pages.push_back(std::move(page));
    page_index_ = out_.pages.size() - 1;
    y_ = page_index_ == 0 ? page_top_ + first_page_extra_ : page_top_;
    line_index_ = 0;
    covered_end_ = start_offset;
    pending_links_.clear();
    return true;
}

bool Builder::ensure_space(int leading, int height, uint32_t offset) {
    int lead = page_empty() ? 0 : leading;
    if (y_ + lead + height > bottom_ && !page_empty()) {
        if (!new_page(offset)) return false;
        lead = 0;
    }
    y_ += lead;
    return true;
}

void Builder::cover(uint32_t end_offset) {
    if (end_offset > covered_end_) covered_end_ = end_offset;
}

void Builder::finish_page() {
    if (out_.pages.empty()) return;
    repub_page& page = out_.pages[page_index_];
    if (covered_end_ < page.info.start_offset) covered_end_ = page.info.start_offset;
    page.info.end_offset = covered_end_;
    page.info.span_count = page.spans.size();
    page.info.image_count = page.images.size();
    page.info.note_count = page.notes.size();
    page.info.primary_image_index =
        (page.images.size() == 1 && page.spans.empty()) ? page.images[0].image_index : -1;
    page.links.clear();
    page.link_hrefs.clear();
    page.link_hrefs.reserve(pending_links_.size());
    for (const PendingLink& pl : pending_links_) page.link_hrefs.push_back(pl.href);
    page.links.resize(pending_links_.size());
    for (size_t i = 0; i < pending_links_.size(); ++i) {
        page.links[i].href = page.link_hrefs[i].c_str();
        page.links[i].rect = pending_links_[i].rect;
    }
    page.info.link_count = page.links.size();
    page.info.span_count = page.spans.size();
    out_.page_start_offsets.push_back(page.info.start_offset);
}

void Builder::emit_line(const LineBuild& line, const BlockStyle& style, bool is_last) {
    if (line.empty) return;
    if (!ensure_space(0, line.line_height, static_cast<uint32_t>(line.cells.front().offset))) return;

    repub_page& page = current_page();
    const int block_avail = content_w_ - style.inset_left - style.inset_right;
    const int avail = std::max(1, block_avail - line.indent);
    const size_t count = line.cells.size();

    size_t visible_end = count;
    int trailing = 0;
    while (visible_end > 0 && (line.cells[visible_end - 1].flags & CELL_SPACE)) {
        trailing += line.cells[visible_end - 1].advance;
        --visible_end;
    }
    int ink_width = line.width - trailing;
    if (visible_end == 0) ink_width = 0;
    if (ink_width > avail) ink_width = avail;
    if (ink_width < 0) ink_width = 0;

    int offset_align = 0;
    if (style.centered) {
        offset_align = (avail - ink_width) / 2;
    } else if (style.right) {
        offset_align = avail - ink_width;
    }
    if (offset_align < 0) offset_align = 0;

    int gap_count = 0;
    for (size_t i = 1; i < visible_end; ++i) {
        if (line.cells[i].flags & CELL_SPACE) ++gap_count;
    }
    int per_space = 0;
    int rem = 0;
    bool justify = style.justify && !is_last && gap_count > 0 && avail > ink_width;
    if (justify) {
        int extra = avail - ink_width;
        per_space = extra / gap_count;
        rem = extra % gap_count;
    }

    const int baseline = y_ + line.ascent;
    std::vector<int> cell_x(count);
    {
        int cursor = content_x_ + style.inset_left + line.indent + offset_align;
        int rem_left = rem;
        for (size_t i = 0; i < count; ++i) {
            cell_x[i] = cursor;
            int adv = line.cells[i].advance;
            if (justify && i < visible_end && (line.cells[i].flags & CELL_SPACE)) {
                adv += per_space;
                if (rem_left > 0) {
                    ++adv;
                    --rem_left;
                }
            }
            cursor += adv;
            if (i + 1 < count) cursor += spec_.tracking_px;
        }
    }

    size_t i = 0;
    while (i < count) {
        const Cell& first = line.cells[i];
        size_t j = i + 1;
        while (j < count) {
            const Cell& c = line.cells[j];
            const Cell& p = line.cells[j - 1];
            if (c.face != first.face || c.px != first.px || c.style != first.style ||
                c.delta != first.delta)
                break;
            if (c.offset != static_cast<uint32_t>(p.offset) + p.len) break;
            /* Justified lines are split after each space so span.x carries the
             * stretched gap; the renderer draws each span at span.x with the
             * natural advances of its text. */
            if (justify && (p.flags & CELL_SPACE)) break;
            ++j;
        }
        /* span.width is the natural advance of the span text (tracking
         * included), never the full line box. */
        int span_advance = 0;
        for (size_t k = i; k < j; ++k) {
            span_advance += line.cells[k].advance;
            if (k + 1 < j) span_advance += spec_.tracking_px;
        }
        repub_span_t span{};
        span.text_offset = first.offset;
        span.text_len = 0;
        for (size_t k = i; k < j; ++k) span.text_len += line.cells[k].len;
        span.x = cell_x[i];
        span.y = baseline + first.delta;
        span.width = std::max(0, span_advance);
        span.height = std::max(1, static_cast<int>(first.asc) + static_cast<int>(first.desc));
        span.line_index = static_cast<uint16_t>(line_index_ > 0xFFFF ? 0xFFFF : line_index_);
        span.font_size_px = first.px;
        span.style = first.style;
        page.spans.push_back(span);
        page.span_extra.push_back(SpanExtra{first.face, 0});

        if (style.is_link) {
            PendingLink pl;
            pl.href = flow_.links[static_cast<size_t>(style.link_index)].href;
            pl.rect = repub_rect_t{span.x, span.y - first.asc, span.width, span.height};
            pending_links_.push_back(std::move(pl));
        }

        for (size_t k = i; k < j; ++k) {
            const Cell& cell = line.cells[k];
            if (cell.note < 0) continue;
            repub_page_note_marker_t marker{};
            marker.note_index = cell.note;
            marker.icon = cell.icon;
            marker.rect = repub_rect_t{cell_x[k], baseline + cell.delta - cell.asc,
                                       std::max(1, cell.advance),
                                       std::max(1, static_cast<int>(cell.asc) + static_cast<int>(cell.desc))};
            page.notes.push_back(marker);
        }
        i = j;
    }
    cover(static_cast<uint32_t>(line.end_offset));
    y_ += line.line_height;
    ++line_index_;
}

/* ----------------------------------------------------------------------- */
/* Text block layout                                                       */
/* ----------------------------------------------------------------------- */

bool Builder::layout_text_block(const FlowBlock& block) {
    if (block.len == 0) return true;
    size_t limit = static_cast<size_t>(block.offset) + block.len;
    if (limit > flow_.text.size()) limit = flow_.text.size();
    size_t pos = block.offset;
    if (pos >= limit) return true;
    block_limit_ = limit;

    BlockStyle style = block_style(block);
    style.face = resolve_face(style.family_id, style.span_style);
    /* Usable text width after block-level insets (blockquotes). */
    const int text_w = std::max(1, content_w_ - style.inset_left - style.inset_right);
    if (style.first_indent >= text_w) style.first_indent = text_w > 1 ? text_w - 1 : 0;

    run_cursor_ = block.run_first;
    block_limit_ = limit;
    run_end_ = static_cast<size_t>(block.run_first) + block.run_count;
    if (run_end_ > flow_.runs.size()) run_end_ = flow_.runs.size();
    if (run_cursor_ > run_end_) run_cursor_ = run_end_;

    /* Drop cap: reserve a scaled first glyph across `drop_cap_lines` lines. */
    size_t cap_bytes = 0;
    int cap_px = 0;
    int cap_w = 0;
    int cap_h = 0;
    int cap_lines = style.drop_cap_lines;
    if (cap_lines > 0 && pos < limit) {
        uint32_t first_cp = 0;
        size_t first_n = decode_cp(flow_.text.data() + pos, limit - pos, &first_cp);
        cap_bytes = first_n;
        if ((prohibited_line_start(first_cp) || prohibited_line_end(first_cp)) && pos + first_n < limit) {
            uint32_t second_cp = 0;
            size_t second_n = decode_cp(flow_.text.data() + pos + first_n, limit - pos - first_n, &second_cp);
            (void)second_cp;
            if (second_n > 0 && cap_bytes + second_n <= 12) cap_bytes += second_n;
        }
        if (cap_bytes > 0) {
            std::string_view cap_text(flow_.text.data() + pos, cap_bytes);
            int lh = std::max(1, (style.px * line_percent_ + 50) / 100);
            cap_h = cap_lines * lh;
            if (cap_h > content_h_) {
                cap_lines = std::max(1, content_h_ / lh);
                cap_h = cap_lines * lh;
            }
            cap_px = std::min(0xFFFF, cap_lines * style.px);
            cap_w = fonts_.text_width(style.face, cap_px, cap_text);
            if (cap_w <= 0) cap_w = cap_px;
            int max_w = text_w * 3 / 5;
            if (cap_w > max_w && max_w > 0) {
                cap_px = std::max(1, cap_px * max_w / cap_w);
                cap_w = fonts_.text_width(style.face, cap_px, cap_text);
                if (cap_w <= 0) cap_w = cap_px;
            }
        }
    }

    int cap_lines_left = cap_lines;
    size_t block_start = pos;
    bool first_line = true;
    bool cap_emitted = false;

    while (pos < limit && !stopped_) {
        uint32_t cp = 0;
        size_t n = decode_cp(flow_.text.data() + pos, limit - pos, &cp);
        if (!n) break;
        if (cp == '\r') {
            pos += n;
            continue;
        }
        if (cp == '\n') {
            pos += n;
            first_line = true;
            continue;
        }

        if (!cap_emitted && cap_bytes > 0 && pos == block_start) {
            if (!ensure_space(style.margin_before, cap_h, static_cast<uint32_t>(pos))) return true;
            repub_page& page = current_page();
            int cap_asc = fonts_.ascender(style.face, cap_px);
            repub_span_t span{};
            span.text_offset = static_cast<uint32_t>(pos);
            span.text_len = static_cast<uint32_t>(cap_bytes);
            span.x = content_x_ + style.inset_left;
            span.y = y_ + cap_asc;
            span.width = cap_w;
            span.height = cap_h;
            span.line_index = 0;
            span.font_size_px = static_cast<uint16_t>(cap_px > 0xFFFF ? 0xFFFF : cap_px);
            span.style = style.span_style;
            page.spans.push_back(span);
            page.span_extra.push_back(SpanExtra{static_cast<int16_t>(style.face), 0});
            cover(static_cast<uint32_t>(pos + cap_bytes));
            if (style.is_link) {
                PendingLink pl;
                pl.href = flow_.links[static_cast<size_t>(style.link_index)].href;
                pl.rect = repub_rect_t{span.x, span.y - cap_asc, span.width, cap_h};
                pending_links_.push_back(std::move(pl));
            }
            cap_emitted = true;
            pos += cap_bytes;
            first_line = false;
            continue;
        }

        bool in_cap = cap_lines_left > 0 && cap_bytes > 0;
        int cap_indent = in_cap ? std::min(text_w - 1, cap_w + style.px / 4)
                                : style.first_indent;
        if (cap_indent < 0) cap_indent = 0;
        BlockStyle line_style = style;
        line_style.first_indent = cap_indent;
        LineBuild line = build_line(pos, limit, line_style, text_w,
                                    in_cap ? true : first_line);
        if (line.empty) {
            pos = line.next > pos ? line.next : pos + 1;
            if (line.hard_end) first_line = true;
            continue;
        }
        if (in_cap) {
            line.indent = cap_indent;
        }
        if (!ensure_space(first_line ? style.margin_before : 0, line.line_height,
                          static_cast<uint32_t>(line.cells.front().offset)))
            return true;
        emit_line(line, style, line.hard_end);
        if (cap_lines_left > 0 && in_cap) --cap_lines_left;
        first_line = false;
        pos = line.next;
        if (line.hard_end) first_line = true;
    }

    int gap = style.margin_after;
    if (!block.line_break_after && gap_percent_ > 0) gap += (style.px * gap_percent_ + 50) / 100;
    if (gap > 0 && !stopped_) {
        y_ += gap;
        if (y_ > bottom_) y_ = bottom_;
    }
    return true;
}

bool Builder::layout_image_block(const FlowBlock& block) {
    int32_t image_index = block.image_index;
    if (!spec_.images_visible || image_index < 0 ||
        static_cast<size_t>(image_index) >= flow_.images.size()) {
        cover(static_cast<uint32_t>(block.offset) + block.len);
        return true;
    }
    const FlowImage& image = flow_.images[static_cast<size_t>(image_index)];
    const int avail_w = std::max(1, spec_.image_full_bleed ? spec_.page_rect.w : content_w_);
    int w = 0;
    int h = 0;
    bool known = image.width > 0 && image.height > 0;
    if (known) {
        int64_t cw = avail_w;
        int64_t ch = content_h_;
        w = image.width;
        h = image.height;
        if (w > cw) {
            h = static_cast<int>(static_cast<int64_t>(h) * cw / w);
            w = static_cast<int>(cw);
        }
        if (h > ch) {
            w = static_cast<int>(static_cast<int64_t>(w) * ch / h);
            h = static_cast<int>(ch);
        }
    } else {
        w = avail_w;
        h = content_h_ * 3 / 5;
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    int x = spec_.image_full_bleed ? spec_.page_rect.x
                                   : content_x_ + (content_w_ - w) / 2;
    if (x < spec_.page_rect.x) x = spec_.page_rect.x;

    /* An illustration that fills the page (or has no usable metrics fallback)
     * gets a page of its own; text resumes on the next page. */
    const bool own_page = (h >= content_h_ - 1) || (!known && h >= content_h_ * 3 / 5);
    if (own_page) {
        if (!page_empty()) {
            if (!new_page(static_cast<uint32_t>(block.offset))) return true;
        }
        repub_page& page = current_page();
        repub_page_image_t placement{};
        placement.image_index = image_index;
        int iy = page_top_ + (content_h_ - h) / 2;
        if (iy < page_top_) iy = page_top_;
        placement.rect = repub_rect_t{x, iy, w, h};
        page.images.push_back(placement);
        cover(static_cast<uint32_t>(block.offset) + block.len);
        y_ = bottom_;
        if (block.offset + block.len < flow_.text.size()) {
            if (!new_page(static_cast<uint32_t>(block.offset) + block.len)) return true;
        }
        return true;
    }

    int leading = page_empty() ? 0 : default_gap();
    const int caption_reserve =
        caption_follows() ? std::max(40, body_px_ * line_percent_ / 100 * 2) : 0;
    if (y_ + leading + h + caption_reserve > bottom_ && !page_empty()) {
        if (!new_page(static_cast<uint32_t>(block.offset))) return true;
        leading = 0;
    }
    y_ += leading;
    repub_page& page = current_page();
    repub_page_image_t placement{};
    placement.image_index = image_index;
    placement.rect = repub_rect_t{x, y_, w, h};
    page.images.push_back(placement);
    cover(static_cast<uint32_t>(block.offset) + block.len);
    y_ += h + default_gap();
    return true;
}

bool Builder::layout_hr_block(const FlowBlock& block) {
    int h = std::max(1, body_px_ / 2);
    int leading = page_empty() ? 0 : h;
    if (!ensure_space(leading, h, static_cast<uint32_t>(block.offset))) return true;
    y_ += h;
    cover(static_cast<uint32_t>(block.offset) + block.len);
    return true;
}

void Builder::emit_block(const FlowBlock& block) {
    if (stopped_) return;
    if (block.type == REPUB_BLOCK_IMAGE || block.image_index >= 0) {
        layout_image_block(block);
        return;
    }
    if (block.type == REPUB_BLOCK_HR) {
        layout_hr_block(block);
        return;
    }
    layout_text_block(block);
}

/* ----------------------------------------------------------------------- */
/* Vertical layout: columns top-to-bottom, first column at the right.      */
/* ----------------------------------------------------------------------- */

bool Builder::layout_vertical_block(const FlowBlock& block) {
    if (block.len == 0) return true;
    size_t limit = static_cast<size_t>(block.offset) + block.len;
    if (limit > flow_.text.size()) limit = flow_.text.size();
    size_t pos = block.offset;
    if (pos >= limit) return true;
    block_limit_ = limit;

    BlockStyle style = block_style(block);
    style.face = resolve_face(style.family_id, style.span_style);
    const int pitch = std::max(1, (style.px * line_percent_ + 50) / 100);

    run_cursor_ = block.run_first;
    run_end_ = static_cast<size_t>(block.run_first) + block.run_count;
    if (run_end_ > flow_.runs.size()) run_end_ = flow_.runs.size();
    if (run_cursor_ > run_end_) run_cursor_ = run_end_;

    if (v_column_top_ == 0) v_column_top_ = page_top_ + first_page_extra_;
    if (v_cursor_x_ < 0) v_cursor_x_ = content_x_ + content_w_;
    int column_x = v_cursor_x_ - pitch;
    bool first_line = true;

    while (pos < limit && !stopped_) {
        uint32_t cp = 0;
        size_t n = decode_cp(flow_.text.data() + pos, limit - pos, &cp);
        if (!n) break;
        if (cp == '\r') {
            pos += n;
            continue;
        }
        if (cp == '\n') {
            pos += n;
            first_line = true;
            continue;
        }

        /* Start a new page when this block's pitch no longer fits. */
        if (v_cursor_x_ - pitch < content_x_) {
            v_column_ = 0;
            if (!new_page(static_cast<uint32_t>(pos))) return true;
            v_column_top_ = page_index_ == 0 ? page_top_ + first_page_extra_ : page_top_;
            v_cursor_x_ = content_x_ + content_w_;
        }
        const int margin = first_line ? style.margin_before : 0;
        const int indent = first_line ? style.first_indent : 0;
        const int avail = std::max(pitch, bottom_ - (v_column_top_ + margin) - indent);
        column_x = v_cursor_x_ - pitch;

        LineBuild col;
        col.indent = indent;
        col.cells.reserve(64);
        int width = 0;
        int candidate = -1;
        size_t p = pos;
        bool hard_end = false;
        while (p < limit) {
            uint32_t c = 0;
            size_t cn = decode_cp(flow_.text.data() + p, limit - p, &c);
            if (!cn) break;
            if (c == '\r') {
                p += cn;
                continue;
            }
            if (c == '\n') {
                hard_end = true;
                break;
            }
            if (!style.preserve && col.cells.empty() && is_space_cp(c)) {
                p += cn;
                continue;
            }
            CharStyle cs = char_style(style, p, limit);
            Cell cell = make_cell(p, cn, c, cs);
            int adv = std::max(1, static_cast<int>(cell.px));
            cell.advance = adv;
            if (!col.cells.empty() && is_combining_cp(c)) {
                Cell& prev = col.cells.back();
                if (prev.len <= 0xFFF0) prev.len = static_cast<uint16_t>(prev.len + cn);
                p += cn;
                continue;
            }
            const int track = col.cells.empty() ? 0 : spec_.tracking_px;
            const int cand = width + adv + track;
            const bool opportunity = !col.cells.empty() && break_opportunity(col.cells.back(), cell);
            if (cand > avail && !col.cells.empty()) {
                int bp = opportunity ? static_cast<int>(col.cells.size()) : candidate;
                if (bp > 0) bp = adjust_break(col, cell, bp);
                if (bp <= 0) bp = static_cast<int>(col.cells.size());
                col.cells.resize(static_cast<size_t>(bp));
                col.end_offset = col.cells.empty()
                                     ? p
                                     : static_cast<size_t>(col.cells.back().offset) + col.cells.back().len;
                col.next = col.end_offset;
                col.hard_end = false;
                break;
            }
            if (opportunity) candidate = static_cast<int>(col.cells.size());
            width = cand;
            col.cells.push_back(cell);
            p += cn;
        }
        if (col.cells.empty()) {
            col.hard_end = pos + n >= limit;
            col.next = (hard_end && p < limit) ? p + 1 : (p > pos ? p : pos + 1);
            pos = col.next;
            if (hard_end) first_line = true;
            continue;
        }
        if (p >= limit) {
            col.hard_end = true;
            col.end_offset = limit;
            col.next = limit;
        } else if (hard_end) {
            col.hard_end = true;
            col.end_offset = p;
            size_t next = p + 1;
            if (flow_.text[p] == '\r' && next < limit && flow_.text[next] == '\n') ++next;
            col.next = next;
        }
        if (!col.hard_end) {
            col.end_offset = static_cast<size_t>(col.cells.back().offset) + col.cells.back().len;
            col.next = col.end_offset;
        }
        col.empty = false;
        col.width = width;
        col.line_height = width;

        /* Column pacing: text columns all start at the page top and advance
         * right-to-left one pitch at a time (handled at the loop head). */
        int top = v_column_top_ + margin;

        repub_page& page = current_page();
        /* Justify the column vertically by stretching spaces when enabled. */
        int gap_count = 0;
        for (size_t k = 1; k < col.cells.size(); ++k) {
            if (col.cells[k].flags & CELL_SPACE) ++gap_count;
        }
        int per_space = 0;
        int rem = 0;
        bool justify = style.justify && !col.hard_end && gap_count > 0;
        if (justify) {
            int room = avail - width;
            if (room > 0) {
                per_space = room / gap_count;
                rem = room % gap_count;
            } else {
                justify = false;
            }
        }
        std::vector<int> cell_y(col.cells.size());
        {
            int cursor = top + indent;
            int rem_left = rem;
            for (size_t k = 0; k < col.cells.size(); ++k) {
                cell_y[k] = cursor;
                int adv = std::max(1, static_cast<int>(col.cells[k].px));
                if (justify && (col.cells[k].flags & CELL_SPACE)) {
                    adv += per_space;
                    if (rem_left > 0) {
                        ++adv;
                        --rem_left;
                    }
                }
                cursor += adv;
                if (k + 1 < col.cells.size()) cursor += spec_.tracking_px;
            }
        }

        size_t i = 0;
        while (i < col.cells.size()) {
            const Cell& first = col.cells[i];
            size_t j = i + 1;
            while (j < col.cells.size()) {
                const Cell& c = col.cells[j];
                const Cell& prev = col.cells[j - 1];
                if (c.face != first.face || c.px != first.px || c.style != first.style) break;
                if (c.offset != static_cast<uint32_t>(prev.offset) + prev.len) break;
                ++j;
            }
            int run_len = cell_y[j - 1] - cell_y[i] + std::max(1, static_cast<int>(col.cells[j - 1].px));
            repub_span_t span{};
            span.text_offset = first.offset;
            span.text_len = 0;
            for (size_t k = i; k < j; ++k) span.text_len += col.cells[k].len;
            span.x = column_x;
            span.y = cell_y[i];
            span.width = pitch;
            span.height = std::max(1, run_len);
            span.line_index = static_cast<uint16_t>(line_index_ > 0xFFFF ? 0xFFFF : line_index_);
            span.font_size_px = first.px;
            span.style = static_cast<uint8_t>(first.style | REPUB_SPAN_VERTICAL);
            page.spans.push_back(span);
            page.span_extra.push_back(SpanExtra{first.face, 0});
            if (style.is_link) {
                PendingLink pl;
                pl.href = flow_.links[static_cast<size_t>(style.link_index)].href;
                pl.rect = repub_rect_t{span.x, span.y, span.width, span.height};
                pending_links_.push_back(std::move(pl));
            }
            for (size_t k = i; k < j; ++k) {
                const Cell& cell = col.cells[k];
                if (cell.note < 0) continue;
                repub_page_note_marker_t marker{};
                marker.note_index = cell.note;
                marker.icon = cell.icon;
                marker.rect = repub_rect_t{column_x, cell_y[k], pitch,
                                           std::max(1, static_cast<int>(cell.px))};
                page.notes.push_back(marker);
            }
            i = j;
        }
        cover(static_cast<uint32_t>(col.end_offset));
        line_index_ = line_index_ < 0xFFFF ? line_index_ + 1 : line_index_;
        v_cursor_x_ = column_x;
        ++v_column_;
        first_line = col.hard_end;
        pos = col.next;
    }
    return true;
}

bool Builder::layout_vertical() {
    if (out_.pages.empty()) {
        if (!new_page(0)) return false;
    }
    v_column_ = 0;
    v_column_top_ = page_index_ == 0 ? page_top_ + first_page_extra_ : page_top_;
    v_cursor_x_ = content_x_ + content_w_;
    for (size_t i = 0; i < flow_.blocks.size() && !stopped_; ++i) {
        current_block_index_ = i;
        const FlowBlock& block = flow_.blocks[i];
        if (block.page_break_before && !page_empty()) {
            if (!new_page(block.offset)) break;
            reset_vertical_columns();
        }
        if (block.type == REPUB_BLOCK_IMAGE || block.image_index >= 0) {
            layout_image_block(block);
            if (stopped_) break;
            /* Illustrations own their page in vertical setting. */
            if (i + 1 < flow_.blocks.size() && !page_empty()) {
                if (!new_page(flow_.blocks[i + 1].offset)) break;
            }
            reset_vertical_columns();
            continue;
        }
        if (block.type == REPUB_BLOCK_HR) {
            int h = std::max(1, body_px_ / 2);
            y_ += h;
            if (y_ > bottom_) y_ = bottom_;
            cover(block.offset + block.len);
            continue;
        }
        layout_vertical_block(block);
        if (block.page_break_after && i + 1 < flow_.blocks.size()) {
            if (!new_page(flow_.blocks[i + 1].offset)) break;
            reset_vertical_columns();
        }
    }
    return true;
}

/* ----------------------------------------------------------------------- */
/* Driver                                                                  */
/* ----------------------------------------------------------------------- */

bool Builder::run() {
    if (!new_page(0)) return true;

    if (flow_.blocks.empty()) {
        if (!flow_.text.empty()) {
            FlowBlock synthetic;
            synthetic.offset = 0;
            synthetic.len = static_cast<uint32_t>(flow_.text.size());
            synthetic.type = REPUB_BLOCK_PARAGRAPH;
            if (vertical_) {
                layout_vertical_block(synthetic);
            } else {
                layout_text_block(synthetic);
            }
        }
        finish_page();
        return true;
    }

    if (vertical_) {
        layout_vertical();
    } else {
        for (size_t i = 0; i < flow_.blocks.size() && !stopped_; ++i) {
            current_block_index_ = i;
            const FlowBlock& block = flow_.blocks[i];
            if (block.page_break_before && !page_empty()) {
                if (!new_page(block.offset)) break;
            }
            emit_block(block);
            if (block.page_break_after && i + 1 < flow_.blocks.size()) {
                if (out_.pages.size() >= max_pages_) {
                    stopped_ = true;
                    break;
                }
                if (y_ > page_top_ || !page_empty()) {
                    if (!new_page(flow_.blocks[i + 1].offset)) break;
                }
            }
        }
    }
    finish_page();
    return true;
}

uint32_t next_generation() {
    static uint32_t generation = 0;
    return ++generation;
}

}  // namespace

/* ----------------------------------------------------------------------- */
/* Public entry points                                                     */
/* ----------------------------------------------------------------------- */

void layout_spec_defaults(repub_layout_spec_t* spec) {
    if (!spec) return;
    std::memset(spec, 0, sizeof(*spec));
    spec->struct_size = sizeof(repub_layout_spec_t);
    spec->font_size_px = 16;
    spec->line_height_percent = 160;
    spec->paragraph_gap_percent = 60;
    spec->tracking_px = 0;
    spec->first_line_indent_em = 2;
    spec->margin_left_px = 0;
    spec->margin_right_px = 0;
    spec->margin_top_px = 0;
    spec->margin_bottom_px = 0;
    spec->writing_mode = REPUB_WRITING_HORIZONTAL;
    spec->images_visible = 1;
    spec->image_full_bleed = 0;
    spec->hyphenation = 1;
    spec->hyphenation_lang = nullptr;
    spec->justify = 1;
    spec->chapter_heading_height_px = 0;
    spec->max_pages = 0;
}

void layout_clear(repub_layout* layout) {
    if (!layout) return;
    layout->chapter = nullptr;
    layout->spec = repub_layout_spec_t{};
    layout->hyphen_lang.clear();
    layout->pages.clear();
    layout->page_start_offsets.clear();
    layout->vertical = false;
    layout->generation = next_generation();
}

namespace {

repub_err_t build_impl(const repub_chapter* chapter_ptr, const Flow& flow,
                       const repub_layout_spec_t& spec, const FontMetrics& fonts,
                       repub_layout* out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    if (spec.page_rect.w <= 0 || spec.page_rect.h <= 0) return REPUB_ERR_INVALID_ARG;
    if (spec.font_size_px <= 0 || spec.font_size_px > 4096) return REPUB_ERR_INVALID_ARG;

    layout_clear(out);
    out->chapter = chapter_ptr;
    out->spec = spec;
    if (out->spec.line_height_percent < 100 || out->spec.line_height_percent > 250)
        out->spec.line_height_percent = 160;
    if (out->spec.paragraph_gap_percent < 0) out->spec.paragraph_gap_percent = 60;
    if (out->spec.paragraph_gap_percent > 200) out->spec.paragraph_gap_percent = 200;
    out->spec.first_line_indent_em = std::min(3, std::max(0, out->spec.first_line_indent_em));
    out->spec.tracking_px = std::min(8, std::max(-8, out->spec.tracking_px));
    out->spec.margin_left_px = std::max(0, out->spec.margin_left_px);
    out->spec.margin_right_px = std::max(0, out->spec.margin_right_px);
    out->spec.margin_top_px = std::max(0, out->spec.margin_top_px);
    out->spec.margin_bottom_px = std::max(0, out->spec.margin_bottom_px);

    std::string lang;
    if (spec.hyphenation_lang && spec.hyphenation_lang[0]) {
        lang = spec.hyphenation_lang;
    } else if (chapter_ptr && chapter_ptr->book) {
        lang = chapter_ptr->book->language;
    }
    out->hyphen_lang = lang;

    bool hyphenate = false;
#if REPUB_ENABLE_HYPHENATION
    if (spec.hyphenation) {
        hyphenate = hyphenation_available(primary_language(lang));
    }
#endif

    Builder builder(flow, out->spec, fonts, chapter_ptr, *out);
    builder.configure(hyphenate, lang, spec.font_size_px, out->spec.line_height_percent,
                      out->spec.paragraph_gap_percent,
#if REPUB_ENABLE_VERTICAL_TEXT
                      spec.writing_mode == REPUB_WRITING_VERTICAL_RL
#else
                      false
#endif
    );
    builder.prepare();
    out->vertical = builder.vertical();
    builder.set_max_pages(spec.max_pages > 0 ? static_cast<size_t>(spec.max_pages) : REPUB_MAX_PAGES);
    builder.run();
    if (out->pages.empty()) return REPUB_ERR_MEMORY;
    out->generation = next_generation();
    return REPUB_OK;
}

}  // namespace

repub_err_t layout_build(const repub_chapter& chapter, const repub_layout_spec_t& spec,
                         const FontMetrics& fonts, repub_layout* out) {
    return build_impl(&chapter, chapter.flow, spec, fonts, out);
}

repub_err_t layout_build_flow(const repub_chapter& owner, const Flow& flow,
                              const repub_layout_spec_t& spec, const FontMetrics& fonts,
                              repub_layout* out) {
    return build_impl(&owner, flow, spec, fonts, out);
}

}  // namespace repub
