/*
 * RollingEPUB HTML/CSS -> Flow parser tests.
 *
 * Uses the real EPUB fixtures through the container/book layer, plus
 * synthetic HTML passed straight to parse_chapter() for tokenizer,
 * robustness, note and CSS edge cases.
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "container/book.h"
#include "content/flow.h"
#include "core/text_util.h"
#include "test_harness.h"

namespace {

using namespace repub;

bool has(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

std::string block_text(const Flow& flow, const FlowBlock& block) {
    if (block.offset > flow.text.size()) return std::string();
    if (block.len > flow.text.size() - block.offset) return std::string();
    return flow.text.substr(block.offset, block.len);
}

const FlowBlock* find_block(const Flow& flow, const char* needle, uint8_t type = 0xFF) {
    for (const FlowBlock& block : flow.blocks) {
        if (type != 0xFF && block.type != type) continue;
        if (has(block_text(flow, block), needle)) return &block;
    }
    return nullptr;
}

size_t count_type(const Flow& flow, uint8_t type) {
    size_t count = 0;
    for (const FlowBlock& block : flow.blocks) {
        if (block.type == type) ++count;
    }
    return count;
}

bool run_has(const Flow& flow, uint16_t flag) {
    for (const Run& run : flow.runs) {
        if ((run.style & flag) != 0) return true;
    }
    return false;
}

const FlowBlock* block_at_offset(const Flow& flow, uint32_t offset) {
    for (const FlowBlock& block : flow.blocks) {
        if (offset >= block.offset && offset < block.offset + block.len) return &block;
    }
    return nullptr;
}

bool anchor_offset(const Flow& flow, const char* id, uint32_t* out) {
    for (const FlowAnchor& anchor : flow.anchors) {
        if (anchor.id == id) {
            if (out) *out = anchor.offset;
            return true;
        }
    }
    return false;
}

const FlowLink* find_link(const Flow& flow, const char* needle) {
    for (const FlowLink& link : flow.links) {
        if (has(link.href, needle)) return &link;
    }
    return nullptr;
}

const FlowNote* find_note(const Flow& flow, const char* href) {
    for (const FlowNote& note : flow.notes) {
        if (note.href == href) return &note;
    }
    return nullptr;
}

/* Owns one opened fixture book. */
struct FixtureBook {
    repub::repub_book book{};
    bool ok = false;

    explicit FixtureBook(const char* file) {
        const std::string path = fixture_path(std::string("epub/") + file);
        ok = book_open(&book, nullptr, path.c_str(), nullptr) == REPUB_OK;
    }
    ~FixtureBook() {
        if (ok) book_close(&book);
    }
    FixtureBook(const FixtureBook&) = delete;
    FixtureBook& operator=(const FixtureBook&) = delete;
};

repub::repub_chapter* open_chapter(repub::repub_book& book, size_t index, const ParseOptions& options = ParseOptions{}) {
    repub::repub_chapter* chapter = nullptr;
    REPUB_CHECK(book_chapter_open(&book, index, options, &chapter) == REPUB_OK);
    REPUB_CHECK(chapter != nullptr);
    return chapter;
}

void free_chapter(repub::repub_chapter* chapter) {
    chapter_clear(chapter);
    delete chapter;
}

/* Parse synthetic markup against an opened fixture book (CSS + resources). */
void parse_synthetic(repub::repub_book& book, const std::string& html, repub::repub_chapter* out,
                     const char* base_dir = "OEBPS") {
    const ParseOptions options{};
    REPUB_CHECK(parse_chapter(book, 0, html, base_dir, options, out) == REPUB_OK);
    chapter_finalize(out);
}

/* Structural invariants every parsed chapter must satisfy. */
void check_flow_invariants(const Flow& flow) {
    for (const FlowBlock& block : flow.blocks) {
        REPUB_CHECK(block.offset <= flow.text.size());
        REPUB_CHECK(block.len <= flow.text.size() - block.offset);
        if (block.offset > 0) REPUB_CHECK(flow.text[block.offset - 1] == '\n');
        if (block.offset + block.len < flow.text.size())
            REPUB_CHECK(flow.text[block.offset + block.len] == '\n');
        /* Runs cover the block range contiguously. */
        if (block.run_count == 0) {
            REPUB_CHECK(block.len == 0);
            continue;
        }
        REPUB_CHECK(block.run_first + block.run_count <= flow.runs.size());
        uint32_t expected = block.offset;
        for (size_t i = 0; i < block.run_count; ++i) {
            const Run& run = flow.runs[block.run_first + i];
            REPUB_CHECK(run.offset == expected);
            expected += run.len;
        }
        REPUB_CHECK(expected == block.offset + block.len);
    }
    for (size_t i = 1; i < flow.block_offsets.size(); ++i)
        REPUB_CHECK(flow.block_offsets[i - 1] <= flow.block_offsets[i]);
    for (const FlowNote& note : flow.notes) {
        if (note.marker_image >= 0) {
            REPUB_CHECK(note.marker_offset + 3 <= flow.text.size());
            REPUB_CHECK(flow.text.compare(note.marker_offset, 3, "\xEF\xBF\xBC") == 0);
            REPUB_CHECK(static_cast<size_t>(note.marker_image) < flow.images.size());
        }
        REPUB_CHECK(!note.text.empty());
    }
    for (const FlowLink& link : flow.links) {
        REPUB_CHECK(!link.href.empty());
        if (link.kind == 2) REPUB_CHECK(link.note_index >= 0 &&
                                        static_cast<size_t>(link.note_index) < flow.notes.size());
    }
}

}  // namespace

/* ---------------------------------------------------------------------- */
/* latin.epub: full block/run/link/note/footnote/image model              */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_latin_blocks_runs_notes) {
    FixtureBook fixture("latin.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter* chapter = open_chapter(fixture.book, 0);
    const Flow& flow = chapter->flow;

    /* Markup never leaks, head content and display:none paragraphs are gone. */
    REPUB_CHECK(!flow.text.empty());
    REPUB_CHECK(!has(flow.text, "<"));
    REPUB_CHECK(!has(flow.text, "This hidden paragraph"));
    REPUB_CHECK(!has(flow.text, "Chapter One"));
    REPUB_CHECK(has(flow.text, "The Rolling EPUB"));
    REPUB_CHECK(has(flow.text, "def hello():\n    return \"world\""));
    REPUB_CHECK(has(flow.text, "internal link"));
    REPUB_CHECK(has(flow.text, "\xE2\x80\xA2 First list item"));

    /* Heading block: level 1, 180% scale, centered, bold, no indent. */
    const FlowBlock* heading = find_block(flow, "The Rolling EPUB", REPUB_BLOCK_HEADING);
    REPUB_CHECK(heading != nullptr);
    REPUB_CHECK_EQ(heading->heading_level, 1);
    REPUB_CHECK_EQ(heading->font_scale_percent, 180);
    REPUB_CHECK((heading->flags & FBLOCK_CENTER) != 0);
    REPUB_CHECK((heading->flags & FBLOCK_BOLD) != 0);
    REPUB_CHECK((heading->flags & FBLOCK_NO_INDENT) != 0);

    /* Paragraph CSS: justify + 2em text-indent (2 * 16px reference). */
    const FlowBlock* paragraph = find_block(flow, "A second paragraph follows");
    REPUB_CHECK(paragraph != nullptr);
    REPUB_CHECK_EQ(paragraph->indent_px, 32);
    REPUB_CHECK((paragraph->flags & FBLOCK_JUSTIFY) != 0);
    REPUB_CHECK((paragraph->flags & FBLOCK_NO_INDENT) != 0);

    /* List items carry their bullet inline. */
    REPUB_CHECK(count_type(flow, REPUB_BLOCK_LIST_ITEM) >= 3);

    /* Blockquote is italic. */
    const FlowBlock* quote = find_block(flow, "A quotation sits in a block", REPUB_BLOCK_BLOCKQUOTE);
    REPUB_CHECK(quote != nullptr);
    REPUB_CHECK((quote->flags & FBLOCK_ITALIC) != 0);

    /* <pre> preserves newlines and is flagged as code. */
    const FlowBlock* pre = find_block(flow, "def hello():", REPUB_BLOCK_PRE);
    REPUB_CHECK(pre != nullptr);
    REPUB_CHECK(pre->preserve_whitespace);
    REPUB_CHECK((pre->flags & FBLOCK_CODE) != 0);
    REPUB_CHECK(has(block_text(flow, *pre), "    return \"world\""));

    /* <hr> is a zero-length block. */
    bool found_hr = false;
    for (const FlowBlock& block : flow.blocks) {
        if (block.type == REPUB_BLOCK_HR) {
            found_hr = true;
            REPUB_CHECK_EQ(block.len, 0u);
        }
    }
    REPUB_CHECK(found_hr);

    /* Image paragraph -> image block backed by a FlowImage. */
    const FlowBlock* image_block = nullptr;
    for (const FlowBlock& block : flow.blocks) {
        if (block.type == REPUB_BLOCK_IMAGE) image_block = &block;
    }
    REPUB_CHECK(image_block != nullptr);
    REPUB_CHECK(image_block->image_index >= 0);
    REPUB_CHECK(static_cast<size_t>(image_block->image_index) < flow.images.size());
    const FlowImage& block_image = flow.images[static_cast<size_t>(image_block->image_index)];
    REPUB_CHECK_STR_EQ(block_image.href, "images/pic.png");
    REPUB_CHECK(block_image.width > 0);
    REPUB_CHECK(block_image.height > 0);

    /* Inline style properties land on blocks. */
    const FlowBlock* small = find_block(flow, "Small text.");
    const FlowBlock* big = find_block(flow, "Big text.");
    const FlowBlock* bold = find_block(flow, "Bold class.");
    const FlowBlock* italic = find_block(flow, "Italic class.");
    const FlowBlock* underline = find_block(flow, "Underlined text.");
    const FlowBlock* strike = find_block(flow, "Struck text.");
    const FlowBlock* id_selector = find_block(flow, "stylesheet id selector");
    REPUB_CHECK(small != nullptr);
    REPUB_CHECK(big != nullptr);
    REPUB_CHECK(bold != nullptr);
    REPUB_CHECK(italic != nullptr);
    REPUB_CHECK(underline != nullptr);
    REPUB_CHECK(strike != nullptr);
    REPUB_CHECK(id_selector != nullptr);
    REPUB_CHECK_EQ(small->font_scale_percent, 80);
    REPUB_CHECK_EQ(big->font_scale_percent, 150);
    REPUB_CHECK((bold->flags & FBLOCK_BOLD) != 0);
    REPUB_CHECK((italic->flags & FBLOCK_ITALIC) != 0);
    REPUB_CHECK((underline->flags & FBLOCK_UNDERLINE) != 0);
    REPUB_CHECK((strike->flags & FBLOCK_STRIKE) != 0);
    REPUB_CHECK((id_selector->flags & FBLOCK_BOLD) != 0);

    /* page-break-before from CSS. */
    const FlowBlock* page_break = find_block(flow, "forces a page break before it");
    REPUB_CHECK(page_break != nullptr);
    REPUB_CHECK_EQ(page_break->page_break_before, 1);

    /* ::first-letter initial-letter:2 -> drop cap. */
    const FlowBlock* drop_cap = find_block(flow, "Once upon a time");
    REPUB_CHECK(drop_cap != nullptr);
    REPUB_CHECK_EQ(drop_cap->drop_cap_lines, 2);

    /* Inline style runs. */
    REPUB_CHECK(run_has(flow, RUN_BOLD));
    REPUB_CHECK(run_has(flow, RUN_ITALIC));
    REPUB_CHECK(run_has(flow, RUN_UNDERLINE));
    REPUB_CHECK(run_has(flow, RUN_STRIKE));
    REPUB_CHECK(run_has(flow, RUN_SUPERSCRIPT));
    REPUB_CHECK(run_has(flow, RUN_SUBSCRIPT));
    REPUB_CHECK(run_has(flow, RUN_LINK));

    /* Links: external stays verbatim, internal resolves package-relative. */
    const FlowLink* external = find_link(flow, "https://example.com");
    REPUB_CHECK(external != nullptr);
    REPUB_CHECK_EQ(external->kind, 1);
    const FlowLink* internal = find_link(flow, "chapter2.xhtml");
    REPUB_CHECK(internal != nullptr);
    REPUB_CHECK_EQ(internal->kind, 0);
    REPUB_CHECK_STR_EQ(internal->href, "OEBPS/chapter2.xhtml");

    /* Anchors: id attributes and the footnote target. */
    uint32_t offset = 0;
    REPUB_CHECK(anchor_offset(flow, "special-id", &offset));
    const FlowBlock* special = block_at_offset(flow, offset);
    REPUB_CHECK(special != nullptr);
    REPUB_CHECK(has(block_text(flow, *special), "stylesheet id selector"));
    REPUB_CHECK(anchor_offset(flow, "n1", &offset));
    REPUB_CHECK(anchor_offset(flow, "r1", &offset));
    REPUB_CHECK(anchor_offset(flow, "r2", &offset));
    REPUB_CHECK(anchor_offset(flow, "r3", &offset));

    /* Footnotes: one same-file, two cross-file (text and icon markers). */
    REPUB_CHECK_EQ(flow.notes.size(), 3u);
    const FlowNote* same_file = find_note(flow, "#n1");
    REPUB_CHECK(same_file != nullptr);
    REPUB_CHECK_EQ(same_file->marker_image, -1);
    REPUB_CHECK(has(same_file->text, "historical background"));
    REPUB_CHECK(!has(same_file->text, "r1"));
    const FlowNote* cross_text = find_note(flow, "notes.xhtml#note-x");
    REPUB_CHECK(cross_text != nullptr);
    REPUB_CHECK(has(cross_text->text, "cross-file note lives"));
    const FlowNote* cross_icon = find_note(flow, "notes.xhtml#note-icon");
    REPUB_CHECK(cross_icon != nullptr);
    REPUB_CHECK(cross_icon->marker_image >= 0);
    REPUB_CHECK_STR_EQ(cross_icon->text, "Icon footnote text.");
    REPUB_CHECK_STR_EQ(flow.images[static_cast<size_t>(cross_icon->marker_image)].href,
                       "images/pic.png");
    /* Cross-file note links become kind=2 links with a note index. */
    const FlowLink* note_link = find_link(flow, "notes.xhtml#note-x");
    REPUB_CHECK(note_link != nullptr);
    REPUB_CHECK_EQ(note_link->kind, 2);
    REPUB_CHECK(note_link->note_index >= 0);

    check_flow_invariants(flow);

    /* Public mirrors are consistent with the internal model. */
    REPUB_CHECK_EQ(chapter->public_blocks.size(), flow.blocks.size());
    REPUB_CHECK_EQ(chapter->public_notes.size(), flow.notes.size());
    REPUB_CHECK_EQ(chapter->public_images.size(), flow.images.size());
    REPUB_CHECK_EQ(chapter->public_links.size(), flow.links.size());
    REPUB_CHECK_EQ(chapter->public_blocks[0].type, flow.blocks[0].type);
    REPUB_CHECK(!chapter->title.empty());

    free_chapter(chapter);
}

REPUB_TEST(flow_latin_chapter2_tables) {
    FixtureBook fixture("latin.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter* chapter = open_chapter(fixture.book, 1);
    const Flow& flow = chapter->flow;

    REPUB_CHECK_EQ(count_type(flow, REPUB_BLOCK_TABLE_ROW), 3u);
    const FlowBlock* row = find_block(flow, "Name", REPUB_BLOCK_TABLE_ROW);
    REPUB_CHECK(row != nullptr);
    REPUB_CHECK_STR_EQ(block_text(flow, *row), "Name  Value");
    const FlowBlock* alpha = find_block(flow, "alpha", REPUB_BLOCK_TABLE_ROW);
    REPUB_CHECK(alpha != nullptr);
    REPUB_CHECK_STR_EQ(block_text(flow, *alpha), "alpha  one");

    /* Self-closing anchor target shares the offset of its first block. */
    uint32_t offset = 0;
    REPUB_CHECK(anchor_offset(flow, "target-anchor", &offset));
    const FlowBlock* anchored = block_at_offset(flow, offset);
    REPUB_CHECK(anchored != nullptr);
    REPUB_CHECK(has(block_text(flow, *anchored), "Anchored paragraph target"));

    check_flow_invariants(flow);
    free_chapter(chapter);
}

REPUB_TEST(flow_cjk_ruby_and_image) {
    FixtureBook fixture("cjk.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter* chapter = open_chapter(fixture.book, 0);
    const Flow& flow = chapter->flow;

    REPUB_CHECK(has(flow.text, "\xE6\xB2\xB3\xE8\xBE\xB9\xE8\xAE\xB0\xE4\xBA\x8B")); /* 濞屽疇绔熺拋棰佺皑 */
    REPUB_CHECK(has(flow.text, "\xE6\xB1\x89\xE5\xAD\x97\xE6\xB3\xA8\xE9\x9F\xB3")); /* 濮瑰鐡у▔銊╃叾 */
    REPUB_CHECK(!has(flow.text, "h\xC3\xA0n"));
    REPUB_CHECK(!has(flow.text, "zh\xC3\xBA"));
    REPUB_CHECK(!has(flow.text, "<rt>"));
    REPUB_CHECK(!has(flow.text, "ruby"));

    const FlowBlock* heading = find_block(flow, "\xE6\xB2\xB3\xE8\xBE\xB9", REPUB_BLOCK_HEADING);
    REPUB_CHECK(heading != nullptr);
    REPUB_CHECK_EQ(heading->heading_level, 1);
    REPUB_CHECK(has(flow.text, "\xE3\x80\x82")); /* CJK full stop */

    REPUB_CHECK(count_type(flow, REPUB_BLOCK_BLOCKQUOTE) == 1u);
    REPUB_CHECK(count_type(flow, REPUB_BLOCK_IMAGE) == 1u);
    REPUB_CHECK_EQ(flow.images.size(), 1u);
    REPUB_CHECK_STR_EQ(flow.images[0].href, "images/pic.png");
    REPUB_CHECK(flow.images[0].width > 0);

    check_flow_invariants(flow);
    free_chapter(chapter);
}

/* ---------------------------------------------------------------------- */
/* display:none / hidden attribute                                        */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_display_none) {
    FixtureBook fixture("test_display_none.epub");
    REPUB_CHECK(fixture.ok);
    /* Chapters 1 and 9 are instructions that mention the literal marker in
     * their visible prose; every other chapter must not leak hidden content. */
    static const char* const kForbidden[] = {
        "This paragraph uses class=\"hidden\" and must not appear",
        "This div has class=\"hidden\".",
        "Second paragraph inside hidden div.",
        "secret word",
        "This aside content must not render.",
        "div.removeme must not appear",
        "Paragraph with inline display:none.",
        "Nested paragraph one.",
        "Child has class=\"visible\" but parent is hidden.",
        "This H2 is hidden via class",
        "Full-size hidden H1",
        "Hidden by inline style, no class involved.",
        "Nested second child",
        "Publisher's note: this sidebar contains promotional",
        "Draft note: expand this scene",
        "word count 2847",
        "Scene Break",
        "Deeply nested paragraph.",
    };
    for (size_t index = 0; index < fixture.book.spine.size(); ++index) {
        repub::repub_chapter* chapter = open_chapter(fixture.book, index);
        const Flow& flow = chapter->flow;
        if (index != 0 && index != 8) REPUB_CHECK(!has(flow.text, "[HIDDEN]"));
        for (const char* forbidden : kForbidden) REPUB_CHECK(!has(flow.text, forbidden));
        REPUB_CHECK(!has(flow.text, "hidden_class"));
        REPUB_CHECK(!has(flow.text, "hidden_inline"));
        REPUB_CHECK(!has(flow.text, "hidden_suppressed"));
        REPUB_CHECK(!has(flow.text, "hidden_parent"));
        REPUB_CHECK(!flow.blocks.empty());
        check_flow_invariants(flow);
        free_chapter(chapter);
    }

    /* Chapter 10: only the visible control image and the inline-override
     * image survive; four hidden images must be dropped. */
    repub::repub_chapter* chapter = open_chapter(fixture.book, 9);
    const Flow& flow = chapter->flow;
    REPUB_CHECK_EQ(flow.images.size(), 2u);
    bool visible = false;
    bool override_visible = false;
    for (const FlowImage& image : flow.images) {
        if (has(image.href, "visible.png")) visible = true;
        if (has(image.href, "override_visible.png")) override_visible = true;
    }
    REPUB_CHECK(visible);
    REPUB_CHECK(override_visible);
    REPUB_CHECK_EQ(count_type(flow, REPUB_BLOCK_IMAGE), 2u);
    free_chapter(chapter);
}

REPUB_TEST(flow_hidden_attribute) {
    FixtureBook fixture("test_hidden.epub");
    REPUB_CHECK(fixture.ok);
    static const char* const kForbidden[] = {
        "Paragraph with hidden=\"hidden\" attribute.",
        "Div with hidden=\"hidden\" attribute.",
        "Second child of hidden div.",
        "Paragraph with hidden=\"\" attribute.",
        "Div with hidden=\"\" attribute.",
        "This H2 is hidden via HTML attribute",
        "Full-size hidden H1",
    };
    for (size_t index = 0; index < fixture.book.spine.size(); ++index) {
        repub::repub_chapter* chapter = open_chapter(fixture.book, index);
        const Flow& flow = chapter->flow;
        if (index != 0) REPUB_CHECK(!has(flow.text, "[HIDDEN]"));
        for (const char* forbidden : kForbidden) REPUB_CHECK(!has(flow.text, forbidden));
        free_chapter(chapter);
    }
}

/* ---------------------------------------------------------------------- */
/* sup/sub runs, including CSS class/inline variants                      */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_sup_sub) {
    FixtureBook fixture("test_supsub.epub");
    REPUB_CHECK(fixture.ok);
    size_t sup_runs = 0;
    size_t sub_runs = 0;
    bool found_same_file_note = false;
    for (size_t index = 0; index < fixture.book.spine.size(); ++index) {
        repub::repub_chapter* chapter = open_chapter(fixture.book, index);
        const Flow& flow = chapter->flow;
        for (const Run& run : flow.runs) {
            if (run.style & RUN_SUPERSCRIPT) ++sup_runs;
            if (run.style & RUN_SUBSCRIPT) ++sub_runs;
        }
        if (!flow.notes.empty()) {
            found_same_file_note = true;
            REPUB_CHECK(has(flow.notes[0].text, "CSS superscript internal-link target"));
        }
        check_flow_invariants(flow);
        free_chapter(chapter);
    }
    REPUB_CHECK(sup_runs > 0);
    REPUB_CHECK(sub_runs > 0);
    REPUB_CHECK(found_same_file_note);
}

/* ---------------------------------------------------------------------- */
/* tables via the tables fixture                                          */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_tables_fixture) {
    FixtureBook fixture("test_tables.epub");
    REPUB_CHECK(fixture.ok);
    size_t table_rows = 0;
    bool big_row = false;
    for (size_t index = 0; index < fixture.book.spine.size(); ++index) {
        repub::repub_chapter* chapter = open_chapter(fixture.book, index);
        const Flow& flow = chapter->flow;
        table_rows += count_type(flow, REPUB_BLOCK_TABLE_ROW);
        for (const FlowBlock& block : flow.blocks) {
            if (block.type != REPUB_BLOCK_TABLE_ROW) continue;
            const std::string text = block_text(flow, block);
            if (has(text, "This is one very long sentence") && has(text, "  ")) big_row = true;
        }
        check_flow_invariants(flow);
        free_chapter(chapter);
    }
    REPUB_CHECK(table_rows > 0);
    REPUB_CHECK(big_row);
}

/* ---------------------------------------------------------------------- */
/* text-decoration cascade and propagation                                */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_text_decorations) {
    FixtureBook fixture("test_text_decorations.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter* chapter = open_chapter(fixture.book, 0);
    const Flow& flow = chapter->flow;

    bool underline = false;
    bool strike = false;
    bool both = false;
    for (const Run& run : flow.runs) {
        const bool u = (run.style & RUN_UNDERLINE) != 0;
        const bool s = (run.style & RUN_STRIKE) != 0;
        if (u) underline = true;
        if (s) strike = true;
        if (u && s) both = true;
    }
    REPUB_CHECK(underline);
    REPUB_CHECK(strike);
    REPUB_CHECK(both); /* <u> inside a line-through parent propagates both */

    /* Parent line-through propagates to child text. */
    const FlowBlock* paragraph = find_block(flow, "Visit");
    REPUB_CHECK(paragraph != nullptr);
    bool paragraph_strike = false;
    for (size_t i = 0; i < paragraph->run_count; ++i) {
        if (flow.runs[paragraph->run_first + i].style & RUN_STRIKE) paragraph_strike = true;
    }
    REPUB_CHECK(paragraph_strike);
    check_flow_invariants(flow);
    free_chapter(chapter);
}

/* ---------------------------------------------------------------------- */
/* images.epub: present and missing resources                             */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_images_fixture) {
    FixtureBook fixture("images.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter* chapter = open_chapter(fixture.book, 0);
    const Flow& flow = chapter->flow;

    REPUB_CHECK_EQ(count_type(flow, REPUB_BLOCK_IMAGE), 4u);
    REPUB_CHECK_EQ(flow.images.size(), 4u);
    size_t decoded_dims = 0;
    bool missing = false;
    for (const FlowImage& image : flow.images) {
        REPUB_CHECK(!image.href.empty());
        REPUB_CHECK(image.href.rfind("data:", 0) != 0);
        if (image.width > 0 && image.height > 0) ++decoded_dims;
        if (has(image.href, "missing.png")) missing = true;
    }
    REPUB_CHECK(decoded_dims >= 3); /* png, jpg, gif */
    REPUB_CHECK(missing);
    check_flow_invariants(flow);
    free_chapter(chapter);
}

/* ---------------------------------------------------------------------- */
/* synthetic: tolerant tokenizer / whitespace / entities                  */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_tokenizer_whitespace_entities) {
    FixtureBook fixture("latin.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter chapter;
    const std::string html =
        "<html><body>"
        "<p>Hello&nbsp; world &amp; <b>bold</b><br/>next</p>"
        "<!-- comment --><![CDATA[raw <text>]]>"
        "<div><p>unclosed <i>italic<p>second &lt;tag&gt; "
        "<img src=\"pic.png\"></p>"
        "<script>var secret=1;</script>"
        "<p>bad utf8: \xFF end</p>"
        "<ul><li>item one<li>item two</ul>"
        "<table><tr><td>a</td><td>b</td></tr></table>"
        "<p class=\"big\" style=\"text-align:center; text-indent:0\">centered</p>"
        "<p class=\"pb\">after break</p>"
        "</body></html>";
    parse_synthetic(fixture.book, html, &chapter);
    const Flow& flow = chapter.flow;

    /* Whitespace collapse and entities. */
    REPUB_CHECK(has(flow.text, "Hello world & bold"));
    /* <br> inside a paragraph is '\n' inside the block text. */
    const FlowBlock* first = find_block(flow, "Hello world");
    REPUB_CHECK(first != nullptr);
    REPUB_CHECK(has(block_text(flow, *first), "bold\nnext"));
    /* comments and CDATA (literal text, no entity decoding). */
    REPUB_CHECK(!has(flow.text, "comment"));
    REPUB_CHECK(has(flow.text, "raw <text>"));
    /* script content dropped. */
    REPUB_CHECK(!has(flow.text, "secret"));
    /* unknown tags keep their content; unknown entities survive as '&'. */
    REPUB_CHECK(has(flow.text, "unclosed italic"));
    REPUB_CHECK(has(flow.text, "second <tag>"));
    /* invalid UTF-8 replaced with U+FFFD. */
    REPUB_CHECK(has(flow.text, "bad utf8: \xEF\xBF\xBD end"));
    /* list bullets. */
    REPUB_CHECK(has(flow.text, "\xE2\x80\xA2 item one"));
    REPUB_CHECK(has(flow.text, "\xE2\x80\xA2 item two"));
    /* table row joins cells with two spaces. */
    const FlowBlock* row = find_block(flow, "a  b", REPUB_BLOCK_TABLE_ROW);
    REPUB_CHECK(row != nullptr);
    /* center inline style and explicit zero indent. */
    const FlowBlock* centered = find_block(flow, "centered");
    REPUB_CHECK(centered != nullptr);
    REPUB_CHECK((centered->flags & FBLOCK_CENTER) != 0);
    REPUB_CHECK_EQ(centered->indent_px, 0);
    REPUB_CHECK((centered->flags & FBLOCK_NO_INDENT) != 0);
    /* page-break-before via CSS class. */
    const FlowBlock* after_break = find_block(flow, "after break");
    REPUB_CHECK(after_break != nullptr);
    REPUB_CHECK_EQ(after_break->page_break_before, 1);
    /* image inside `unclosed` paragraph is a block image. */
    REPUB_CHECK_EQ(flow.images.size(), 1u);
    check_flow_invariants(flow);
    chapter_clear(&chapter);
}

/* ---------------------------------------------------------------------- */
/* synthetic: standalone <br> line_break_after, page_break_after, links    */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_breaks_links_and_list_style) {
    FixtureBook latin("latin.epub");
    REPUB_CHECK(latin.ok);
    repub::repub_chapter chapter;
    const std::string html =
        "<html><body>"
        "<p>before<br/></p>"
        "<p style=\"page-break-after:always\">after</p>"
        "<p><a href=\"chapter2.xhtml\">whole block link</a></p>"
        "<ul><li style=\"list-style-type:none\">no bullet</li>"
        "<li>with bullet</li></ul>"
        "</body></html>";
    parse_synthetic(latin.book, html, &chapter);
    const Flow& flow = chapter.flow;

    const FlowBlock* before = find_block(flow, "before");
    REPUB_CHECK(before != nullptr);
    REPUB_CHECK_EQ(before->line_break_after, 1);
    REPUB_CHECK_EQ(before->len, 6u); /* "before", trailing br newline excluded */
    const FlowBlock* after = find_block(flow, "after");
    REPUB_CHECK(after != nullptr);
    REPUB_CHECK_EQ(after->page_break_after, 1);

    /* Whole-block link: block link_index + FBLOCK_LINK + RUN_LINK runs. */
    const FlowBlock* link_block = find_block(flow, "whole block link");
    REPUB_CHECK(link_block != nullptr);
    REPUB_CHECK(link_block->link_index >= 0);
    REPUB_CHECK((link_block->flags & FBLOCK_LINK) != 0);
    REPUB_CHECK(run_has(flow, RUN_LINK));

    /* Inline list-style:none suppresses the bullet. */
    const FlowBlock* no_bullet = find_block(flow, "no bullet");
    REPUB_CHECK(no_bullet != nullptr);
    REPUB_CHECK(no_bullet->type == REPUB_BLOCK_LIST_ITEM);
    REPUB_CHECK_STR_EQ(block_text(flow, *no_bullet), "no bullet");
    const FlowBlock* with_bullet = find_block(flow, "with bullet");
    REPUB_CHECK(with_bullet != nullptr);
    REPUB_CHECK(block_text(flow, *with_bullet).rfind("\xE2\x80\xA2 ", 0) == 0);

    check_flow_invariants(flow);
    chapter_clear(&chapter);
}

REPUB_TEST(flow_list_style_none_stylesheet) {
    /* test_tables stylesheet has "#TOC li { list-style: none; }". */
    FixtureBook fixture("test_tables.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter chapter;
    const std::string html =
        "<html><body><ul id=\"TOC\"><li>contents item</li></ul>"
        "<ul><li>normal item</li></ul></body></html>";
    parse_synthetic(fixture.book, html, &chapter, "EPUB/text");
    const Flow& flow = chapter.flow;
    const FlowBlock* toc_item = find_block(flow, "contents item");
    REPUB_CHECK(toc_item != nullptr);
    REPUB_CHECK_STR_EQ(block_text(flow, *toc_item), "contents item");
    const FlowBlock* normal = find_block(flow, "normal item");
    REPUB_CHECK(normal != nullptr);
    REPUB_CHECK(block_text(flow, *normal).rfind("\xE2\x80\xA2 ", 0) == 0);
    chapter_clear(&chapter);
}

/* ---------------------------------------------------------------------- */
/* synthetic: same-file icon note marker (U+FFFC)                         */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_note_icon_marker) {
    FixtureBook fixture("latin.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter chapter;
    const std::string html =
        "<html><body><p>Text marker here"
        "<a class=\"noteref\" href=\"#note\"><img src=\"pic.png\"/></a>.</p>"
        "<aside id=\"note\">The note body text.</aside></body></html>";
    parse_synthetic(fixture.book, html, &chapter);
    const Flow& flow = chapter.flow;

    REPUB_CHECK_EQ(flow.notes.size(), 1u);
    REPUB_CHECK_EQ(flow.notes[0].marker_image, 0);
    REPUB_CHECK_STR_EQ(flow.notes[0].text, "The note body text.");
    REPUB_CHECK_EQ(flow.images.size(), 1u);
    REPUB_CHECK_EQ(flow.images[0].href, "OEBPS/pic.png");
    REPUB_CHECK(flow.text.compare(flow.notes[0].marker_offset, 3, "\xEF\xBF\xBC") == 0);
    const FlowLink* link = find_link(flow, "#note");
    REPUB_CHECK(link != nullptr);
    REPUB_CHECK_EQ(link->kind, 2);
    check_flow_invariants(flow);
    chapter_clear(&chapter);
}

/* ---------------------------------------------------------------------- */
/* synthetic: cross-file note from notes.xhtml                            */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_note_cross_file) {
    FixtureBook fixture("latin.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter chapter;
    const std::string html =
        "<html><body><p>See<a href=\"notes.xhtml#note-icon\">icon note</a>.</p>"
        "</body></html>";
    parse_synthetic(fixture.book, html, &chapter);
    const Flow& flow = chapter.flow;

    REPUB_CHECK_EQ(flow.notes.size(), 1u);
    REPUB_CHECK_STR_EQ(flow.notes[0].href, "notes.xhtml#note-icon");
    REPUB_CHECK_STR_EQ(flow.notes[0].text, "Icon footnote text.");
    const FlowLink* link = find_link(flow, "notes.xhtml#note-icon");
    REPUB_CHECK(link != nullptr);
    REPUB_CHECK_EQ(link->kind, 2);
    REPUB_CHECK_EQ(link->note_index, 0);
    chapter_clear(&chapter);
}

/* ---------------------------------------------------------------------- */
/* synthetic: nesting cap and malformed markup never crash                */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_deep_nesting_and_malformed) {
    FixtureBook fixture("latin.epub");
    REPUB_CHECK(fixture.ok);

    std::string deep = "<html><body>";
    for (int i = 0; i < 400; ++i) deep += "<div>";
    deep += "<p>deep content survives</p>";
    for (int i = 0; i < 400; ++i) deep += "</div>";
    deep += "</body></html>";

    repub::repub_chapter chapter;
    parse_synthetic(fixture.book, deep, &chapter);
    REPUB_CHECK(has(chapter.flow.text, "deep content survives"));
    check_flow_invariants(chapter.flow);
    chapter_clear(&chapter);

    /* Mismatched end tags, stray '<', giant attributes and truncation. */
    std::string giant = "<html><body><p data-blob=\"";
    giant.append(200000, 'x');
    giant += "\">after giant attribute</p><div><span>tail text</span></div></body></html>";
    parse_synthetic(fixture.book, giant, &chapter);
    REPUB_CHECK(has(chapter.flow.text, "after giant attribute"));
    REPUB_CHECK(has(chapter.flow.text, "tail text"));
    chapter_clear(&chapter);

    parse_synthetic(fixture.book, "<html><body><p>ok</p></span></div></p>", &chapter);
    REPUB_CHECK(has(chapter.flow.text, "ok"));
    chapter_clear(&chapter);

    parse_synthetic(fixture.book, "<p>truncated <b>tag", &chapter);
    REPUB_CHECK(has(chapter.flow.text, "truncated"));
    chapter_clear(&chapter);
}

/* ---------------------------------------------------------------------- */
/* empty / bounded input                                                  */
/* ---------------------------------------------------------------------- */

REPUB_TEST(flow_empty_and_limits) {
    FixtureBook fixture("latin.epub");
    REPUB_CHECK(fixture.ok);
    repub::repub_chapter chapter;
    parse_synthetic(fixture.book, "", &chapter);
    REPUB_CHECK(chapter.flow.text.empty());
    REPUB_CHECK(chapter.flow.blocks.empty());
    REPUB_CHECK(chapter.flow.notes.empty());
    REPUB_CHECK_EQ(chapter.public_blocks.size(), 0u);
    chapter_clear(&chapter);

    /* max_blocks / max_text_bytes truncate instead of failing. */
    ParseOptions options;
    options.max_text_bytes = 32;
    const std::string html =
        "<html><body><p>aaaaaaaaaaaaaaaaaaaaaaaaaaaa</p><p>bbbbbbbbbbbbbbbb</p></body></html>";
    repub::repub_chapter bounded;
    REPUB_CHECK(parse_chapter(fixture.book, 0, html, "OEBPS", options, &bounded) == REPUB_OK);
    REPUB_CHECK(bounded.flow.text.size() <= 32u);
    chapter_clear(&bounded);

    /* Oversized chapter is rejected with REPUB_ERR_LIMIT. */
    std::string huge(REPUB_MAX_CHAPTER_BYTES + 1, 'a');
    repub::repub_chapter rejected;
    REPUB_CHECK(parse_chapter(fixture.book, 0, huge, "OEBPS", ParseOptions{}, &rejected) ==
                REPUB_ERR_LIMIT);
    chapter_clear(&rejected);
}
