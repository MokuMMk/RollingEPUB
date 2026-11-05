/*
 * RollingEPUB layout tests: synthetic flows + a deterministic stub FontMetrics.
 *
 * SPDX-License-Identifier: MIT
 */

#include "layout/layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "content/flow.h"
#include "layout/hyphen.h"
#include "render/font.h"
#include "test_harness.h"

namespace {

/* ----------------------------------------------------------------------- */
/* Stub font metrics: monospace, fixed proportions, fully deterministic.   */
/* ----------------------------------------------------------------------- */

class StubFont final : public repub::FontMetrics {
   public:
    int text_width(int face, int px, std::string_view utf8) const override {
        int total = 0;
        size_t at = 0;
        while (at < utf8.size()) {
            uint32_t cp = 0;
            size_t n = decode(utf8.data() + at, utf8.size() - at, &cp);
            total += glyph_advance(face, px, cp);
            at += n;
        }
        return total;
    }
    int glyph_advance(int face, int px, uint32_t cp) const override {
        (void)face;
        if (cp == '\t') return px / 2;
        if (cp == 0xFFFC) return px / 2; /* overridden by layout icon width */
        if (is_wide(cp)) return px;
        return px / 2;
    }
    int ascender(int face, int px) const override {
        (void)face;
        return px * 8 / 10;
    }
    int descender(int face, int px) const override {
        (void)face;
        return px * 2 / 10;
    }
    int line_height(int face, int px) const override {
        (void)face;
        return px * 12 / 10;
    }
    bool has_glyph(int face, int px, uint32_t cp) const override {
        (void)face;
        (void)px;
        (void)cp;
        return true;
    }
    int resolve_face(std::string_view family, uint16_t style_flags) const override {
        int face = 0;
        if (style_flags & REPUB_FONT_BOLD) face |= 1;
        if (style_flags & REPUB_FONT_ITALIC) face |= 2;
        if (family == "serif") face |= 4;
        return face;
    }

   private:
    static bool is_wide(uint32_t cp) {
        return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
               (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
               (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x2FA1F);
    }
    static size_t decode(const char* data, size_t size, uint32_t* cp) {
        const unsigned char* u = reinterpret_cast<const unsigned char*>(data);
        if (!size) return 0;
        if (u[0] < 0x80) {
            *cp = u[0];
            return 1;
        }
        size_t n = 0;
        if (u[0] >= 0xC2 && u[0] <= 0xDF) n = 2;
        else if (u[0] >= 0xE0 && u[0] <= 0xEF) n = 3;
        else if (u[0] >= 0xF0 && u[0] <= 0xF4) n = 4;
        else {
            *cp = 0xFFFD;
            return 1;
        }
        if (n > size) {
            *cp = 0xFFFD;
            return 1;
        }
        uint32_t value = u[0] & (0x7F >> n);
        for (size_t i = 1; i < n; ++i) value = (value << 6) | (u[i] & 0x3F);
        *cp = value;
        return n;
    }
};

const StubFont kFonts;

/* ----------------------------------------------------------------------- */
/* Synthetic flow / chapter builder.                                       */
/* ----------------------------------------------------------------------- */

struct Builder {
    repub::repub_chapter chapter;
    repub::Flow& flow = chapter.flow;
    repub::FlowImage dummy_image;

    explicit Builder(uint32_t spine = 1) { chapter.spine_index = spine; }

    size_t add_block(const std::string& text, uint8_t type = REPUB_BLOCK_PARAGRAPH,
                     uint16_t flags = 0, int scale = 100, uint8_t drop_cap = 0,
                     int32_t image_index = -1) {
        repub::FlowBlock block;
        block.offset = static_cast<uint32_t>(flow.text.size());
        flow.text += text;
        block.len = static_cast<uint32_t>(text.size());
        block.type = type;
        block.flags = flags;
        block.font_scale_percent = static_cast<int16_t>(scale);
        block.drop_cap_lines = drop_cap;
        block.image_index = image_index;
        block.run_first = static_cast<uint16_t>(flow.runs.size());
        flow.blocks.push_back(block);
        return flow.blocks.size() - 1;
    }

    size_t add_block_break(const std::string& text, uint8_t page_break_before = 0,
                           uint8_t page_break_after = 0, uint8_t line_break_after = 0,
                           uint8_t type = REPUB_BLOCK_PARAGRAPH) {
        size_t index = add_block(text, type);
        flow.blocks[index].page_break_before = page_break_before;
        flow.blocks[index].page_break_after = page_break_after;
        flow.blocks[index].line_break_after = line_break_after;
        return index;
    }

    void add_run(size_t block_index, uint32_t offset, uint32_t len, uint16_t style,
                 uint16_t scale = 100, int16_t family = -1) {
        repub::Run run;
        run.offset = offset;
        run.len = len;
        run.style = style;
        run.scale_percent = scale;
        run.family_id = family;
        flow.runs.push_back(run);
        flow.blocks[block_index].run_count =
            static_cast<uint16_t>(flow.blocks[block_index].run_count + 1);
    }

    void add_image(int32_t width, int32_t height) {
        repub::FlowImage image;
        image.href = "images/pic.png";
        image.raw_src = "pic.png";
        image.width = width;
        image.height = height;
        flow.images.push_back(image);
    }
};

repub_layout_spec_t make_spec(int w = 200, int h = 300, int font_px = 16) {
    repub_layout_spec_t spec;
    repub::layout_spec_defaults(&spec);
    spec.page_rect = repub_rect_t{0, 0, w, h};
    spec.font_size_px = font_px;
    return spec;
}

repub::repub_layout build(repub::repub_chapter& chapter, const repub_layout_spec_t& spec) {
    repub::repub_layout layout;
    repub_err_t err = repub::layout_build(chapter, spec, kFonts, &layout);
    REPUB_CHECK_EQ(static_cast<int>(err), static_cast<int>(REPUB_OK));
    return layout;
}

/* ----------------------------------------------------------------------- */
/* Small helpers.                                                          */
/* ----------------------------------------------------------------------- */

uint32_t first_cp(std::string_view text) {
    if (text.empty()) return 0;
    const unsigned char* u = reinterpret_cast<const unsigned char*>(text.data());
    size_t n = text.size();
    if (u[0] < 0x80) return u[0];
    size_t len = (u[0] >= 0xF0) ? 4 : (u[0] >= 0xE0) ? 3 : 2;
    if (len > n) return 0xFFFD;
    uint32_t cp = u[0] & (0x7F >> len);
    for (size_t i = 1; i < len; ++i) cp = (cp << 6) | (u[i] & 0x3F);
    return cp;
}

uint32_t last_cp(std::string_view text) {
    if (text.empty()) return 0;
    size_t at = text.size();
    while (at > 0 && (static_cast<unsigned char>(text[at - 1]) & 0xC0) == 0x80) --at;
    if (at > 0) --at;
    return first_cp(text.substr(at));
}

std::string span_text(const repub::Flow& flow, const repub_span_t& span) {
    if (span.text_offset >= flow.text.size()) return {};
    size_t len = std::min<size_t>(span.text_len, flow.text.size() - span.text_offset);
    return flow.text.substr(span.text_offset, len);
}

/* Spans of one horizontal line, ordered left to right. */
std::vector<const repub_span_t*> line_spans(const repub::repub_page& page, uint16_t line_index) {
    std::vector<const repub_span_t*> spans;
    for (const repub_span_t& span : page.spans) {
        if (span.line_index == line_index) spans.push_back(&span);
    }
    std::stable_sort(spans.begin(), spans.end(),
                     [](const repub_span_t* a, const repub_span_t* b) { return a->x < b->x; });
    return spans;
}

bool is_forbidden_line_start(uint32_t cp) {
    switch (cp) {
        case 0x3001: case 0x3002: case 0x3009: case 0x300B: case 0x300D: case 0x300F:
        case 0x3011: case 0x3015: case 0x3017: case 0x3019: case 0x301B:
        case 0xFF01: case 0xFF09: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B:
        case 0xFF1F: case 0xFF3D: case 0xFF5D:
        case 0x2026: case 0x00BB: case 0x2019: case 0x201D: case 0x203A:
            return true;
        default:
            return false;
    }
}

bool is_forbidden_line_end(uint32_t cp) {
    switch (cp) {
        case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014:
        case 0x3016: case 0x3018: case 0x301A:
        case 0xFF08: case 0xFF3B: case 0xFF5B:
        case 0x2018: case 0x201C: case 0x00AB:
            return true;
        default:
            return false;
    }
}

int content_left(const repub_layout_spec_t& spec) {
    return spec.page_rect.x + spec.margin_left_px;
}
int content_right(const repub_layout_spec_t& spec) {
    return spec.page_rect.x + spec.page_rect.w - spec.margin_right_px;
}

}  // namespace

/* ----------------------------------------------------------------------- */
/* Tests                                                                   */
/* ----------------------------------------------------------------------- */

REPUB_TEST(layout_spec_defaults) {
    repub_layout_spec_t spec;
    std::memset(&spec, 0xAA, sizeof(spec));
    repub::layout_spec_defaults(&spec);
    REPUB_CHECK_EQ(spec.font_size_px, 16);
    REPUB_CHECK_EQ(spec.line_height_percent, 160);
    REPUB_CHECK_EQ(spec.paragraph_gap_percent, 60);
    REPUB_CHECK_EQ(spec.first_line_indent_em, 2);
    REPUB_CHECK_EQ(spec.justify, 1);
    REPUB_CHECK_EQ(spec.hyphenation, 1);
    REPUB_CHECK_EQ(spec.images_visible, 1);
    REPUB_CHECK_EQ(spec.image_full_bleed, 0);
    REPUB_CHECK_EQ(spec.margin_left_px, 0);
    REPUB_CHECK_EQ(spec.margin_right_px, 0);
    REPUB_CHECK_EQ(spec.margin_top_px, 0);
    REPUB_CHECK_EQ(spec.margin_bottom_px, 0);
    REPUB_CHECK_EQ(spec.max_pages, 0);
    REPUB_CHECK_EQ(static_cast<int>(spec.writing_mode), static_cast<int>(REPUB_WRITING_HORIZONTAL));
    REPUB_CHECK_EQ(spec.page_rect.w, 0); /* page_rect intentionally untouched */
}

REPUB_TEST(layout_empty_flow_one_page) {
    Builder builder;
    auto spec = make_spec();
    repub::repub_layout layout = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout.pages.size(), 1u);
    REPUB_CHECK_EQ(layout.page_start_offsets.size(), 1u);
    REPUB_CHECK_EQ(layout.pages[0].info.start_offset, 0u);
    REPUB_CHECK_EQ(layout.pages[0].info.end_offset, 0u);
    REPUB_CHECK_EQ(layout.pages[0].spans.size(), 0u);
    REPUB_CHECK(layout.pages[0].chapter == &builder.chapter);
}

REPUB_TEST(layout_latin_line_breaking) {
    Builder builder;
    std::string text;
    for (int i = 0; i < 12; ++i) {
        text += "The quick brown fox jumps over the lazy dog. ";
    }
    builder.add_block(text);
    auto spec = make_spec();
    repub::repub_layout layout = build(builder.chapter, spec);
    REPUB_CHECK(layout.pages.size() > 1u);

    for (const repub::repub_page& page : layout.pages) {
        REPUB_CHECK(page.spans.size() > 0u);
        /* offsets monotonic and inside the page */
        uint32_t previous_end = page.info.start_offset;
        for (const repub_span_t& span : page.spans) {
            REPUB_CHECK(span.text_offset >= previous_end);
            REPUB_CHECK(span.text_offset + span.text_len <= page.info.end_offset);
            REPUB_CHECK(span.x >= content_left(spec));
            REPUB_CHECK(span.x + span.width <= content_right(spec) + spec.tracking_px + 1);
            REPUB_CHECK(span.font_size_px == 16);
            previous_end = span.text_offset + span.text_len;
        }
        /* spans on one line never overlap horizontally */
        uint16_t max_line = 0;
        for (const repub_span_t& span : page.spans) max_line = std::max(max_line, span.line_index);
        for (uint16_t line = 0; line <= max_line; ++line) {
            std::vector<const repub_span_t*> spans = line_spans(page, line);
            for (size_t i = 1; i < spans.size(); ++i) {
                REPUB_CHECK(spans[i]->x >= spans[i - 1]->x + spans[i - 1]->width);
            }
        }
    }
}

REPUB_TEST(layout_cjk_line_breaking_and_kinsoku) {
    Builder builder;
    std::string text;
    for (int i = 0; i < 20; ++i) text += "\u4E2D\u6587\u6BB5\u843D\u6D4B\u8BD5\u6807\u70B9\u3002\u4E2D\u6587\u6BB5\u843D\uFF08\u6D4B\u8BD5\u6807\u70B9\uFF09\u3002";
    builder.add_block(text);
    auto spec = make_spec();
    repub::repub_layout layout = build(builder.chapter, spec);
    REPUB_CHECK(layout.pages.size() > 1u);

    for (const repub::repub_page& page : layout.pages) {
        uint16_t max_line = 0;
        for (const repub_span_t& span : page.spans) max_line = std::max(max_line, span.line_index);
        for (uint16_t line = 0; line <= max_line; ++line) {
            std::vector<const repub_span_t*> spans = line_spans(page, line);
            if (spans.empty()) continue;
            std::string first_text = span_text(builder.flow, *spans.front());
            std::string last_text = span_text(builder.flow, *spans.back());
            REPUB_CHECK(!first_text.empty());
            REPUB_CHECK(!last_text.empty());
            REPUB_CHECK(!is_forbidden_line_start(first_cp(first_text)));
            REPUB_CHECK(!is_forbidden_line_end(last_cp(last_text)));
        }
        for (const repub_span_t& span : page.spans) {
            REPUB_CHECK(span.width > 0);
            REPUB_CHECK(span.font_size_px == 16);
        }
    }
}

REPUB_TEST(layout_hyphenation_hook_breaks_long_word) {
    const std::string word = "antidisestablishmentarianism";
    repub::HyphenBreak breaks[32];
    int break_count = repub::hyphenate_word(word, "en", breaks, 32);
    REPUB_CHECK(break_count > 0);

    Builder builder;
    builder.add_block(word);
    auto spec = make_spec();
    spec.hyphenation = 1;
    spec.hyphenation_lang = "en";
    repub::repub_layout layout = build(builder.chapter, spec);
    REPUB_CHECK(layout.pages.size() >= 1u);
    /* First line ends inside the word at a pattern- or emergency-approved boundary. */
    const repub::repub_page& page = layout.pages[0];
    std::string first_line;
    for (const repub_span_t& span : page.spans) {
        if (span.line_index == 0) first_line += span_text(builder.flow, span);
    }
    REPUB_CHECK(!first_line.empty());
    REPUB_CHECK(first_line.size() < word.size());
    bool approved = false;
    for (int i = 0; i < break_count; ++i) {
        if (first_line.size() == breaks[i].byte_offset) approved = true;
    }
    /* The emergency UTF-8 boundary split is allowed as a fallback, but with
     * English patterns a pattern break should fit a 148 px line. */
    REPUB_CHECK(approved);
    /* Every span starts/ends on a UTF-8 boundary (no broken multibyte chars). */
    for (const repub_span_t& span : page.spans) {
        REPUB_CHECK(span.text_offset + span.text_len <= builder.flow.text.size());
        if (span.text_offset + span.text_len < builder.flow.text.size()) {
            unsigned char next =
                static_cast<unsigned char>(builder.flow.text[span.text_offset + span.text_len]);
            REPUB_CHECK((next & 0xC0) != 0x80);
        }
    }
}

REPUB_TEST(layout_paragraph_indent_and_spacing) {
    Builder builder;
    builder.add_block("Hello");
    builder.add_block("World");
    auto spec = make_spec();
    spec.justify = 0;
    repub::repub_layout layout = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout.pages.size(), 1u);
    const repub::repub_page& page = layout.pages[0];
    REPUB_CHECK_EQ(page.spans.size(), 2u);
    const repub_span_t& first = page.spans[0];
    const repub_span_t& second = page.spans[1];
    REPUB_CHECK_EQ(first.x, content_left(spec) + 32); /* 2em at 16 px */
    REPUB_CHECK_EQ(second.x, content_left(spec) + 32);
    /* paragraph gap = 60% of 16 px ~= 10 px + one line height (26 px) */
    int baseline_delta = second.y - first.y;
    REPUB_CHECK(baseline_delta >= 26 + 9);
}

REPUB_TEST(layout_no_indent_and_heading) {
    Builder builder;
    builder.add_block("No indent", REPUB_BLOCK_PARAGRAPH, repub::FBLOCK_NO_INDENT);
    builder.add_block("Title", REPUB_BLOCK_HEADING, 0, 100);
    builder.flow.blocks[1].heading_level = 1;
    auto spec = make_spec();
    repub::repub_layout layout = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout.pages.size(), 1u);
    const repub::repub_page& page = layout.pages[0];
    REPUB_CHECK(page.spans.size() >= 2u);
    REPUB_CHECK_EQ(page.spans[0].x, content_left(spec));
    /* heading fallback scale for h1 is 200% */
    bool heading_found = false;
    for (const repub_span_t& span : page.spans) {
        if (span.text_offset == builder.flow.blocks[1].offset) {
            heading_found = true;
            REPUB_CHECK_EQ(span.font_size_px, 32);
            REPUB_CHECK_EQ(span.x, content_left(spec));
        }
    }
    REPUB_CHECK(heading_found);
}

REPUB_TEST(layout_drop_cap) {
    Builder builder;
    std::string text =
        "Once upon a time there was a small layout engine that paginated text very carefully.";
    builder.add_block(text, REPUB_BLOCK_PARAGRAPH, 0, 100, 2);
    auto spec = make_spec();
    spec.justify = 0;
    repub::repub_layout layout = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout.pages.size(), 1u);
    const repub::repub_page& page = layout.pages[0];
    const repub_span_t* cap = nullptr;
    for (const repub_span_t& span : page.spans) {
        if (span.text_offset == 0 && span.font_size_px > 16) cap = &span;
    }
    REPUB_CHECK(cap != nullptr);
    REPUB_CHECK_EQ(cap->text_offset, 0u);
    REPUB_CHECK_EQ(cap->font_size_px, 32); /* 2 lines * 16 px */
    REPUB_CHECK(cap->height >= 50);        /* ~2 * 26 px */
    /* The first two text lines are indented beside the cap. */
    int cap_right = cap->x + cap->width;
    bool found_indented = false;
    for (const repub_span_t& span : page.spans) {
        if (span.text_offset == 0) continue;
        if (span.line_index <= 1) {
            REPUB_CHECK(span.x >= cap_right);
            found_indented = true;
        }
    }
    REPUB_CHECK(found_indented);
}

REPUB_TEST(layout_images) {
    /* Small image flows below text on the same page. */
    {
        Builder builder;
        builder.add_block("Text above the image.");
        builder.add_image(100, 50);
        builder.add_block("", REPUB_BLOCK_IMAGE, 0, 100, 0, 0);
        auto spec = make_spec();
        repub::repub_layout layout = build(builder.chapter, spec);
        REPUB_CHECK_EQ(layout.pages.size(), 1u);
        const repub::repub_page& page = layout.pages[0];
        REPUB_CHECK_EQ(page.images.size(), 1u);
        REPUB_CHECK_EQ(page.images[0].image_index, 0);
        REPUB_CHECK(page.images[0].rect.w > 0);
        REPUB_CHECK(page.images[0].rect.h > 0);
        REPUB_CHECK(page.images[0].rect.x >= content_left(spec));
        REPUB_CHECK(page.images[0].rect.x + page.images[0].rect.w <= content_right(spec) + 1);
        REPUB_CHECK_EQ(page.info.primary_image_index, -1);
    }
    /* A page-filling image becomes an image-only page. */
    {
        Builder builder;
        builder.add_image(100, 1000);
        builder.add_block("", REPUB_BLOCK_IMAGE, 0, 100, 0, 0);
        auto spec = make_spec();
        repub::repub_layout layout = build(builder.chapter, spec);
        REPUB_CHECK_EQ(layout.pages.size(), 1u);
        const repub::repub_page& page = layout.pages[0];
        REPUB_CHECK_EQ(page.spans.size(), 0u);
        REPUB_CHECK_EQ(page.images.size(), 1u);
        REPUB_CHECK_EQ(page.info.primary_image_index, 0);
    }
    /* images_visible = 0 suppresses all image placement. */
    {
        Builder builder;
        builder.add_image(100, 50);
        builder.add_block("", REPUB_BLOCK_IMAGE, 0, 100, 0, 0);
        auto spec = make_spec();
        spec.images_visible = 0;
        repub::repub_layout layout = build(builder.chapter, spec);
        REPUB_CHECK_EQ(layout.pages.size(), 1u);
        REPUB_CHECK_EQ(layout.pages[0].images.size(), 0u);
        REPUB_CHECK_EQ(layout.pages[0].info.primary_image_index, -1);
    }
}

REPUB_TEST(layout_note_markers) {
    /* Text marker: the marker character is a normal glyph. */
    {
        Builder builder;
        const std::string text = "Hello note world";
        size_t block = builder.add_block(text);
        repub::FlowNote note;
        note.marker_offset = static_cast<uint32_t>(text.find("note"));
        note.text = "Footnote body";
        note.href = "#n1";
        note.marker_image = -1;
        builder.flow.notes.push_back(note);
        (void)block;
        auto spec = make_spec();
        repub::repub_layout layout = build(builder.chapter, spec);
        const repub::repub_page& page = layout.pages[0];
        REPUB_CHECK_EQ(page.notes.size(), 1u);
        REPUB_CHECK_EQ(page.notes[0].note_index, 0);
        REPUB_CHECK_EQ(page.notes[0].icon, -1);
        REPUB_CHECK(page.notes[0].rect.w > 0 && page.notes[0].rect.h > 0);
        REPUB_CHECK(page.notes[0].rect.x >= content_left(spec));
        REPUB_CHECK(page.info.note_count == 1u);
    }
    /* Icon marker: U+FFFC placeholder reserves an icon-sized inline box. */
    {
        Builder builder;
        const std::string text = "A\xEF\xBF\xBC" "B";
        builder.add_block(text);
        repub::FlowNote note;
        note.marker_offset = 1; /* byte offset of U+FFFC */
        note.text = "Icon note";
        note.href = "#n2";
        note.marker_image = 0;
        builder.flow.notes.push_back(note);
        auto spec = make_spec();
        repub::repub_layout layout = build(builder.chapter, spec);
        const repub::repub_page& page = layout.pages[0];
        REPUB_CHECK_EQ(page.notes.size(), 1u);
        REPUB_CHECK_EQ(page.notes[0].icon, 0);
        REPUB_CHECK(page.notes[0].rect.w >= 14);
        REPUB_CHECK(page.spans.size() == 1u);
        REPUB_CHECK(page.spans[0].text_len == text.size());
    }
}

REPUB_TEST(layout_links) {
    /* Block-level link. */
    {
        Builder builder;
        size_t block = builder.add_block("Linked text", REPUB_BLOCK_PARAGRAPH, repub::FBLOCK_LINK);
        builder.flow.blocks[block].link_index = 0;
        repub::FlowLink link;
        link.href = "chapter2.xhtml#sec";
        link.kind = 0;
        builder.flow.links.push_back(link);
        auto spec = make_spec();
        repub::repub_layout layout = build(builder.chapter, spec);
        const repub::repub_page& page = layout.pages[0];
        REPUB_CHECK(page.links.size() >= 1u);
        REPUB_CHECK_STR_EQ(page.links[0].href, "chapter2.xhtml#sec");
        REPUB_CHECK(page.links[0].rect.w > 0 && page.links[0].rect.h > 0);
        REPUB_CHECK((page.spans[0].style & REPUB_SPAN_LINK) != 0);
    }
    /* Inline run link bit (no href: underline only). */
    {
        Builder builder;
        size_t block = builder.add_block("before link after");
        builder.add_run(block, 7, 4, repub::RUN_LINK);
        auto spec = make_spec();
        auto layout = build(builder.chapter, spec);
        bool found = false;
        for (const repub_span_t& span : layout.pages[0].spans) {
            if ((span.style & REPUB_SPAN_LINK) && span.text_offset == 7) {
                found = true;
                REPUB_CHECK_EQ(span.text_len, 4u);
            }
        }
        REPUB_CHECK(found);
    }
}

REPUB_TEST(layout_line_break_after_suppresses_gap) {
    Builder builder;
    builder.add_block_break("Line one", 0, 0, 1);
    builder.add_block("Line two");
    auto spec = make_spec();
    spec.justify = 0;
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout.pages.size(), 1u);
    const repub::repub_page& page = layout.pages[0];
    REPUB_CHECK_EQ(page.spans.size(), 2u);
    REPUB_CHECK_EQ(page.spans[1].y - page.spans[0].y, 26);
}

REPUB_TEST(layout_page_break_before_after) {
    Builder builder;
    builder.add_block_break("Page one", 0, 0, 0);
    builder.add_block_break("Page two", 1, 1, 0);
    builder.add_block("Page three");
    auto spec = make_spec();
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout.pages.size(), 3u);
    REPUB_CHECK_EQ(layout.page_start_offsets[1], builder.flow.blocks[1].offset);
    REPUB_CHECK_EQ(layout.page_start_offsets[2], builder.flow.blocks[2].offset);
    REPUB_CHECK_EQ(layout.pages[0].spans.size(), 1u);
    REPUB_CHECK_EQ(layout.pages[1].spans.size(), 1u);
    REPUB_CHECK_EQ(layout.pages[2].spans.size(), 1u);
}

REPUB_TEST(layout_offset_mapping) {
    Builder builder;
    for (int i = 0; i < 30; ++i) builder.add_block("Paragraph number " + std::to_string(i));
    auto spec = make_spec();
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK(layout.pages.size() > 1u);
    REPUB_CHECK_EQ(layout.page_start_offsets.size(), layout.pages.size());
    REPUB_CHECK_EQ(layout.page_start_offsets[0], 0u);
    for (size_t i = 0; i < layout.pages.size(); ++i) {
        const repub_page_info_t& info = layout.pages[i].info;
        REPUB_CHECK_EQ(info.start_offset, layout.page_start_offsets[i]);
        REPUB_CHECK(info.end_offset >= info.start_offset);
        if (i + 1 < layout.pages.size()) {
            REPUB_CHECK(layout.page_start_offsets[i + 1] >= info.end_offset);
            REPUB_CHECK(layout.page_start_offsets[i + 1] > layout.page_start_offsets[i]);
        }
        for (const repub_span_t& span : layout.pages[i].spans) {
            REPUB_CHECK(span.text_offset >= info.start_offset);
            REPUB_CHECK(span.text_offset + span.text_len <= info.end_offset);
        }
    }
    REPUB_CHECK_EQ(layout.pages.back().info.end_offset,
                   static_cast<uint32_t>(builder.flow.text.size()));
}

REPUB_TEST(layout_vertical_columns) {
    Builder builder;
    std::string text;
    for (int i = 0; i < 12; ++i) text += "\u7AD6\u6392\u6587\u5B57\u6D4B\u8BD5\u5185\u5BB9\u3002";
    builder.add_block(text);
    auto spec = make_spec();
    spec.writing_mode = REPUB_WRITING_VERTICAL_RL;
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK(layout.vertical);
    REPUB_CHECK(layout.pages.size() >= 1u);
    const int pitch = 26; /* 160% of 16 px */
    for (const repub::repub_page& page : layout.pages) {
        REPUB_CHECK(page.spans.size() > 0u);
        uint16_t previous_line = 0;
        uint32_t previous_offset = page.info.start_offset;
        bool first = true;
        for (const repub_span_t& span : page.spans) {
            REPUB_CHECK((span.style & REPUB_SPAN_VERTICAL) != 0);
            REPUB_CHECK_EQ(span.width, pitch);
            REPUB_CHECK(span.height > 0);
            REPUB_CHECK(span.x >= content_left(spec));
            REPUB_CHECK(span.x + span.width <= content_right(spec) + 1);
            if (!first) {
                REPUB_CHECK(span.line_index >= previous_line);
                if (span.line_index > previous_line) {
                    REPUB_CHECK(span.x < page.spans[0].x); /* later columns move left */
                    REPUB_CHECK(span.text_offset >= previous_offset);
                }
            }
            first = false;
            previous_line = span.line_index;
            previous_offset = span.text_offset;
        }
        /* First column starts at the right edge of the content area. */
        REPUB_CHECK_EQ(page.spans[0].x, content_right(spec) - pitch);
    }
    for (size_t i = 1; i < layout.pages.size(); ++i) {
        REPUB_CHECK(layout.page_start_offsets[i] >= layout.pages[i - 1].info.end_offset);
    }
}

REPUB_TEST(layout_determinism) {
    Builder builder;
    std::string text;
    for (int i = 0; i < 10; ++i) text += "Determinism paragraph with some words. ";
    builder.add_block(text);
    builder.add_image(80, 40);
    builder.add_block("", REPUB_BLOCK_IMAGE, 0, 100, 0, 0);
    builder.add_block("Tail paragraph.");
    auto spec = make_spec();
    auto layout_a = build(builder.chapter, spec);
    auto layout_b = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout_a.pages.size(), layout_b.pages.size());
    for (size_t i = 0; i < layout_a.pages.size(); ++i) {
        const repub::repub_page& a = layout_a.pages[i];
        const repub::repub_page& b = layout_b.pages[i];
        REPUB_CHECK_EQ(a.info.start_offset, b.info.start_offset);
        REPUB_CHECK_EQ(a.info.end_offset, b.info.end_offset);
        REPUB_CHECK_EQ(a.spans.size(), b.spans.size());
        REPUB_CHECK_EQ(a.images.size(), b.images.size());
        REPUB_CHECK_EQ(a.notes.size(), b.notes.size());
        REPUB_CHECK_EQ(a.links.size(), b.links.size());
        for (size_t k = 0; k < a.spans.size(); ++k) {
            REPUB_CHECK_EQ(a.spans[k].text_offset, b.spans[k].text_offset);
            REPUB_CHECK_EQ(a.spans[k].text_len, b.spans[k].text_len);
            REPUB_CHECK_EQ(a.spans[k].x, b.spans[k].x);
            REPUB_CHECK_EQ(a.spans[k].y, b.spans[k].y);
            REPUB_CHECK_EQ(a.spans[k].width, b.spans[k].width);
            REPUB_CHECK_EQ(a.spans[k].line_index, b.spans[k].line_index);
            REPUB_CHECK_EQ(a.spans[k].font_size_px, b.spans[k].font_size_px);
            REPUB_CHECK_EQ(a.spans[k].style, b.spans[k].style);
            REPUB_CHECK_EQ(a.span_extra[k].face, b.span_extra[k].face);
        }
    }
}

REPUB_TEST(layout_max_pages) {
    Builder builder;
    for (int i = 0; i < 80; ++i) builder.add_block("A fairly long paragraph to fill pages quickly.");
    auto spec = make_spec();
    spec.max_pages = 2;
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout.pages.size(), 2u);
    REPUB_CHECK(layout.pages.back().info.end_offset <= builder.flow.text.size());
    REPUB_CHECK(layout.pages.back().info.end_offset > 0u);
}

REPUB_TEST(layout_build_flow_ownership) {
    Builder owner;
    Builder sub;
    sub.add_block("Popup note text that is laid out on its own.");
    repub::repub_layout layout;
    auto spec = make_spec(120, 160, 14);
    repub_err_t err = repub::layout_build_flow(owner.chapter, sub.flow, spec, kFonts, &layout);
    REPUB_CHECK_EQ(static_cast<int>(err), static_cast<int>(REPUB_OK));
    REPUB_CHECK(layout.chapter == &owner.chapter);
    REPUB_CHECK(!layout.pages.empty());
    REPUB_CHECK(layout.pages[0].chapter == &owner.chapter);
    REPUB_CHECK(layout.pages[0].spans.size() > 0u);
}

REPUB_TEST(layout_superscript_subscript) {
    Builder builder;
    size_t block = builder.add_block("x2");
    builder.add_run(block, 1, 1, repub::RUN_SUPERSCRIPT);
    auto spec = make_spec();
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout.pages.size(), 1u);
    const repub::repub_page& page = layout.pages[0];
    const repub_span_t* base = nullptr;
    const repub_span_t* sup = nullptr;
    for (const repub_span_t& span : page.spans) {
        if (span.text_offset == 0) base = &span;
        if (span.text_offset == 1) sup = &span;
    }
    REPUB_CHECK(base != nullptr);
    REPUB_CHECK(sup != nullptr);
    REPUB_CHECK((sup->style & REPUB_SPAN_SUPERSCRIPT) != 0);
    REPUB_CHECK_EQ(sup->font_size_px, 10); /* 60% of 16 px, rounded */
    REPUB_CHECK(sup->y < base->y);
}

REPUB_TEST(layout_margins_and_chapter_heading) {
    Builder builder;
    for (int i = 0; i < 20; ++i) builder.add_block("Margins paragraph number " + std::to_string(i));
    auto spec = make_spec();
    spec.margin_left_px = 20;
    spec.margin_top_px = 15;
    spec.margin_right_px = 10;
    spec.margin_bottom_px = 5;
    spec.chapter_heading_height_px = 40;
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK(layout.pages.size() >= 2u);
    /* first span sits below the chapter heading reserve */
    const repub_span_t& first = layout.pages[0].spans[0];
    REPUB_CHECK(first.x >= 20);
    REPUB_CHECK(first.y >= 15 + 40);
    /* second page has no heading reserve */
    REPUB_CHECK(layout.pages[1].spans[0].y >= 15);
    REPUB_CHECK(layout.pages[1].spans[0].y < first.y);
    REPUB_CHECK(layout.pages[1].spans[0].x >= 20);
    /* content stays inside the right margin */
    for (const repub::repub_page& page : layout.pages) {
        for (const repub_span_t& span : page.spans) {
            REPUB_CHECK(span.x + span.width <= spec.page_rect.w - 10 + spec.tracking_px + 1);
        }
    }
}
REPUB_TEST(layout_vertical_multiple_blocks_continue_columns) {
    Builder builder;
    builder.add_block("\u7AD6\u6392\u7B2C\u4E00\u6BB5\u5185\u5BB9\u3002");
    builder.add_block("\u7AD6\u6392\u7B2C\u4E8C\u6BB5\u5185\u5BB9\u3002");
    auto spec = make_spec();
    spec.writing_mode = REPUB_WRITING_VERTICAL_RL;
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK(layout.vertical);
    REPUB_CHECK_EQ(layout.pages.size(), 1u);
    const repub::repub_page& page = layout.pages[0];
    REPUB_CHECK(page.spans.size() >= 2u);
    /* The second block continues on the next column to the left instead of
     * restarting at the right edge. */
    bool found_first = false;
    bool found_second = false;
    int first_x = 0;
    int second_x = 0;
    for (const repub_span_t& span : page.spans) {
        if (span.text_offset == builder.flow.blocks[0].offset) {
            found_first = true;
            first_x = span.x;
            REPUB_CHECK_EQ(span.line_index, 0);
        }
        if (span.text_offset == builder.flow.blocks[1].offset) {
            found_second = true;
            second_x = span.x;
        }
    }
    REPUB_CHECK(found_first);
    REPUB_CHECK(found_second);
    REPUB_CHECK(second_x < first_x);
    /* Column x positions stay on the pitch grid and never increase. */
    int previous_x = 1 << 20;
    uint16_t previous_line = 0;
    for (const repub_span_t& span : page.spans) {
        REPUB_CHECK(span.line_index >= previous_line);
        if (span.line_index > previous_line) REPUB_CHECK(span.x < previous_x);
        previous_line = span.line_index;
        previous_x = span.x;
    }
}
REPUB_TEST(layout_css_indent_and_run_scale) {
    /* Mirrors a real parsed paragraph: justify+no_indent flags, explicit CSS
     * text-indent (32 px at body size 16), inherited 180% font scale stored on
     * both the block and the run (must not double-apply). */
    Builder builder;
    size_t block = builder.add_block("Body paragraph words that wrap around.");
    builder.flow.blocks[block].flags = static_cast<uint16_t>(repub::FBLOCK_JUSTIFY | repub::FBLOCK_NO_INDENT);
    builder.flow.blocks[block].indent_px = 32;
    (void)block;
    auto spec = make_spec();
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK_EQ(layout.pages.size(), 1u);
    const repub::repub_page& page = layout.pages[0];
    REPUB_CHECK(page.spans.size() >= 1u);
    REPUB_CHECK_EQ(page.spans[0].x, content_left(spec) + 32);
    /* Second line of the same paragraph is not indented. */
    if (page.spans.size() > 1u) {
        const repub_span_t& second = page.spans[1];
        if (second.line_index != page.spans[0].line_index) REPUB_CHECK_EQ(second.x, content_left(spec));
    }

    Builder heading;
    size_t h = heading.add_block("Heading With Scale");
    heading.flow.blocks[h].type = REPUB_BLOCK_HEADING;
    heading.flow.blocks[h].heading_level = 1;
    heading.flow.blocks[h].font_scale_percent = 180;
    heading.add_run(h, 0, static_cast<uint32_t>(heading.flow.text.size()), 0, 180);
    auto hspec = make_spec(400, 300, 20);
    auto hlayout = build(heading.chapter, hspec);
    REPUB_CHECK_EQ(hlayout.pages.size(), 1u);
    REPUB_CHECK_EQ(hlayout.pages[0].spans[0].font_size_px, 36); /* 20 * 1.8, once */
}

REPUB_TEST(layout_hyphen_prefix_contiguity) {
    /* A long word must break without dropping the line-start fragment. */
    Builder builder;
    std::string text;
    for (int i = 0; i < 6; ++i) text += "antidisestablishmentarianism ";
    builder.add_block(text);
    auto spec = make_spec(150, 300, 16);
    spec.hyphenation = 1;
    spec.hyphenation_lang = "en";
    auto layout = build(builder.chapter, spec);
    REPUB_CHECK(layout.pages.size() >= 1u);
    const repub::Flow& flow = builder.flow;
    for (const repub::repub_page& page : layout.pages) {
        uint32_t previous_end = page.info.start_offset;
        for (const repub_span_t& span : page.spans) {
            if (span.text_offset > previous_end) {
                for (uint32_t at = previous_end; at < span.text_offset; ++at) {
                    unsigned char c = static_cast<unsigned char>(flow.text[at]);
                    REPUB_CHECK(c == ' ' || c == '\t' || c == '\r' || c == '\n');
                }
            }
            REPUB_CHECK(span.text_offset >= previous_end);
            previous_end = span.text_offset + span.text_len;
        }
    }
}
