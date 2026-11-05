/*
 * RollingEPUB HTML/XHTML -> Flow chapter parser.
 *
 * Tolerant one-pass tokenizer plus a block/run writer.  It accepts HTML and
 * XHTML (including malformed markup), applies the book's CSS cascade with
 * inheritance, extracts footnotes (same file and cross file), links, anchors,
 * images and ruby, and produces the normalized Flow consumed by the layout
 * engine.
 *
 * Design notes / documented choices
 * ---------------------------------
 *  - The tokenizer is deliberately not the XML parser in container/xml.h:
 *    real books contain unclosed tags, stray '<', uppercase tags and HTML
 *    void elements.  Nesting and output sizes are bounded (see limits below)
 *    so hostile input cannot exhaust memory or the stack.
 *  - Whitespace: runs collapse to one space in normal text; block boundaries
 *    are separated by one '\n' in Flow::text and block offsets/lengths exclude
 *    that separator.  A <br> inside a block appends '\n' to the block text.
 *    <pre>/<code> plus CSS white-space:pre/pre-wrap preserve whitespace
 *    (a leading newline directly after <pre> is dropped, tabs become spaces).
 *    A standalone <br> between paragraph elements bumps the previous block's
 *    margin_after_px to one line (FlowBlock has no line-break field).
 *  - CSS: for every element the style starts from the inherited parent state,
 *    then the book's CssSheet::resolve() overlay, then the inline style.
 *    font-size percentages multiply the inherited scale; bold/italic/
 *    decoration/family/text-align inherit.  Block properties use a reference
 *    body font size of 16 px (CssLength::to_px(16)) exactly like the layout
 *    doc comment: layout scales indent/margins by font_size/16.
 *    text-indent sets FlowBlock::indent_px and FBLOCK_NO_INDENT (explicit
 *    indent wins over the reader's default first-line indent).
 *  - display:none and the HTML hidden attribute remove the whole subtree.
 *    A later inline display:block overrides an earlier display:none.
 *  - Ruby: base text is kept, <rt>/<rp> content is dropped (documented
 *    choice: the plain reading flow never repeats the annotation).
 *  - Tables: one FlowBlock per row (REPUB_BLOCK_TABLE_ROW), cells joined by
 *    two spaces.  Nested tables flatten into the enclosing cell text.
 *  - Footnotes: an internal <a href="...#frag"> only becomes a note when the
 *    target id exists (same chapter id scan, or the referenced file element
 *    is found through book_read_resource_string).  Note text is the target
 *    element text, minus backlink/key/noteref elements, whitespace collapsed.
 *    Icon markers (<img>/<svg> inside the note anchor) append U+FFFC and set
 *    marker_image.  Duplicate hrefs and more than 32 notes are ignored.
 *  - Images resolve against base_dir (OPF-relative), skip data:/external
 *    sources, probe dimensions non-fatally and add a FlowImage.
 *  - Links keep internal hrefs resolved to package-relative paths (with the
 *    fragment preserved) so the book layer can resolve them later; external
 *    hrefs stay verbatim.  Inline links are block-level for v1 (the public
 *    RunFlags have no LINK bit): a block fully inside a link gets
 *    FlowBlock::link_index and FBLOCK_LINK.
 *
 * Provenance
 * ----------
 *  Derived from Read Pico main/book/html_text.c (Apache-2.0,
 *  SPDX-FileCopyrightText 2026 mindreset): the streaming block/writer state
 *  machine, whitespace handling, footnote/anchor/image heuristics and the
 *  cross-file element-text extractor.  Table row joining, list bullets, drop
 *  caps and heading/link handling follow CrossSIGO crossmux
 *  lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp (MIT).  Rewritten in C++17
 *  against the RollingEPUB contracts; no ESP-IDF/Arduino dependencies.
 *
 * SPDX-License-Identifier: MIT
 */
#include "content/flow.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "container/book.h"
#include "content/css.h"
#include "core/base.h"
#include "core/text_util.h"

namespace repub {
namespace {

/* ---------------------------------------------------------------------- */
/* Bounds                                                                  */
/* ---------------------------------------------------------------------- */

constexpr size_t kMaxElementDepth = 256;
constexpr size_t kMaxAttributes = 32;
constexpr size_t kMaxTagBytes = 64u * 1024u;
constexpr size_t kMaxAncestorMatch = 64;
constexpr size_t kMaxNotes = 32;
constexpr size_t kMaxNoteRefs = 64;
constexpr size_t kMaxNoteTextBytes = 4096;
constexpr size_t kMaxImages = 256;
constexpr size_t kMaxLinks = 512;
constexpr size_t kMaxAnchors = 1024;
constexpr size_t kMaxIds = 256;
constexpr size_t kMaxExclusions = 256;
constexpr size_t kMaxClassIdBytes = 512;
constexpr size_t kCrossFileWindow = 64u * 1024u;
constexpr size_t kMaxFamilies = 64;
constexpr float kReferenceFontPx = 16.0f;

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */
/* ---------------------------------------------------------------------- */

inline unsigned char lower_ascii(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

inline bool ascii_space_c(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline bool is_alpha_c(unsigned char c) {
    c = lower_ascii(c);
    return c >= 'a' && c <= 'z';
}

inline bool is_name_char_c(unsigned char c) {
    c = lower_ascii(c);
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == '-' ||
           c == '_' || c == '.';
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower_ascii(static_cast<unsigned char>(a[i])) !=
            lower_ascii(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

int16_t clamp_i16(int32_t value) {
    if (value < -32767) return -32767;
    if (value > 32767) return 32767;
    return static_cast<int16_t>(value);
}

std::string cap_utf8(std::string text, size_t max_bytes) {
    if (text.size() <= max_bytes) return text;
    size_t n = max_bytes;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0u) == 0x80u) --n;
    text.resize(n);
    return text;
}

bool starts_with_bytes(std::string_view text, size_t pos, std::string_view prefix) {
    return pos <= text.size() && text.size() - pos >= prefix.size() &&
           text.compare(pos, prefix.size(), prefix) == 0;
}

bool contains_token_ci(std::string_view text, std::string_view needle) {
    return repub::contains_ci(text, needle);
}

/* ---------------------------------------------------------------------- */
/* Tokenizer                                                               */
/* ---------------------------------------------------------------------- */

struct Attr {
    std::string_view name;
    std::string_view value;
};

struct TagToken {
    std::string name; /* lowercased */
    bool closing = false;
    bool self_closing = false;
    Attr attrs[kMaxAttributes];
    size_t attr_count = 0;
};

enum class ScanResult { Ok, Fail, Truncated };

bool attr_name_is(std::string_view name, const char* literal) {
    return iequals(name, std::string_view(literal));
}

bool find_attr(const TagToken& tag, const char* name, std::string_view* value_out) {
    for (size_t i = 0; i < tag.attr_count; ++i) {
        if (attr_name_is(tag.attrs[i].name, name)) {
            if (value_out) *value_out = tag.attrs[i].value;
            return true;
        }
    }
    return false;
}

bool has_attr(const TagToken& tag, const char* name) {
    return find_attr(tag, name, nullptr);
}

std::string decode_attr(const TagToken& tag, const char* name) {
    std::string_view value;
    if (!find_attr(tag, name, &value) || value.empty()) return std::string();
    return repub::decode_entities(value);
}

/* Parse one tag starting at src[pos] == '<'.  On success pos moves past '>'. */
ScanResult scan_tag(std::string_view src, size_t& pos, TagToken& tag) {
    tag = TagToken{};
    if (pos >= src.size() || src[pos] != '<') return ScanResult::Fail;
    size_t p = pos + 1;
    if (p < src.size() && src[p] == '/') {
        tag.closing = true;
        ++p;
    }
    if (p >= src.size() || !is_alpha_c(static_cast<unsigned char>(src[p])))
        return ScanResult::Fail;
    size_t name_start = p;
    while (p < src.size() && is_name_char_c(static_cast<unsigned char>(src[p]))) ++p;
    if (p == name_start) return ScanResult::Fail;
    tag.name.assign(src.data() + name_start, p - name_start);
    for (char& c : tag.name) c = static_cast<char>(lower_ascii(static_cast<unsigned char>(c)));

    const size_t limit = std::min(src.size(), pos + kMaxTagBytes);
    while (p < limit) {
        while (p < limit && (ascii_space_c(static_cast<unsigned char>(src[p])) || src[p] == '/'))
            ++p;
        if (p >= limit) break;
        if (src[p] == '>') {
            tag.self_closing = p > pos && src[p - 1] == '/';
            pos = p + 1;
            return ScanResult::Ok;
        }
        size_t name_begin = p;
        while (p < limit && is_name_char_c(static_cast<unsigned char>(src[p]))) ++p;
        if (p == name_begin) {
            /* Unknown punctuation inside a tag: skip it and keep going. */
            ++p;
            continue;
        }
        std::string_view attr_name = src.substr(name_begin, p - name_begin);
        while (p < limit && ascii_space_c(static_cast<unsigned char>(src[p]))) ++p;
        std::string_view attr_value;
        if (p < limit && src[p] == '=') {
            ++p;
            while (p < limit && ascii_space_c(static_cast<unsigned char>(src[p]))) ++p;
            if (p < limit && (src[p] == '"' || src[p] == '\'')) {
                const char quote = src[p++];
                const size_t value_begin = p;
                while (p < limit && src[p] != quote) ++p;
                attr_value = src.substr(value_begin, p - value_begin);
                if (p < limit) ++p;
            } else {
                const size_t value_begin = p;
                while (p < limit && !ascii_space_c(static_cast<unsigned char>(src[p])) &&
                       src[p] != '>')
                    ++p;
                attr_value = src.substr(value_begin, p - value_begin);
            }
        }
        if (tag.attr_count < kMaxAttributes) {
            tag.attrs[tag.attr_count].name = attr_name;
            tag.attrs[tag.attr_count].value = attr_value;
            ++tag.attr_count;
        }
    }
    return ScanResult::Truncated;
}

/* Skip one complete element including nested same-name tags. */
size_t skip_element(std::string_view html, size_t pos, size_t limit) {
    if (pos >= limit || html[pos] != '<') return pos + 1;
    TagToken tag;
    size_t scan = pos;
    if (scan_tag(html, scan, tag) != ScanResult::Ok) return limit;
    if (tag.closing || tag.self_closing) return scan;
    size_t depth = 1;
    while (scan < limit) {
        size_t lt = html.find('<', scan);
        if (lt == std::string_view::npos || lt >= limit) break;
        size_t at = lt;
        TagToken inner;
        if (scan_tag(html, at, inner) != ScanResult::Ok) {
            scan = lt + 1;
            continue;
        }
        if (inner.name == tag.name) {
            if (inner.closing) {
                if (--depth == 0) return at;
            } else if (!inner.self_closing) {
                ++depth;
            }
        }
        scan = at;
    }
    return limit;
}

/* ---------------------------------------------------------------------- */
/* Element / style model                                                   */
/* ---------------------------------------------------------------------- */

struct InlineState {
    uint16_t style = 0; /* RunFlags */
    int16_t scale = 100;
    int16_t family = -1;
    TextAlign align = TextAlign::Unset;
    bool preserve = false;
};

enum class BlockKind : uint8_t {
    None = 0,
    Paragraph,
    Heading,
    Pre,
    ListItem,
    Blockquote,
    Center,
    Hr,
    Image,
    TableRow
};

struct Element {
    std::string tag;
    std::string cls;
    std::string id;
    InlineState in;

    BlockKind kind = BlockKind::None;
    uint8_t heading_level = 0;
    int16_t margin_before = 0;
    int16_t margin_after = 0;
    int16_t indent = 0;
    bool indent_defined = false;
    uint8_t drop_cap_lines = 0;
    uint8_t break_before = 0;
    uint8_t break_after = 0;
    bool break_before_pending = false;
    bool break_after_pending = false;
    bool no_bullet = false;

    bool skip = false; /* display:none / hidden / rt annotation */
    bool bullet_pending = false;

    bool is_link = false;
    int32_t link_index = -1;
    bool note_candidate = false;
    int32_t note_ref = -1;

    bool is_exclusion = false; /* backlink/key element (stripped from note text) */
    size_t exclusion_start = 0;

    bool is_id_range = false;
    int32_t id_range = -1;
};

struct NoteRef {
    size_t marker = 0;
    size_t content_start = 0;
    size_t content_end = SIZE_MAX;
    bool same_file = true;
    std::string target_file; /* resolved package-relative file for cross-file */
    std::string target_id;
    std::string href_raw;
    int32_t link_index = -1;
    int32_t marker_image = -1;
};

struct IdRange {
    std::string id;
    size_t start = 0;
    size_t end = SIZE_MAX;
};

struct TextRange {
    size_t start = 0;
    size_t end = 0;
};

/* ---------------------------------------------------------------------- */
/* Parser                                                                  */
/* ---------------------------------------------------------------------- */

class ChapterParser {
   public:
    ChapterParser(const repub_book& book, size_t spine_index, std::string_view html,
                  std::string_view base_dir, const ParseOptions& options)
        : book_(book), spine_index_(spine_index), html_(html), options_(options) {
        if (spine_index_ < book_.spine.size()) chapter_href_ = book_.spine[spine_index_].href;
        effective_base_dir_ = base_dir.empty() ? dirname_of(chapter_href_) : std::string(base_dir);
        max_blocks_ = options_.max_blocks > 0 ? static_cast<size_t>(options_.max_blocks)
                                             : static_cast<size_t>(REPUB_MAX_BLOCKS);
        max_text_bytes_ = options_.max_text_bytes > 0 ? options_.max_text_bytes
                                                      : static_cast<size_t>(REPUB_MAX_TEXT_BYTES);
        if (max_text_bytes_ > REPUB_MAX_TEXT_BYTES) max_text_bytes_ = REPUB_MAX_TEXT_BYTES;
    }

    repub_err_t run(std::string* text_out) {
        if (html_.size() > REPUB_MAX_CHAPTER_BYTES) return REPUB_ERR_LIMIT;
        tokenize();
        finish();
        *text_out = std::move(flow_.text);
        return REPUB_OK;
    }

    Flow& flow() { return flow_; }

   private:
    /* ---- tokenizer loop ------------------------------------------------- */

    void tokenize() {
        const std::string_view src = html_;
        size_t pos = 0;
        if (src.size() >= 3 && static_cast<unsigned char>(src[0]) == 0xEF &&
            static_cast<unsigned char>(src[1]) == 0xBB &&
            static_cast<unsigned char>(src[2]) == 0xBF)
            pos = 3;
        while (pos < src.size() && !stopped_) {
            const size_t lt = src.find('<', pos);
            if (lt == std::string_view::npos) {
                emit_text(src.substr(pos), true);
                break;
            }
            if (lt > pos) emit_text(src.substr(pos, lt - pos), true);
            pos = lt;
            if (stopped_) break;

            if (starts_with_bytes(src, pos, "<!--")) {
                const size_t end = src.find("-->", pos + 4);
                pos = end == std::string_view::npos ? src.size() : end + 3;
                continue;
            }
            if (starts_with_bytes(src, pos, "<![CDATA[")) {
                const size_t body = pos + 9;
                const size_t end = src.find("]]>", body);
                const size_t stop = end == std::string_view::npos ? src.size() : end;
                emit_text(src.substr(body, stop - body), false);
                pos = end == std::string_view::npos ? src.size() : end + 3;
                continue;
            }
            if (pos + 1 < src.size() && src[pos + 1] == '!') {
                const size_t end = src.find('>', pos + 2);
                pos = end == std::string_view::npos ? src.size() : end + 1;
                continue;
            }
            if (pos + 1 < src.size() && src[pos + 1] == '?') {
                const size_t end = src.find("?>", pos + 2);
                pos = end == std::string_view::npos ? src.size() : end + 2;
                continue;
            }
            bool looks_like_tag = false;
            if (pos + 1 < src.size()) {
                const unsigned char c = static_cast<unsigned char>(src[pos + 1]);
                looks_like_tag = is_alpha_c(c);
                if (!looks_like_tag && c == '/' && pos + 2 < src.size())
                    looks_like_tag = is_alpha_c(static_cast<unsigned char>(src[pos + 2]));
            }
            if (looks_like_tag) {
                TagToken tag;
                const ScanResult result = scan_tag(src, pos, tag);
                if (result == ScanResult::Ok) {
                    handle_tag(tag);
                    continue;
                }
                if (result == ScanResult::Truncated) {
                    /* Pathologically long tag: skip to the next '>' within a
                     * generous bound so the following chapter content survives. */
                    const size_t recovery_limit = std::min(src.size(), lt + 4u * 1024u * 1024u);
                    const size_t close = src.find('>', lt + 1);
                    if (close != std::string_view::npos && close < recovery_limit) {
                        pos = close + 1;
                        continue;
                    }
                }
                /* Truncated / unreadable tag: keep the remaining bytes as text. */
                emit_text(src.substr(lt), true);
                pos = src.size();
                break;
            }
            emit_text(src.substr(pos, 1), true);
            ++pos;
        }
    }

    /* ---- text emission -------------------------------------------------- */

    void emit_text(std::string_view chunk, bool decode_entities) {
        if (skip_depth_ > 0 || stopped_) return;
        size_t i = 0;
        while (i < chunk.size() && !stopped_) {
            if (decode_entities && chunk[i] == '&') {
                uint32_t cp = 0;
                const size_t used = repub::decode_entity(chunk.data() + i, chunk.size() - i, &cp);
                if (used > 0) {
                    emit_cp(cp);
                    i += used;
                    continue;
                }
            }
            uint32_t cp = 0;
            const size_t used = repub::utf8_decode(chunk.data() + i, chunk.size() - i, &cp);
            emit_cp(cp);
            i += used > 0 ? used : 1;
        }
    }

    void emit_cp(uint32_t cp) {
        if (stopped_ || skip_depth_ > 0) return;
        const InlineState& st = current_inline();
        if (!st.preserve) {
            if (cp == 0x200B || cp == 0xFEFF || cp == 0x00AD) return;
            if (cp == 0x00A0 || repub::is_space(cp)) {
                if (active_) pending_space_ = true;
                return;
            }
            ensure_block_for_content();
            if (!active_) return;
            sync_run_style();
            if (pending_space_) {
                text_.push_back(' ');
                pending_space_ = false;
            }
            append_cp(cp);
            return;
        }

        /* preserve whitespace: <pre>, <code>, white-space:pre/pre-wrap */
        if (cp == 0x200B || cp == 0xFEFF) return;
        if (pending_pre_newline_) {
            pending_pre_newline_ = false;
            if (cp == '\n') return;
        }
        if (cp == '\n') {
            ensure_block_for_content();
            if (!active_) return;
            sync_run_style();
            text_.push_back('\n');
            check_limits();
            return;
        }
        if (cp == '\t') {
            for (int i = 0; i < 4; ++i) {
                ensure_block_for_content();
                if (!active_) return;
                sync_run_style();
                text_.push_back(' ');
            }
            check_limits();
            return;
        }
        ensure_block_for_content();
        if (!active_) return;
        pending_space_ = false;
        sync_run_style();
        append_cp(cp);
    }

    void append_cp(uint32_t cp) {
        char bytes[4];
        const size_t n = repub::utf8_encode(cp, bytes);
        if (n == 0) return;
        text_.append(bytes, n);
        check_limits();
    }

    void check_limits() {
        if (text_.size() >= max_text_bytes_) stopped_ = true;
        if (flow_.blocks.size() >= max_blocks_) stopped_ = true;
    }

    /* ---- inline style --------------------------------------------------- */

    const InlineState& current_inline() const {
        return stack_.empty() ? root_inline_ : stack_.back().in;
    }

    int16_t map_family(int16_t css_family_id) {
        if (css_family_id < 0) return -1;
        for (const auto& entry : family_map_) {
            if (entry.first == css_family_id) return entry.second;
        }
        const std::string& name = book_.css.family(static_cast<int>(css_family_id));
        if (name.empty()) return -1;
        int16_t result = -1;
        for (size_t i = 0; i < flow_.families.size(); ++i) {
            if (iequals(flow_.families[i], name)) {
                result = static_cast<int16_t>(i);
                break;
            }
        }
        if (result < 0) {
            if (flow_.families.size() >= kMaxFamilies) return -1;
            flow_.families.push_back(name);
            result = static_cast<int16_t>(flow_.families.size() - 1);
        }
        family_map_.push_back({css_family_id, result});
        return result;
    }

    void apply_css_props(Element& el, const CssStyle& style) {
        if (style.defined & CSS_DEF_ALIGN && style.align != TextAlign::Unset)
            el.in.align = style.align;
        if (style.defined & CSS_DEF_FONT_SIZE && style.font_scale_percent > 0) {
            int32_t value = static_cast<int32_t>(el.in.scale) * style.font_scale_percent;
            value = (value + 50) / 100;
            if (value < 25) value = 25;
            if (value > 400) value = 400;
            el.in.scale = static_cast<int16_t>(value);
        }
        if (style.defined & CSS_DEF_WEIGHT) {
            if (style.weight == 2)
                el.in.style |= RUN_BOLD;
            else if (style.weight == 1)
                el.in.style &= static_cast<uint16_t>(~RUN_BOLD);
        }
        if (style.defined & CSS_DEF_ITALIC) {
            if (style.italic == 2)
                el.in.style |= RUN_ITALIC;
            else if (style.italic == 1)
                el.in.style &= static_cast<uint16_t>(~RUN_ITALIC);
        }
        if (style.defined & CSS_DEF_DECORATION) {
            if (style.decoration & CSS_DECOR_UNDERLINE) el.in.style |= RUN_UNDERLINE;
            if (style.decoration & CSS_DECOR_STRIKE) el.in.style |= RUN_STRIKE;
        }
        if (style.defined & CSS_DEF_FAMILY && style.family_id >= 0) {
            const int16_t mapped = map_family(style.family_id);
            if (mapped >= 0) el.in.family = mapped;
        }
        if (style.defined & CSS_DEF_VERTICAL_ALIGN) {
            el.in.style &= static_cast<uint16_t>(~(RUN_SUPERSCRIPT | RUN_SUBSCRIPT));
            if (style.vertical_align == 2)
                el.in.style |= RUN_SUPERSCRIPT;
            else if (style.vertical_align == 3)
                el.in.style |= RUN_SUBSCRIPT;
        }
        if (style.defined & CSS_DEF_WHITE_SPACE) {
            if (style.preserve_whitespace)
                el.in.preserve = true;
            else
                el.in.preserve = false;
        }
    }

    void apply_ua_defaults(Element& el) {
        const std::string& tag = el.tag;
        if (tag == "b" || tag == "strong" || tag == "th") {
            el.in.style |= RUN_BOLD;
        } else if (tag == "i" || tag == "em" || tag == "cite" || tag == "var" ||
                   tag == "dfn" || tag == "address") {
            el.in.style |= RUN_ITALIC;
        } else if (tag == "u" || tag == "ins") {
            el.in.style |= RUN_UNDERLINE;
        } else if (tag == "s" || tag == "strike" || tag == "del") {
            el.in.style |= RUN_STRIKE;
        } else if (tag == "sup") {
            el.in.style &= static_cast<uint16_t>(~(RUN_SUPERSCRIPT | RUN_SUBSCRIPT));
            el.in.style |= RUN_SUPERSCRIPT;
        } else if (tag == "sub") {
            el.in.style &= static_cast<uint16_t>(~(RUN_SUPERSCRIPT | RUN_SUBSCRIPT));
            el.in.style |= RUN_SUBSCRIPT;
        } else if (tag == "code" || tag == "kbd" || tag == "samp" || tag == "tt" ||
                   tag == "pre") {
            el.in.preserve = true;
        } else if (tag == "center") {
            el.in.align = TextAlign::Center;
        } else if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
            el.in.style |= RUN_BOLD;
        }
    }

    static BlockKind kind_for_tag(const std::string& tag, uint8_t* heading_level) {
        *heading_level = 0;
        if (tag == "blockquote") return BlockKind::Blockquote;
        if (tag == "pre") return BlockKind::Pre;
        if (tag == "li") return BlockKind::ListItem;
        if (tag == "center") return BlockKind::Center;
        if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
            *heading_level = static_cast<uint8_t>(tag[1] - '0');
            return BlockKind::Heading;
        }
        static const char* const kParagraphTags[] = {
            "p",     "div",    "section",  "article",  "header",  "footer", "main",
            "aside", "figure", "figcaption", "nav",    "address", "form",   "fieldset",
            "legend", "details", "summary", "dt",      "dd",      "caption", "td",
            "th",    "tr"};
        for (const char* candidate : kParagraphTags) {
            if (tag == candidate) return BlockKind::Paragraph;
        }
        return BlockKind::None;
    }

    static bool is_block_boundary(const std::string& tag) {
        static const char* const kBoundaries[] = {
            "p",          "div",     "section",  "article", "header",    "footer",
            "main",       "aside",   "figure",   "figcaption", "blockquote", "pre",
            "h1",         "h2",      "h3",       "h4",      "h5",        "h6",
            "li",         "dt",      "dd",       "center",  "hr",        "table",
            "thead",      "tbody",   "tfoot",    "tr",      "td",        "th",
            "ol",         "ul",      "dl",       "form",    "fieldset",  "address",
            "nav",        "caption", "details",  "summary", "body",      "html"};
        for (const char* candidate : kBoundaries) {
            if (tag == candidate) return true;
        }
        return false;
    }

    static bool is_void_tag(const std::string& tag) {
        static const char* const kVoid[] = {"br",   "hr",   "img",   "image", "svg:image",
                                            "meta", "link", "input", "wbr",   "area",
                                            "base", "col",  "embed", "param", "source",
                                            "track"};
        for (const char* candidate : kVoid) {
            if (tag == candidate) return true;
        }
        return false;
    }

    static bool is_skipped_subtree(const std::string& tag) {
        static const char* const kSkip[] = {"script", "style", "head", "title", "noscript",
                                            "template"};
        for (const char* candidate : kSkip) {
            if (tag == candidate) return true;
        }
        return false;
    }

    static bool is_ignored_void(const std::string& tag) {
        static const char* const kIgnore[] = {"meta", "link", "base", "input", "wbr",
                                              "area", "embed", "param", "source", "track",
                                              "col"};
        for (const char* candidate : kIgnore) {
            if (tag == candidate) return true;
        }
        return false;
    }

    /* Build the resolved element state.  Returns false when the element (and
     * its subtree) must be skipped because of display:none/hidden. */
    bool build_element(const TagToken& tag, Element& el) {
        el = Element{};
        el.tag = tag.name;
        const std::string cls_value = decode_attr(tag, "class");
        el.cls = cap_utf8(cls_value, kMaxClassIdBytes);
        const std::string id_value = decode_attr(tag, "id");
        el.id = cap_utf8(repub::trim(id_value), kMaxClassIdBytes);
        if (el.id.empty()) {
            const std::string xml_id = decode_attr(tag, "xml:id");
            el.id = cap_utf8(repub::trim(xml_id), kMaxClassIdBytes);
        }
        const std::string style_attr = decode_attr(tag, "style");

        if (has_attr(tag, "hidden")) {
            el.in = current_inline();
            el.skip = true;
            return false;
        }

        /* CSS ancestor chain: outermost first, direct parent last. */
        CssAncestor ancestors[kMaxAncestorMatch];
        size_t ancestor_count = 0;
        const size_t first = stack_.size() > kMaxAncestorMatch ? stack_.size() - kMaxAncestorMatch : 0;
        for (size_t i = first; i < stack_.size(); ++i) {
            ancestors[ancestor_count++] = CssAncestor::make(stack_[i].tag, stack_[i].cls, stack_[i].id);
        }

        const InlineState& parent = current_inline();
        el.in = parent;
        if (!stack_.empty()) {
            el.indent = stack_.back().indent;
            el.indent_defined = stack_.back().indent_defined;
        }
        apply_ua_defaults(el);

        CssStyle css_style;
        if (options_.css_enabled && !book_.css.empty()) {
            css_style = book_.css.resolve(tag.name, el.cls, el.id, ancestors, ancestor_count);
            apply_css_props(el, css_style);
        }
        CssStyle inline_style;
        bool has_inline = false;
        if (!style_attr.empty() && options_.css_enabled) {
            inline_style = CssSheet::parse_inline(style_attr);
            has_inline = true;
            apply_css_props(el, inline_style);
        }

        el.kind = kind_for_tag(tag.name, &el.heading_level);

        /* Length properties: inline layer wins over stylesheet layer. */
        auto effective_length = [&](uint32_t bit, const CssLength CssStyle::*member,
                                    int16_t* out, bool* set) {
            const CssLength* chosen = nullptr;
            if ((css_style.defined & bit) != 0 && (css_style.*member).set)
                chosen = &(css_style.*member);
            if (has_inline && (inline_style.defined & bit) != 0 && (inline_style.*member).set)
                chosen = &(inline_style.*member);
            if (!chosen) return;
            *out = clamp_i16(chosen->to_px(kReferenceFontPx));
            if (set) *set = true;
        };
        effective_length(CSS_DEF_MARGIN_TOP, &CssStyle::margin_top, &el.margin_before, nullptr);
        effective_length(CSS_DEF_MARGIN_BOTTOM, &CssStyle::margin_bottom, &el.margin_after,
                         nullptr);
        effective_length(CSS_DEF_INDENT, &CssStyle::indent, &el.indent, &el.indent_defined);

        /* page-break-before/after (always/left/right/page -> 1). */
        uint8_t break_before = 0;
        uint8_t break_after = 0;
        if ((css_style.defined & CSS_DEF_PAGE_BREAK_BEFORE) != 0)
            break_before = css_style.page_break_before > 0 ? 1 : 0;
        if ((css_style.defined & CSS_DEF_PAGE_BREAK_AFTER) != 0)
            break_after = css_style.page_break_after > 0 ? 1 : 0;
        if (has_inline && (inline_style.defined & CSS_DEF_PAGE_BREAK_BEFORE) != 0)
            break_before = inline_style.page_break_before > 0 ? 1 : 0;
        if (has_inline && (inline_style.defined & CSS_DEF_PAGE_BREAK_AFTER) != 0)
            break_after = inline_style.page_break_after > 0 ? 1 : 0;
        el.break_before = break_before;
        el.break_after = break_after;
        el.break_before_pending = break_before != 0;
        el.break_after_pending = break_after != 0;

        /* display:none (an inline display:block overrides an earlier none). */
        bool display_none = false;
        if ((css_style.defined & CSS_DEF_DISPLAY) != 0) display_none = css_style.display_none != 0;
        if (has_inline && (inline_style.defined & CSS_DEF_DISPLAY) != 0)
            display_none = inline_style.display_none != 0;

        /* list-style(-type): none suppresses the list bullet.  The resolved
         * stylesheet value is authoritative; the inline style attribute is a
         * fallback for very old parsers that fold unknown properties away. */
        if ((css_style.defined & CSS_DEF_LIST_STYLE) != 0)
            el.no_bullet = css_style.list_style_none != 0;
        if (has_inline && (inline_style.defined & CSS_DEF_LIST_STYLE) != 0)
            el.no_bullet = inline_style.list_style_none != 0;
        if (!el.no_bullet && !style_attr.empty() &&
            contains_token_ci(style_attr, "list-style") &&
            contains_token_ci(style_attr, "none"))
            el.no_bullet = true;

        /* Drop caps (initial-letter, then ::first-letter). */
        el.drop_cap_lines = 0;
        if (options_.drop_caps_enabled) {
            uint8_t initial = 0;
            bool initial_set = false;
            if ((css_style.defined & CSS_DEF_INITIAL_LETTER) != 0) {
                initial = css_style.initial_letter;
                initial_set = true;
            }
            if (has_inline && (inline_style.defined & CSS_DEF_INITIAL_LETTER) != 0) {
                initial = inline_style.initial_letter;
                initial_set = true;
            }
            if (initial_set) {
                if (initial > 4) initial = 4;
                el.drop_cap_lines = initial;
            } else if (!book_.css.empty() && book_.css.has_first_letter_rules() &&
                       (el.kind == BlockKind::Paragraph || el.kind == BlockKind::Blockquote)) {
                const CssStyle first = book_.css.resolve_first_letter(
                    tag.name, el.cls, el.id, ancestors, ancestor_count);
                if ((first.defined & CSS_DEF_INITIAL_LETTER) != 0 && first.initial_letter > 0) {
                    uint8_t lines = first.initial_letter > 4 ? 4 : first.initial_letter;
                    el.drop_cap_lines = lines;
                }
            }
        }

        if (display_none) {
            el.skip = true;
            return false;
        }
        return true;
    }

    void push_skip(const TagToken& tag) {
        if (tag.self_closing) return;
        if (stack_.size() >= kMaxElementDepth) {
            ++overflow_;
            return;
        }
        Element el;
        el.tag = tag.name;
        el.skip = true;
        el.in = current_inline();
        stack_.push_back(std::move(el));
        ++skip_depth_;
    }

    /* ---- tag dispatch --------------------------------------------------- */

    void handle_tag(const TagToken& tag) {
        if (tag.name.empty()) return;
        if (tag.closing) {
            close_element(tag.name);
            return;
        }
        if (skip_depth_ > 0) {
            if (!tag.self_closing && !is_void_tag(tag.name)) push_skip(tag);
            return;
        }
        if (overflow_ > 0) {
            if (!tag.self_closing && !is_void_tag(tag.name)) ++overflow_;
            return;
        }
        if (is_skipped_subtree(tag.name)) {
            push_skip(tag);
            return;
        }
        if (is_ignored_void(tag.name)) return;

        if (tag.name == "br") {
            Element el;
            if (!build_element(tag, el)) return;
            line_break();
            return;
        }
        if (tag.name == "hr") {
            Element el;
            if (!build_element(tag, el)) return;
            horizontal_rule();
            return;
        }
        if (tag.name == "img" || tag.name == "image" || tag.name == "svg:image" ||
            tag.name == "object") {
            Element el;
            if (!build_element(tag, el)) return;
            const std::string source = image_source(tag, tag.name == "object");
            if (!source.empty()) handle_image(source);
            return;
        }

        Element el;
        if (!build_element(tag, el)) {
            if (!tag.self_closing) {
                el.in = current_inline();
                push_skip(tag);
            }
            return;
        }

        if (tag.name == "table") {
            if (table_depth_ == 0) close_block();
            ++table_depth_;
            if (!tag.self_closing) {
                if (push_element(std::move(el))) register_marks(tag, stack_.back());
            }
            return;
        }
        if (tag.name == "tr" && table_depth_ == 1) {
            close_block();
            row_mode_ = true;
            row_cell_count_ = 0;
            if (!tag.self_closing) {
                if (push_element(std::move(el))) register_marks(tag, stack_.back());
            } else {
                finish_row();
            }
            return;
        }
        if ((tag.name == "td" || tag.name == "th") && row_mode_) {
            if (row_cell_count_ > 0 && active_) append_cell_separator();
            ++row_cell_count_;
            if (!tag.self_closing) {
                if (push_element(std::move(el))) register_marks(tag, stack_.back());
            }
            return;
        }

        if (is_block_boundary(tag.name) && !row_mode_) close_block();

        if (tag.self_closing) {
            register_marks(tag, el);
            if (tag.name == "a") handle_anchor(tag, el);
            return;
        }

        if (!push_element(std::move(el))) return;
        Element& pushed = stack_.back();
        register_marks(tag, pushed);
        if (tag.name == "a") handle_anchor(tag, pushed);
        if (pushed.kind == BlockKind::ListItem)
            pushed.bullet_pending = !pushed.no_bullet;
        if (tag.name == "rt" || tag.name == "rp") {
            bool in_ruby = false;
            for (size_t i = stack_.size(); i-- > 0;) {
                if (stack_[i].tag == "ruby") {
                    in_ruby = true;
                    break;
                }
            }
            if (in_ruby) {
                pushed.skip = true;
                ++skip_depth_;
            }
        }
        if (tag.name == "pre") pending_pre_newline_ = true;
    }

    bool push_element(Element&& el) {
        if (stack_.size() >= kMaxElementDepth) {
            ++overflow_;
            return false;
        }
        if (el.skip) ++skip_depth_;
        stack_.push_back(std::move(el));
        return true;
    }

    void close_element(const std::string& name) {
        if (overflow_ > 0) {
            --overflow_;
            return;
        }
        size_t index = SIZE_MAX;
        for (size_t i = stack_.size(); i-- > 0;) {
            if (stack_[i].tag == name) {
                index = i;
                break;
            }
        }
        if (index == SIZE_MAX) return; /* mismatched end tag: recover silently */
        while (stack_.size() > index + 1) close_top();
        close_top();
    }

    void close_top() {
        if (stack_.empty()) return;
        Element el = std::move(stack_.back());
        stack_.pop_back();
        if (el.skip) {
            if (skip_depth_ > 0) --skip_depth_;
            pending_pre_newline_ = false;
            return;
        }
        if (el.tag == "tr" && row_mode_ && table_depth_ == 1) finish_row();
        if (el.tag == "table") {
            if (table_depth_ > 0) --table_depth_;
            if (table_depth_ == 0 && row_mode_) finish_row();
            close_block();
            return;
        }
        if (el.is_link && el.note_candidate) {
            NoteRef& ref = note_refs_[el.note_ref];
            if (ref.content_end == SIZE_MAX) ref.content_end = text_.size();
        }
        if (el.is_exclusion && exclusions_.size() < kMaxExclusions)
            exclusions_.push_back(TextRange{el.exclusion_start, text_.size()});
        if (el.is_id_range && static_cast<size_t>(el.id_range) < id_ranges_.size())
            id_ranges_[static_cast<size_t>(el.id_range)].end = text_.size();
        if (is_block_boundary(el.tag) && !row_mode_) close_block();
        if (el.tag == "pre") pending_pre_newline_ = false;
    }

    /* ---- anchors, ids, links -------------------------------------------- */

    uint32_t next_content_offset() const {
        if (active_) return static_cast<uint32_t>(text_.size());
        return static_cast<uint32_t>(text_.size() + (text_.empty() ? 0 : 1));
    }

    void add_anchor(const std::string& id) {
        if (id.empty() || id.size() > 256) return;
        for (const FlowAnchor& existing : flow_.anchors) {
            if (existing.id == id) return;
        }
        if (flow_.anchors.size() >= kMaxAnchors) return;
        FlowAnchor anchor;
        anchor.id = id;
        anchor.offset = next_content_offset();
        flow_.anchors.push_back(std::move(anchor));
    }

    void register_marks(const TagToken& tag, Element& el) {
        std::string id;
        std::string_view value;
        if (find_attr(tag, "id", &value) && !value.empty())
            id = repub::trim(repub::decode_entities(value));
        if (id.empty() && find_attr(tag, "xml:id", &value) && !value.empty())
            id = repub::trim(repub::decode_entities(value));
        if (id.empty() && tag.name == "a" && find_attr(tag, "name", &value) && !value.empty())
            id = repub::trim(repub::decode_entities(value));
        if (!id.empty()) {
            add_anchor(id);
            if (id_ranges_.size() < kMaxIds) {
                IdRange range;
                range.id = id;
                range.start = text_.size();
                range.end = SIZE_MAX;
                el.is_id_range = true;
                el.id_range = static_cast<int32_t>(id_ranges_.size());
                id_ranges_.push_back(std::move(range));
            }
        }

        bool exclusion = false;
        if (!el.cls.empty() &&
            (contains_token_ci(el.cls, "back") || contains_token_ci(el.cls, "noteref") ||
             contains_token_ci(el.cls, "key")))
            exclusion = true;
        if (!el.id.empty() &&
            (repub::starts_with_ci(el.id, "noteref") || repub::starts_with_ci(el.id, "back")))
            exclusion = true;
        if (tag.name == "a" && find_attr(tag, "href", &value) && !value.empty()) {
            std::string href = repub::trim(repub::decode_entities(value));
            if (!href.empty() && !repub::is_external_href(href)) {
                std::string file;
                std::string fragment;
                repub::split_fragment(repub::percent_decode(href), &file, &fragment);
                if (file.empty() ||
                    repub::resolve_path(effective_base_dir_, file) == chapter_href_)
                    exclusion = true;
            }
        }
        if (exclusion) {
            el.is_exclusion = true;
            el.exclusion_start = text_.size();
        }
    }

    void handle_anchor(const TagToken& tag, Element& el) {
        std::string_view value;
        if (!find_attr(tag, "href", &value) || value.empty()) return;
        const std::string href_raw = repub::trim(repub::decode_entities(value));
        if (href_raw.empty()) return;
        if (links_.size() >= kMaxLinks) return;

        const bool external = repub::is_external_href(href_raw);
        std::string display_href = href_raw;
        std::string resolved_file;
        std::string fragment;
        std::string file;
        if (!external) {
            const std::string decoded = repub::percent_decode(href_raw);
            repub::split_fragment(decoded, &file, &fragment);
            resolved_file = file.empty() ? chapter_href_
                                         : repub::resolve_path(effective_base_dir_, file);
            display_href = resolved_file;
            if (!fragment.empty()) {
                if (!display_href.empty()) display_href += '#';
                display_href += fragment;
            }
            if (display_href.empty()) display_href = href_raw;
        }

        FlowLink link;
        link.href = display_href;
        link.kind = external ? 1 : 0;
        link.note_index = -1;
        const int32_t link_index = static_cast<int32_t>(links_.size());
        links_.push_back(std::move(link));
        el.is_link = true;
        el.link_index = link_index;
        /* Mark every run inside the anchor as a link (layout maps this to
         * REPUB_SPAN_LINK); inline children inherit the state. */
        el.in.style |= RUN_LINK;

        if (external || !options_.footnotes_enabled) return;
        if (fragment.empty()) return;
        if (note_refs_.size() >= kMaxNoteRefs) return;

        NoteRef ref;
        ref.marker = next_content_offset();
        ref.content_start = text_.size();
        ref.content_end = SIZE_MAX;
        ref.same_file = file.empty() || resolved_file == chapter_href_;
        ref.target_file = resolved_file;
        ref.target_id = repub::percent_decode(fragment);
        ref.href_raw = href_raw;
        ref.link_index = link_index;
        el.note_candidate = true;
        el.note_ref = static_cast<int32_t>(note_refs_.size());
        note_refs_.push_back(std::move(ref));
    }

    /* ---- images --------------------------------------------------------- */

    std::string image_source(const TagToken& tag, bool object) {
        if (object) {
            std::string_view value;
            if (!find_attr(tag, "data", &value) || value.empty()) return std::string();
            return repub::trim(repub::decode_entities(value));
        }
        static const char* const kAttrs[] = {"data-src",    "data-original", "data-lazy-src",
                                             "src",         "xlink:href",    "href",
                                             "srcset",      "data-srcset"};
        for (const char* name : kAttrs) {
            std::string_view value;
            if (!find_attr(tag, name, &value) || value.empty()) continue;
            std::string decoded = repub::trim(repub::decode_entities(value));
            if (decoded.empty()) continue;
            if (repub::starts_with_ci(decoded, "data:")) continue;
            const bool srcset = std::strstr(name, "srcset") != nullptr;
            if (srcset) {
                size_t end = 0;
                while (end < decoded.size() &&
                       !ascii_space_c(static_cast<unsigned char>(decoded[end])) &&
                       decoded[end] != ',')
                    ++end;
                decoded.resize(end);
            }
            if (!decoded.empty()) return decoded;
        }
        return std::string();
    }

    std::string resolve_image_path(const std::string& source) {
        if (source.empty()) return std::string();
        if (repub::starts_with_ci(source, "data:")) return std::string();
        if (repub::is_external_href(source)) return std::string();
        std::string decoded = repub::percent_decode(source);
        std::string file;
        std::string fragment;
        repub::split_fragment(decoded, &file, &fragment);
        const size_t query = file.find('?');
        if (query != std::string::npos) file.resize(query);
        if (file.empty()) return std::string();
        return repub::resolve_path(effective_base_dir_, file);
    }

    int32_t add_flow_image(const std::string& source) {
        const std::string path = resolve_image_path(source);
        if (path.empty()) return -1;
        if (flow_.images.size() >= kMaxImages) return -1;
        FlowImage image;
        image.href = path;
        image.raw_src = source;
        if (book_.opened) {
            int32_t width = 0;
            int32_t height = 0;
            if (book_image_dims(book_, path, &width, &height) == REPUB_OK) {
                image.width = width;
                image.height = height;
            }
        }
        flow_.images.push_back(std::move(image));
        return static_cast<int32_t>(flow_.images.size() - 1);
    }

    bool add_inline_note_image(const std::string& source) {
        for (size_t i = stack_.size(); i-- > 0;) {
            Element& el = stack_[i];
            if (!el.is_link || !el.note_candidate || el.note_ref < 0) continue;
            NoteRef& ref = note_refs_[static_cast<size_t>(el.note_ref)];
            if (ref.marker_image >= 0) return true;
            const int32_t image_index = add_flow_image(source);
            if (image_index < 0) return true;
            ensure_block_for_content();
            if (!active_) return true;
            sync_run_style();
            const size_t offset = text_.size();
            text_.append("\xEF\xBF\xBC", 3); /* U+FFFC object replacement */
            ref.marker = offset;
            ref.marker_image = image_index;
            check_limits();
            return true;
        }
        return false;
    }

    void handle_image(const std::string& source) {
        if (!options_.images_enabled) return;
        if (add_inline_note_image(source)) return;
        close_block();
        const int32_t image_index = add_flow_image(source);
        if (image_index < 0) return;
        open_block(BlockKind::Image, image_index);
        if (active_) {
            sync_run_style();
            text_.append("\xEF\xBF\xBC", 3);
        }
        close_block();
    }

    /* ---- block writer --------------------------------------------------- */

    void line_break() {
        if (row_mode_) {
            if (active_) append_content_bytes(" ", 1);
            return;
        }
        if (active_) {
            sync_run_style();
            text_.push_back('\n');
            pending_space_ = false;
            check_limits();
            return;
        }
        /* Standalone <br> between block elements: ask layout to break the line
         * instead of adding a paragraph gap. */
        if (!flow_.blocks.empty()) flow_.blocks.back().line_break_after = 1;
    }

    void horizontal_rule() {
        close_block();
        open_block(BlockKind::Hr, -1);
        close_block();
    }

    void append_content_bytes(const char* bytes, size_t length) {
        if (!active_) return;
        sync_run_style();
        text_.append(bytes, length);
        check_limits();
    }

    void append_cell_separator() {
        if (!active_) return;
        sync_run_style();
        text_.append("  ", 2);
        check_limits();
    }

    void finish_row() {
        if (!row_mode_) return;
        row_mode_ = false;
        row_cell_count_ = 0;
        pending_space_ = false;
        close_block();
    }

    const Element* innermost_kind_element() const {
        for (size_t i = stack_.size(); i-- > 0;) {
            if (stack_[i].kind != BlockKind::None) return &stack_[i];
        }
        return nullptr;
    }

    Element* nearest_li() {
        for (size_t i = stack_.size(); i-- > 0;) {
            if (stack_[i].tag == "li") return &stack_[i];
        }
        return nullptr;
    }

    const Element* find_tr() const {
        for (size_t i = stack_.size(); i-- > 0;) {
            if (stack_[i].tag == "tr") return &stack_[i];
        }
        return nullptr;
    }

    void ensure_block_for_content() {
        if (active_ || stopped_) return;
        if (flow_.blocks.size() >= max_blocks_) {
            stopped_ = true;
            return;
        }
        BlockKind kind = BlockKind::Paragraph;
        if (row_mode_) {
            kind = BlockKind::TableRow;
        } else {
            const Element* context = innermost_kind_element();
            if (context) kind = context->kind;
            if (kind == BlockKind::None) kind = BlockKind::Paragraph;
            if (kind == BlockKind::Paragraph && nearest_li() != nullptr)
                kind = BlockKind::ListItem;
        }
        open_block(kind, -1);
    }

    static uint8_t flow_block_type(BlockKind kind) {
        switch (kind) {
            case BlockKind::Heading:
                return REPUB_BLOCK_HEADING;
            case BlockKind::Pre:
                return REPUB_BLOCK_PRE;
            case BlockKind::ListItem:
                return REPUB_BLOCK_LIST_ITEM;
            case BlockKind::Blockquote:
                return REPUB_BLOCK_BLOCKQUOTE;
            case BlockKind::Hr:
                return REPUB_BLOCK_HR;
            case BlockKind::Image:
                return REPUB_BLOCK_IMAGE;
            case BlockKind::TableRow:
                return REPUB_BLOCK_TABLE_ROW;
            case BlockKind::Paragraph:
            case BlockKind::Center:
            case BlockKind::None:
            default:
                return REPUB_BLOCK_PARAGRAPH;
        }
    }

    void open_block(BlockKind kind, int32_t image_index) {
        if (active_ || stopped_) return;
        const Element* context = nullptr;
        if (kind == BlockKind::TableRow) {
            context = find_tr();
        } else if (kind == BlockKind::Image || kind == BlockKind::Hr) {
            context = innermost_kind_element();
        } else {
            context = innermost_kind_element();
        }

        const InlineState run_state = current_inline();
        if (!text_.empty()) text_.push_back('\n');

        FlowBlock block;
        block.offset = static_cast<uint32_t>(text_.size());
        block.type = flow_block_type(kind);
        if (kind == BlockKind::Heading && context) block.heading_level = context->heading_level;
        if (image_index >= 0) block.image_index = image_index;
        block.font_scale_percent = run_state.scale;
        block.family_id = run_state.family;
        block.preserve_whitespace = run_state.preserve;

        if (context) {
            const InlineState& st = context->in;
            if (st.style & RUN_BOLD) block.flags |= FBLOCK_BOLD;
            if (st.style & RUN_ITALIC) block.flags |= FBLOCK_ITALIC;
            if (st.style & RUN_UNDERLINE) block.flags |= FBLOCK_UNDERLINE;
            if (st.style & RUN_STRIKE) block.flags |= FBLOCK_STRIKE;
            if (st.preserve) block.flags |= FBLOCK_CODE;
            if (st.align == TextAlign::Center)
                block.flags |= FBLOCK_CENTER;
            else if (st.align == TextAlign::Right)
                block.flags |= FBLOCK_RIGHT;
            else if (st.align == TextAlign::Justify)
                block.flags |= FBLOCK_JUSTIFY;
            block.margin_before_px = context->margin_before;
            block.margin_after_px = context->margin_after;
            block.indent_px = context->indent;
            if (context->indent_defined) block.flags |= FBLOCK_NO_INDENT;
            block.font_scale_percent = st.scale;
            block.family_id = st.family;
            block.drop_cap_lines = context->drop_cap_lines;
            if (context->break_after) block.page_break_after = 1;
        }
        switch (kind) {
            case BlockKind::Heading:
                block.flags |= FBLOCK_NO_INDENT;
                break;
            case BlockKind::Pre:
                block.flags |= FBLOCK_CODE | FBLOCK_NO_INDENT;
                block.preserve_whitespace = true;
                break;
            case BlockKind::ListItem:
                block.flags |= FBLOCK_NO_INDENT;
                break;
            case BlockKind::Center:
                block.flags |= FBLOCK_CENTER;
                break;
            case BlockKind::Image:
            case BlockKind::Hr:
            case BlockKind::TableRow:
                block.flags |= FBLOCK_NO_INDENT;
                break;
            default:
                break;
        }
        if (!options_.drop_caps_enabled) block.drop_cap_lines = 0;

        /* A block that starts inside an <a href> is a link block; partial
         * inline links are carried by RUN_LINK runs instead. */
        for (size_t i = stack_.size(); i-- > 0;) {
            if (stack_[i].is_link) {
                block.link_index = stack_[i].link_index;
                block.flags |= FBLOCK_LINK;
                break;
            }
        }

        /* page-break-before propagates to the first block that starts inside
         * the element carrying it. */
        for (size_t i = stack_.size(); i-- > 0;) {
            if (stack_[i].break_before_pending) {
                stack_[i].break_before_pending = false;
                block.page_break_before = 1;
                break;
            }
        }

        flow_.blocks.push_back(block);
        active_index_ = flow_.blocks.size() - 1;
        active_ = true;
        pending_space_ = false;
        block_run_first_ = runs_.size();
        run_key_.style = run_state.style;
        run_key_.scale = run_state.scale;
        run_key_.family = run_state.family;
        run_start_ = text_.size();
        run_open_ = true;

        /* list bullet is inline with the first block of the list item */
        Element* list_item = nearest_li();
        if (list_item && list_item->bullet_pending && !row_mode_) {
            list_item->bullet_pending = false;
            text_.append("\xE2\x80\xA2 ", 4); /* "• " */
        }
    }

    void close_block() {
        pending_space_ = false;
        if (!active_) return;
        FlowBlock& block = flow_.blocks[active_index_];
        size_t end = text_.size();
        const bool trailing_break = end > block.offset && text_[end - 1] == '\n';
        while (end > block.offset && text_[end - 1] == '\n') --end;
        if (block.type == REPUB_BLOCK_TABLE_ROW) {
            while (end > block.offset && text_[end - 1] == ' ') --end;
        }
        text_.resize(end);
        close_run();
        block.len = static_cast<uint32_t>(text_.size() - block.offset);
        if (trailing_break) block.line_break_after = 1;
        block.run_first = static_cast<uint16_t>(block_run_first_ > 65535 ? 65535 : block_run_first_);
        const size_t run_count = runs_.size() - block_run_first_;
        block.run_count = static_cast<uint16_t>(run_count > 65535 ? 65535 : run_count);
        active_ = false;
        if (block.len == 0 && block.type != REPUB_BLOCK_IMAGE && block.type != REPUB_BLOCK_HR) {
            if (runs_.size() > block_run_first_) runs_.resize(block_run_first_);
            flow_.blocks.pop_back();
        }
    }

    struct RunKey {
        uint16_t style = 0;
        int16_t scale = 100;
        int16_t family = -1;
    };

    static bool same_run(const Run& run, const RunKey& key) {
        return run.style == key.style && run.scale_percent == static_cast<uint16_t>(key.scale) &&
               run.family_id == key.family;
    }

    void sync_run_style() {
        if (!active_) return;
        const InlineState& st = current_inline();
        if (run_open_ && run_key_.style == st.style && run_key_.scale == st.scale &&
            run_key_.family == st.family)
            return;
        close_run();
        run_key_.style = st.style;
        run_key_.scale = st.scale;
        run_key_.family = st.family;
        run_start_ = text_.size();
        run_open_ = true;
    }

    void close_run() {
        if (!run_open_) return;
        if (text_.size() > run_start_) {
            Run run;
            run.offset = static_cast<uint32_t>(run_start_);
            run.len = static_cast<uint32_t>(text_.size() - run_start_);
            run.style = run_key_.style;
            run.scale_percent = static_cast<uint16_t>(run_key_.scale);
            run.family_id = run_key_.family;
            if (!runs_.empty()) {
                Run& previous = runs_.back();
                if (previous.offset + previous.len == run.offset && same_run(previous, run_key_)) {
                    previous.len += run.len;
                    run_open_ = false;
                    return;
                }
            }
            runs_.push_back(run);
        }
        run_open_ = false;
    }

    /* ---- finish: notes, cross-file extraction --------------------------- */

    void finish() {
        while (!stack_.empty()) close_top();
        close_block();
        for (NoteRef& ref : note_refs_) {
            if (ref.content_end == SIZE_MAX) ref.content_end = text_.size();
        }
        resolve_notes();
    }

    std::string text_range_without_exclusions(size_t start, size_t end) const {
        if (end > text_.size()) end = text_.size();
        if (start >= end) return std::string();
        std::vector<TextRange> ranges;
        for (const TextRange& exclusion : exclusions_) {
            const size_t from = std::max(start, exclusion.start);
            const size_t to = std::min(end, exclusion.end);
            if (from < to) ranges.push_back(TextRange{from, to});
        }
        std::sort(ranges.begin(), ranges.end(),
                  [](const TextRange& a, const TextRange& b) { return a.start < b.start; });
        std::string raw;
        raw.reserve(end - start);
        size_t position = start;
        for (const TextRange& range : ranges) {
            if (range.start > position)
                raw.append(text_.data() + position, range.start - position);
            if (range.end > position) position = range.end;
        }
        if (position < end) raw.append(text_.data() + position, end - position);
        std::string collapsed = repub::collapse_whitespace(raw);
        collapsed = repub::trim(collapsed);
        return cap_utf8(std::move(collapsed), kMaxNoteTextBytes);
    }

    std::string same_file_note_text(const std::string& target_id) const {
        for (const IdRange& range : id_ranges_) {
            if (range.id != target_id) continue;
            const size_t end = range.end == SIZE_MAX ? text_.size() : range.end;
            if (end <= range.start) return std::string();
            if (end - range.start > kMaxNoteTextBytes * 4) return std::string();
            return text_range_without_exclusions(range.start, end);
        }
        return std::string();
    }

    /* Lightweight extractor used for cross-file note targets.  Only elements
     * that are structurally footnotes are accepted; an ordinary cross-chapter
     * link (e.g. "chapter2.xhtml#section") must stay a normal link instead of
     * turning the target paragraph into a note. */
    std::string cross_file_note_text(const std::string& file, const std::string& target_id) const {
        if (file.empty() || target_id.empty() || !book_.opened) return std::string();
        std::string html;
        if (book_read_resource_string(book_, file, &html, REPUB_MAX_CHAPTER_BYTES) != REPUB_OK)
            return std::string();
        return extract_element_text(html, target_id, true);
    }

    static bool is_footnote_element(const TagToken& tag) {
        if (tag.name == "aside") return true;
        std::string_view value;
        if (find_attr(tag, "epub:type", &value) || find_attr(tag, "type", &value)) {
            if (contains_token_ci(repub::decode_entities(value), "footnote") ||
                contains_token_ci(repub::decode_entities(value), "endnote") ||
                contains_token_ci(repub::decode_entities(value), "rearnote"))
                return true;
        }
        if (find_attr(tag, "class", &value)) {
            const std::string cls = repub::decode_entities(value);
            if (contains_token_ci(cls, "footnote") || contains_token_ci(cls, "endnote") ||
                contains_token_ci(cls, "noteref"))
                return true;
        }
        if (find_attr(tag, "role", &value)) {
            if (contains_token_ci(repub::decode_entities(value), "doc-footnote")) return true;
        }
        return false;
    }

    static std::string extract_element_text(std::string_view html, std::string_view id,
                                            bool require_footnote = false) {
        if (html.empty() || id.empty() || id.size() > 64) return std::string();
        size_t content_start = std::string_view::npos;
        size_t content_end = html.size();
        size_t pos = 0;
        while (pos < html.size()) {
            const size_t lt = html.find('<', pos);
            if (lt == std::string_view::npos) break;
            pos = lt;
            TagToken tag;
            size_t at = pos;
            if (scan_tag(html, at, tag) != ScanResult::Ok) {
                /* XML declaration, DOCTYPE, comment, PI or stray '<':
                 * skip one byte and keep scanning for the target element. */
                pos = lt + 1;
                continue;
            }
            if (!tag.closing && !tag.self_closing) {
                std::string_view value;
                bool matches = false;
                if (find_attr(tag, "id", &value) && value.size() == id.size() &&
                    std::memcmp(value.data(), id.data(), id.size()) == 0)
                    matches = true;
                if (!matches && find_attr(tag, "name", &value) && value.size() == id.size() &&
                    std::memcmp(value.data(), id.data(), id.size()) == 0)
                    matches = true;
                if (matches) {
                    if (require_footnote && !is_footnote_element(tag)) return std::string();
                    content_start = at;
                    const size_t limit = std::min(html.size(), at + kCrossFileWindow);
                    size_t depth = 1;
                    size_t scan = at;
                    while (scan < limit) {
                        const size_t next = html.find('<', scan);
                        if (next == std::string_view::npos || next >= limit) break;
                        size_t inner_at = next;
                        TagToken inner;
                        if (scan_tag(html, inner_at, inner) != ScanResult::Ok) {
                            scan = next + 1;
                            continue;
                        }
                        if (inner.name == tag.name) {
                            if (inner.closing) {
                                if (--depth == 0) {
                                    content_end = next;
                                    break;
                                }
                            } else if (!inner.self_closing) {
                                ++depth;
                            }
                        }
                        scan = inner_at;
                    }
                    break;
                }
            }
            pos = at;
        }
        if (content_start == std::string_view::npos) return std::string();

        std::string out;
        bool pending_space = false;
        size_t p = content_start;
        const size_t limit = content_end;
        while (p < limit && out.size() < kMaxNoteTextBytes) {
            if (html[p] == '<') {
                TagToken tag;
                size_t at = p;
                if (scan_tag(html, at, tag) == ScanResult::Ok) {
                    p = at;
                    if (!tag.closing && !tag.self_closing) {
                        const bool skip = tag.name == "a" || tag.name == "script" ||
                                          tag.name == "style" || tag.name == "rt" ||
                                          tag.name == "rp";
                        bool class_skip = false;
                        std::string_view class_value;
                        if (find_attr(tag, "class", &class_value)) {
                            const std::string cls = repub::decode_entities(class_value);
                            class_skip = contains_token_ci(cls, "back") ||
                                         contains_token_ci(cls, "noteref") ||
                                         contains_token_ci(cls, "key");
                        }
                        if (skip || class_skip) {
                            p = skip_element(html, p, limit);
                            pending_space = true;
                        }
                    }
                    continue;
                }
                ++p;
                continue;
            }
            uint32_t cp = 0;
            size_t used = 0;
            if (html[p] == '&') used = repub::decode_entity(html.data() + p, limit - p, &cp);
            if (used == 0) used = repub::utf8_decode(html.data() + p, limit - p, &cp);
            if (used == 0) {
                ++p;
                continue;
            }
            p += used;
            if (cp == 0x200B || cp == 0xFEFF) continue;
            if (cp == 0x00A0 || repub::is_space(cp)) {
                if (!out.empty()) pending_space = true;
                continue;
            }
            char bytes[4];
            const size_t count = repub::utf8_encode(cp, bytes);
            if (pending_space && !out.empty()) out.push_back(' ');
            pending_space = false;
            if (out.size() + count > kMaxNoteTextBytes) break;
            out.append(bytes, count);
        }
        out = repub::trim(out);
        return cap_utf8(std::move(out), kMaxNoteTextBytes);
    }

    void resolve_notes() {
        std::vector<std::string> seen;
        for (const NoteRef& ref : note_refs_) {
            if (notes_.size() >= kMaxNotes) break;
            if (ref.link_index < 0) continue;
            if (ref.content_end <= ref.content_start && ref.marker_image < 0) continue;
            bool duplicate = false;
            for (const std::string& previous : seen) {
                if (previous == ref.href_raw) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            std::string note_text;
            if (ref.same_file)
                note_text = same_file_note_text(ref.target_id);
            else
                note_text = cross_file_note_text(ref.target_file, ref.target_id);
            if (note_text.empty()) continue;
            seen.push_back(ref.href_raw);
            FlowNote note;
            note.marker_offset = static_cast<uint32_t>(ref.marker);
            note.text = std::move(note_text);
            note.href = ref.href_raw;
            note.marker_image = ref.marker_image;
            notes_.push_back(std::move(note));
            FlowLink& link = links_[static_cast<size_t>(ref.link_index)];
            link.kind = 2;
            link.note_index = static_cast<int32_t>(notes_.size() - 1);
        }
    }

    /* ---- state ---------------------------------------------------------- */

    const repub_book& book_;
    size_t spine_index_ = 0;
    std::string_view html_;
    ParseOptions options_{};
    size_t max_blocks_ = REPUB_MAX_BLOCKS;
    size_t max_text_bytes_ = REPUB_MAX_TEXT_BYTES;
    std::string chapter_href_;
    std::string effective_base_dir_;

    Flow flow_;
    std::string& text_ = flow_.text; /* convenience alias */
    std::vector<Run>& runs_ = flow_.runs;
    std::vector<FlowLink>& links_ = flow_.links;
    std::vector<FlowNote>& notes_ = flow_.notes;
    std::vector<Element> stack_;
    std::vector<NoteRef> note_refs_;
    std::vector<IdRange> id_ranges_;
    std::vector<TextRange> exclusions_;
    std::vector<std::pair<int16_t, int16_t>> family_map_;
    InlineState root_inline_;

    bool active_ = false;
    size_t active_index_ = 0;
    size_t block_run_first_ = 0;
    bool run_open_ = false;
    size_t run_start_ = 0;
    RunKey run_key_;
    bool pending_space_ = false;
    bool pending_pre_newline_ = false;
    size_t skip_depth_ = 0;
    size_t overflow_ = 0;
    bool stopped_ = false;
    bool row_mode_ = false;
    size_t table_depth_ = 0;
    size_t row_cell_count_ = 0;
};

}  // namespace

/* ---------------------------------------------------------------------- */
/* Public entry points                                                     */
/* ---------------------------------------------------------------------- */

repub_err_t parse_chapter(const repub_book& book, size_t spine_index, std::string_view html,
                          std::string_view base_dir, const ParseOptions& options,
                          repub_chapter* out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    chapter_clear(out);
    out->book = &book;
    out->spine_index = static_cast<uint32_t>(spine_index);
    if (html.size() > REPUB_MAX_CHAPTER_BYTES) return REPUB_ERR_LIMIT;

    ChapterParser parser(book, spine_index, html, base_dir, options);
    std::string text;
    const repub_err_t err = parser.run(&text);
    if (err != REPUB_OK) return err;
    out->flow = std::move(parser.flow());
    out->flow.text = std::move(text);

    /* Notes store text by value in FlowNote, so nothing points into the text. */
    chapter_finalize(out);
    return REPUB_OK;
}

void chapter_finalize(repub_chapter* chapter) {
    if (!chapter) return;
    Flow& flow = chapter->flow;

    flow.block_offsets.clear();
    flow.block_offsets.reserve(flow.blocks.size());
    for (const FlowBlock& block : flow.blocks) flow.block_offsets.push_back(block.offset);
    std::sort(flow.block_offsets.begin(), flow.block_offsets.end());

    chapter->public_blocks.clear();
    chapter->public_blocks.reserve(flow.blocks.size());
    for (const FlowBlock& block : flow.blocks) {
        repub_block_info_t info{};
        info.text_offset = block.offset;
        info.text_len = block.len;
        info.type = block.type;
        info.heading_level = block.heading_level;
        info.flags = static_cast<uint8_t>(block.flags & 0xFFu);
        info.margin_before_px = block.margin_before_px;
        info.margin_after_px = block.margin_after_px;
        info.indent_px = block.indent_px;
        info.font_scale_percent = block.font_scale_percent;
        info.image_index = block.image_index;
        info.link_index = block.link_index;
        chapter->public_blocks.push_back(info);
    }

    chapter->public_notes.clear();
    chapter->public_notes.reserve(flow.notes.size());
    for (const FlowNote& note : flow.notes) {
        repub_note_info_t info{};
        info.marker_text_offset = note.marker_offset;
        info.text = note.text.c_str();
        info.text_len = static_cast<uint32_t>(note.text.size());
        info.href = note.href.empty() ? nullptr : note.href.c_str();
        info.marker_image_index = note.marker_image;
        chapter->public_notes.push_back(info);
    }

    chapter->public_images.clear();
    chapter->public_images.reserve(flow.images.size());
    for (const FlowImage& image : flow.images) {
        repub_chapter_image_info_t info{};
        info.href = image.href.c_str();
        info.width = image.width;
        info.height = image.height;
        chapter->public_images.push_back(info);
    }

    chapter->public_links.clear();
    chapter->public_links.reserve(flow.links.size());
    for (const FlowLink& link : flow.links) {
        repub_chapter_link_info_t info{};
        info.href = link.href.c_str();
        info.kind = link.kind;
        info.note_index = link.note_index;
        chapter->public_links.push_back(info);
    }

    /* Title: TOC entry for the spine item, else the first heading text. */
    std::string title;
    if (chapter->book)
        title = book_toc_title_for_spine(*chapter->book, chapter->spine_index);
    if (title.empty()) {
        for (const FlowBlock& block : flow.blocks) {
            if (block.type != REPUB_BLOCK_HEADING || block.len == 0) continue;
            if (block.offset > flow.text.size() || block.len > flow.text.size() - block.offset)
                continue;
            std::string heading(flow.text, block.offset, block.len);
            heading = repub::trim(heading);
            if (!heading.empty()) {
                title = cap_utf8(std::move(heading), 200);
                break;
            }
        }
    }
    chapter->title = std::move(title);
}

void chapter_clear(repub_chapter* chapter) {
    if (!chapter) return;
    chapter->book = nullptr;
    chapter->spine_index = 0;
    chapter->title.clear();
    chapter->flow = Flow{};
    chapter->public_blocks.clear();
    chapter->public_notes.clear();
    chapter->public_images.clear();
    chapter->public_links.clear();
}

}  // namespace repub
