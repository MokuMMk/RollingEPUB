/*
 * RollingEPUB CSS cascade engine - implementation.
 *
 * SPDX-License-Identifier: MIT
 *
 * Rewritten for RollingEPUB (namespace repub, CssStyle::defined bitmask,
 * CssLength::to_px, per-chapter family interning) from:
 *
 *  - CrossSIGO/CrossMux lib/Epub/Epub/css/CssParser.cpp + CssStyle.h (MIT):
 *    selector compound/combinator parsing, specificity ordering, property
 *    interpreters, border shorthand/edge-value handling, @font-face and
 *    @media skipping, declaration parsing.
 *  - Read Pico main/book/html_text.c CSS subset (Apache-2.0):
 *    font-family candidate list handling, font-size percentage conversion,
 *    balanced-brace at-rule skipping.
 *
 * Design notes
 * ------------
 *  - Bounded: at most kMaxRules rule entries (a selector group counts one per
 *    selector), selector strings capped, declaration blocks capped, CSS input
 *    capped.  Hostile input is skipped, never trusted.
 *  - No exceptions are thrown deliberately and no IO/allocation through the
 *    platform layer is needed; a sheet is parsed once when a book is opened.
 *  - Not thread-safe: one CssSheet must not be used from several threads.
 *  - Padding has no dedicated fields in CssStyle, so padding-* declarations
 *    are mapped onto the corresponding margin field (documented in css.h).
 *  - Inline styles are parsed without a family intern table (parse_inline() is
 *    static and has no sheet context), so font-family inside a style attribute
 *    is ignored; a stylesheet rule's font-family is interned per sheet.
 */

#include "content/css.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace repub {
namespace {

/* --------------------------------------------------------------------- */
/* Bounds                                                                */
/* --------------------------------------------------------------------- */

constexpr size_t kMaxRules = 512;
constexpr size_t kMaxSelectorLength = 256;
constexpr size_t kMaxCompounds = 8;
constexpr size_t kMaxElementClasses = 4;
constexpr size_t kMaxCssInput = 1u << 20; /* parse at most the first 1 MiB */
constexpr size_t kMaxDeclarationBlock = 16u << 10;
constexpr size_t kMaxSelectorGroup = 16u << 10;
constexpr size_t kMaxFamilies = 64;
constexpr size_t kMaxFamilyName = 96;
constexpr size_t kMaxFontFaces = 32;
constexpr size_t kMaxNumberLength = 32;

constexpr size_t kNpos = static_cast<size_t>(-1);

/* --------------------------------------------------------------------- */
/* Small ASCII helpers                                                   */
/* --------------------------------------------------------------------- */

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* Characters allowed inside tag names, classes, ids and at-rule names. */
bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

std::string_view trim_ws(std::string_view text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && is_space(text[start])) ++start;
    while (end > start && is_space(text[end - 1])) --end;
    return text.substr(start, end - start);
}

std::string_view trim_right_ws(std::string_view text) {
    size_t end = text.size();
    while (end > 0 && is_space(text[end - 1])) --end;
    return text.substr(0, end);
}

/* Case-insensitive equality; `keyword` must already be lowercase. */
bool ieq(std::string_view text, std::string_view keyword) {
    if (text.size() != keyword.size()) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (ascii_lower(text[i]) != keyword[i]) return false;
    }
    return true;
}

/* Case-insensitive equality of two arbitrary strings (both sides lowered). */
bool iequals_ci(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

bool starts_with_ci(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (ascii_lower(text[i]) != ascii_lower(prefix[i])) return false;
    }
    return true;
}

bool ends_with_ci(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    return starts_with_ci(text.substr(text.size() - suffix.size()), suffix);
}

size_t find_ci(std::string_view haystack, std::string_view lowercase_needle) {
    if (lowercase_needle.empty() || haystack.size() < lowercase_needle.size()) return kNpos;
    const size_t limit = haystack.size() - lowercase_needle.size();
    for (size_t i = 0; i <= limit; ++i) {
        bool ok = true;
        for (size_t j = 0; j < lowercase_needle.size(); ++j) {
            if (ascii_lower(haystack[i + j]) != lowercase_needle[j]) {
                ok = false;
                break;
            }
        }
        if (ok) return i;
    }
    return kNpos;
}

/* Stable FNV-1a over lowercased ASCII.  Never returns 0 so 0 can mean absent. */
uint32_t hash_ci(std::string_view text) {
    uint32_t hash = 2166136261u;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(ascii_lower(c));
        hash *= 16777619u;
    }
    return hash ? hash : 1u;
}

/* Walk whitespace-separated tokens (no allocation). */
template <typename Fn>
void for_each_space_token(std::string_view text, Fn&& fn) {
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && is_space(text[i])) ++i;
        if (i >= text.size()) break;
        const size_t start = i;
        while (i < text.size() && !is_space(text[i])) ++i;
        fn(text.substr(start, i - start));
    }
}

/* First ':' / ';' / other delimiter outside quotes, () and []. */
size_t find_top_level(std::string_view text, size_t start, char target) {
    char quote = 0;
    int depth = 0;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != 0) {
            if (c == '\\' && i + 1 < text.size()) {
                ++i;
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(' || c == '[') {
            ++depth;
        } else if ((c == ')' || c == ']') && depth > 0) {
            --depth;
        } else if (c == target && depth == 0) {
            return i;
        }
    }
    return kNpos;
}

/* Split a declaration block on top-level ';' (respects strings and parens). */
template <typename Fn>
void for_each_declaration(std::string_view block, Fn&& fn) {
    size_t start = 0;
    char quote = 0;
    int depth = 0;
    for (size_t i = 0; i < block.size(); ++i) {
        const char c = block[i];
        if (quote != 0) {
            if (c == '\\' && i + 1 < block.size()) {
                ++i;
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(' || c == '[') {
            ++depth;
        } else if ((c == ')' || c == ']') && depth > 0) {
            --depth;
        } else if (c == ';' && depth == 0) {
            fn(block.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < block.size()) fn(block.substr(start));
}

/* Split a selector group on top-level ',' (respects strings and []/()). */
template <typename Fn>
void for_each_selector_part(std::string_view text, Fn&& fn) {
    size_t start = 0;
    char quote = 0;
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != 0) {
            if (c == '\\' && i + 1 < text.size()) {
                ++i;
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(' || c == '[') {
            ++depth;
        } else if ((c == ')' || c == ']') && depth > 0) {
            --depth;
        } else if (c == ',' && depth == 0) {
            fn(text.substr(start, i - start));
            start = i + 1;
        }
    }
    fn(text.substr(start));
}

/* Remove comments from selector text; selectors are tiny, allocation is fine. */
std::string remove_comments(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            i = (i + 1 < text.size()) ? i + 2 : text.size();
            continue;
        }
        out.push_back(text[i++]);
    }
    return out;
}

/* --------------------------------------------------------------------- */
/* Numbers and lengths                                                   */
/* --------------------------------------------------------------------- */

bool parse_number(std::string_view text, float* out) {
    text = trim_ws(text);
    if (text.empty() || text.size() >= kMaxNumberLength) return false;
    char buffer[kMaxNumberLength];
    std::memcpy(buffer, text.data(), text.size());
    buffer[text.size()] = '\0';
    char* end = nullptr;
    const float value = std::strtof(buffer, &end);
    if (end != buffer + text.size() || !std::isfinite(value)) return false;
    *out = value;
    return true;
}

/* Split "1.5em" into number and lowercase unit.  Returns false without digits. */
bool split_length(std::string_view text, float* number, std::string_view* unit) {
    text = trim_ws(text);
    if (text.empty() || text.size() >= kMaxNumberLength) return false;
    size_t i = 0;
    if (text[i] == '+' || text[i] == '-') ++i;
    bool has_digit = false;
    bool has_dot = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (is_digit(c)) {
            has_digit = true;
        } else if (c == '.' && !has_dot) {
            has_dot = true;
        } else {
            break;
        }
    }
    if (!has_digit) return false;
    if (!parse_number(text.substr(0, i), number)) return false;
    *unit = trim_ws(text.substr(i));
    return true;
}

/* Parse px/em/rem/pt/% into a deferred CssLength.  `auto` etc. fail. */
bool parse_length(std::string_view value, CssLength* out) {
    float number = 0.0f;
    std::string_view unit;
    if (!split_length(value, &number, &unit)) return false;
    CssUnit css_unit;
    if (unit.empty() || ieq(unit, "px")) {
        css_unit = CssUnit::Px;
    } else if (ieq(unit, "em")) {
        css_unit = CssUnit::Em;
    } else if (ieq(unit, "rem")) {
        /* css.h has no Rem unit; resolve rem like em (both scale by the em size
         * handed to CssLength::to_px) rather than dropping the declaration. */
        css_unit = CssUnit::Em;
    } else if (ieq(unit, "pt")) {
        css_unit = CssUnit::Pt;
    } else if (unit == "%") {
        css_unit = CssUnit::Percent;
    } else {
        return false;
    }
    out->value = number;
    out->unit = css_unit;
    out->set = true;
    return true;
}

/* font-size -> percentage of the reader body size (0 = inherit, so >= 1). */
bool parse_font_scale(std::string_view value, int16_t* out) {
    value = trim_ws(value);
    struct Keyword {
        const char* name;
        float percent;
    };
    static constexpr Keyword kKeywords[] = {
        {"xx-small", 60.0f}, {"x-small", 75.0f}, {"small", 89.0f},   {"medium", 100.0f},
        {"large", 120.0f},   {"x-large", 150.0f}, {"xx-large", 200.0f}, {"xxx-large", 300.0f},
        {"smaller", 83.0f},  {"larger", 120.0f},
    };
    for (const Keyword& keyword : kKeywords) {
        if (ieq(value, keyword.name)) {
            *out = static_cast<int16_t>(keyword.percent);
            return true;
        }
    }

    float number = 0.0f;
    std::string_view unit;
    if (!split_length(value, &number, &unit)) return false;
    float percent;
    if (unit.empty() || ieq(unit, "px")) {
        percent = number * 100.0f / 16.0f;
    } else if (ieq(unit, "pt")) {
        percent = number * 1.333f * 100.0f / 16.0f;
    } else if (ieq(unit, "em") || ieq(unit, "rem")) {
        percent = number * 100.0f;
    } else if (unit == "%") {
        percent = number;
    } else {
        return false;
    }
    if (!(percent > 0.0f) || !std::isfinite(percent)) return false;
    if (percent < 1.0f) percent = 1.0f;
    if (percent > 1000.0f) percent = 1000.0f;
    *out = static_cast<int16_t>(percent + 0.5f);
    return true;
}

/* --------------------------------------------------------------------- */
/* Property value interpreters                                           */
/* --------------------------------------------------------------------- */

bool interpret_align(std::string_view value, TextAlign* out) {
    value = trim_ws(value);
    if (ieq(value, "left") || ieq(value, "start")) {
        *out = TextAlign::Left;
    } else if (ieq(value, "right") || ieq(value, "end")) {
        *out = TextAlign::Right;
    } else if (ieq(value, "center")) {
        *out = TextAlign::Center;
    } else if (ieq(value, "justify")) {
        *out = TextAlign::Justify;
    } else {
        return false;
    }
    return true;
}

/* 0 unset, 1 normal, 2 italic. */
bool interpret_font_style(std::string_view value, uint8_t* out) {
    value = trim_ws(value);
    if (ieq(value, "normal")) {
        *out = 1;
    } else if (ieq(value, "italic") || ieq(value, "oblique")) {
        *out = 2;
    } else {
        return false;
    }
    return true;
}

/* 0 unset, 1 normal, 2 bold.  CSS 400 = normal, 700..900 = bold. */
bool interpret_font_weight(std::string_view value, uint8_t* out) {
    value = trim_ws(value);
    if (ieq(value, "normal") || ieq(value, "lighter")) {
        *out = 1;
        return true;
    }
    if (ieq(value, "bold") || ieq(value, "bolder")) {
        *out = 2;
        return true;
    }
    float number = 0.0f;
    if (parse_number(value, &number)) {
        if (number < 100.0f) number = 100.0f;
        if (number > 900.0f) number = 900.0f;
        *out = number >= 700.0f ? 2 : 1;
        return true;
    }
    return false;
}

/* text-decoration / text-decoration-line -> CssDecoration bits. */
bool interpret_decoration(std::string_view value, uint8_t* out) {
    uint8_t bits = 0;
    bool explicit_none = false;
    bool recognized = false;
    for_each_space_token(value, [&](std::string_view token) {
        if (ieq(token, "none")) {
            explicit_none = true;
            recognized = true;
        } else if (ieq(token, "underline")) {
            bits |= CSS_DECOR_UNDERLINE;
            recognized = true;
        } else if (ieq(token, "line-through")) {
            bits |= CSS_DECOR_STRIKE;
            recognized = true;
        }
    });
    if (!recognized) return false;
    *out = explicit_none ? 0 : bits;
    return true;
}

/* Forced page break values; returns false for values that do not force one. */
bool interpret_forced_break(std::string_view value, bool* known) {
    value = trim_ws(value);
    if (ieq(value, "always") || ieq(value, "page") || ieq(value, "left") ||
        ieq(value, "right") || ieq(value, "recto") || ieq(value, "verso")) {
        *known = true;
        return true;
    }
    if (ieq(value, "auto") || ieq(value, "avoid") || ieq(value, "inherit") ||
        ieq(value, "initial") || ieq(value, "avoid-page")) {
        *known = true;
        return false;
    }
    *known = false;
    return false;
}

/* background(-color): any non-white, non-transparent color means "shaded". */
bool interpret_shade(std::string_view value, bool* shaded) {
    value = trim_ws(value);
    bool saw_color = false;
    for_each_space_token(value, [&](std::string_view token) {
        if (ieq(token, "none") || ieq(token, "inherit") || ieq(token, "initial") ||
            ieq(token, "unset")) {
            return;
        }
        if (ieq(token, "url(") || starts_with_ci(token, "url(")) return;
        if (ieq(token, "transparent") || ieq(token, "white") || ieq(token, "#fff") ||
            ieq(token, "#ffffff") || ieq(token, "#ffff") || ieq(token, "#ffffffff") ||
            ieq(token, "rgb(255,255,255)")) {
            saw_color = true;
            return;
        }
        if (ieq(token, "rgb(255,") || ieq(token, "rgba(255,")) {
            /* Very rough white rgb() prefix; the tokens themselves are split. */
            saw_color = true;
            return;
        }
        saw_color = true;
    });
    /* White/transparent tokens only ever mark the background as not shaded. */
    bool white_only = true;
    for_each_space_token(value, [&](std::string_view token) {
        if (ieq(token, "transparent") || ieq(token, "white") || ieq(token, "#fff") ||
            ieq(token, "#ffffff") || ieq(token, "#ffff") || ieq(token, "#ffffffff") ||
            ieq(token, "none") || ieq(token, "inherit") || ieq(token, "initial") ||
            ieq(token, "unset") || starts_with_ci(token, "url(")) {
            return;
        }
        if (ieq(token, "255") || ieq(token, "255)") || ieq(token, "rgb(255,") ||
            ieq(token, "rgba(255,")) {
            return;
        }
        white_only = false;
    });
    *shaded = saw_color && !white_only;
    return true;
}

/* Border width keyword/number -> CSS pixels (0 = no line). */
bool interpret_border_width(std::string_view token, uint8_t* out) {
    token = trim_ws(token);
    if (ieq(token, "thin")) {
        *out = 1;
        return true;
    }
    if (ieq(token, "medium")) {
        *out = 2;
        return true;
    }
    if (ieq(token, "thick")) {
        *out = 3;
        return true;
    }
    float number = 0.0f;
    std::string_view unit;
    if (!split_length(token, &number, &unit)) return false;
    float px = number;
    if (unit.empty()) {
        if (number != 0.0f) return false;
    } else if (ieq(unit, "px")) {
        px = number;
    } else if (ieq(unit, "pt")) {
        px = number * 4.0f / 3.0f;
    } else if (ieq(unit, "em") || ieq(unit, "rem")) {
        px = number * 16.0f;
    } else {
        return false;
    }
    if (px <= 0.0f) {
        *out = 0;
    } else {
        const float rounded = px + 0.5f;
        *out = static_cast<uint8_t>(rounded < 1.0f ? 1.0f : (rounded > 16.0f ? 16.0f : rounded));
    }
    return true;
}

/* Border style keyword -> css.h style code (0 none, 1 solid, ...). */
bool interpret_border_style(std::string_view token, uint8_t* out) {
    token = trim_ws(token);
    if (ieq(token, "none") || ieq(token, "hidden")) {
        *out = 0;
    } else if (ieq(token, "solid") || ieq(token, "groove") || ieq(token, "ridge") ||
               ieq(token, "inset") || ieq(token, "outset")) {
        *out = 1;
    } else if (ieq(token, "double")) {
        *out = 2;
    } else if (ieq(token, "dotted")) {
        *out = 3;
    } else if (ieq(token, "dashed")) {
        *out = 4;
    } else {
        return false;
    }
    return true;
}

/* "1px solid #000" -> width and style; colors are ignored (borders draw black). */
CssBorderSide interpret_border_shorthand(std::string_view value) {
    CssBorderSide side;
    bool has_width = false;
    bool has_style = false;
    for_each_space_token(value, [&](std::string_view token) {
        uint8_t width = 0;
        if (interpret_border_width(token, &width)) {
            side.width = width;
            has_width = true;
            return;
        }
        uint8_t style = 0;
        if (interpret_border_style(token, &style)) {
            side.style = style;
            has_style = true;
        }
    });
    /* A shorthand without a style token defaults to border-style: none(css), so
     * a plain border color must not resurrect a base border.  Only a visible
     * style (solid/double/...) gets the CSS medium width default. */
    if (!has_width) side.width = 0;
    if (side.style == 0) {
        side.width = 0;
    } else if (!has_width) {
        side.width = 2; /* CSS medium */
    }
    side.set = true;
    return side;
}

/* Collect up to 4 whitespace-separated edge values (margin/padding shorthand). */
size_t collect_edge_tokens(std::string_view value, std::string_view* out, size_t max_count) {
    size_t count = 0;
    for_each_space_token(value, [&](std::string_view token) {
        if (count < max_count) out[count++] = token;
    });
    return count;
}

/* First family candidate: quotes stripped, trailing comma list ignored. */
std::string_view first_family_name(std::string_view value) {
    value = trim_ws(value);
    if (value.empty()) return {};
    if (value[0] == '"' || value[0] == '\'') {
        const char quote = value[0];
        size_t end = 1;
        while (end < value.size() && value[end] != quote) {
            if (value[end] == '\\' && end + 1 < value.size()) ++end;
            ++end;
        }
        return value.substr(1, end > 1 ? end - 1 : 0);
    }
    size_t end = 0;
    while (end < value.size() && value[end] != ',') ++end;
    return trim_ws(value.substr(0, end));
}

/* Interner function used while parsing declarations (null in inline mode). */
using FamilyInternFn = int (*)(void* ctx, std::string_view family);

/* --------------------------------------------------------------------- */
/* Selector model and matching                                           */
/* --------------------------------------------------------------------- */

struct CompoundSelector {
    uint32_t tag_hash = 0; /* 0 = any element */
    uint32_t id_hash = 0;  /* 0 = no #id constraint */
    uint32_t class_hashes[kMaxElementClasses] = {0, 0, 0, 0};
    uint8_t class_count = 0;

    bool matches(const CssAncestor& ancestor) const {
        if (tag_hash != 0 && tag_hash != ancestor.tag_hash) return false;
        if (id_hash != 0 && id_hash != ancestor.id_hash) return false;
        for (uint8_t i = 0; i < class_count; ++i) {
            bool found = false;
            for (uint8_t j = 0; j < ancestor.class_count; ++j) {
                if (ancestor.class_hashes[j] == class_hashes[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
};

struct Selector {
    CompoundSelector compounds[kMaxCompounds];
    /* child[i] is the combinator between compounds[i] and compounds[i + 1]:
     * true = child ('>'), false = descendant (' '). */
    bool child[kMaxCompounds] = {};
    uint8_t count = 0;
    uint32_t specificity = 0;
    bool first_letter = false;
};

struct Rule {
    Selector selector;
    CssStyle style;
    uint32_t order = 0; /* global source order; later rules win ties */
};

bool match_ancestor_chain(const Selector& selector, const CssAncestor& element,
                          const CssAncestor* ancestors, size_t ancestor_count) {
    if (selector.count == 0) return false;
    if (!selector.compounds[selector.count - 1].matches(element)) return false;

    size_t ancestor_index = ancestor_count; /* one past the direct parent */
    size_t compound_index = selector.count - 1;
    while (compound_index > 0) {
        const bool child = selector.child[compound_index - 1];
        if (child) {
            if (ancestor_index == 0) return false;
            --ancestor_index;
            if (!selector.compounds[compound_index - 1].matches(ancestors[ancestor_index])) {
                return false;
            }
            --compound_index;
        } else {
            bool found = false;
            while (ancestor_index > 0) {
                --ancestor_index;
                if (selector.compounds[compound_index - 1].matches(ancestors[ancestor_index])) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
            --compound_index;
        }
    }
    return true;
}

/* Parse one compound starting at *index.  Accepts tag, '*', #id, .class. */
bool parse_compound(std::string_view text, size_t* index, CompoundSelector* out) {
    *out = CompoundSelector{};
    size_t i = *index;
    bool any = false;
    if (i < text.size() && text[i] == '*') {
        ++i;
        any = true;
    } else if (i < text.size() && is_ident_char(text[i])) {
        const size_t start = i;
        while (i < text.size() && is_ident_char(text[i])) ++i;
        out->tag_hash = hash_ci(text.substr(start, i - start));
        any = true;
    }
    while (i < text.size() && (text[i] == '.' || text[i] == '#')) {
        const char marker = text[i++];
        const size_t start = i;
        while (i < text.size() && is_ident_char(text[i])) ++i;
        if (i == start) return false; /* empty name */
        const std::string_view name = text.substr(start, i - start);
        if (marker == '#') {
            if (out->id_hash != 0) return false; /* only one id */
            out->id_hash = hash_ci(name);
        } else {
            if (out->class_count >= kMaxElementClasses) return false;
            out->class_hashes[out->class_count++] = hash_ci(name);
        }
        any = true;
    }
    *index = i;
    return any;
}

/* Parse a comma-free selector into compounds + combinators. */
bool parse_selector(std::string_view text, Selector* out) {
    text = trim_ws(text);
    if (text.empty() || text.size() > kMaxSelectorLength) return false;

    bool first_letter = false;
    if (ends_with_ci(text, "::first-letter")) {
        text = trim_right_ws(text.substr(0, text.size() - 14));
        first_letter = true;
    } else if (ends_with_ci(text, ":first-letter")) {
        text = trim_right_ws(text.substr(0, text.size() - 13));
        first_letter = true;
    }
    if (text.empty()) return false;

    /* Sibling combinators, attribute selectors and other pseudo-classes are
     * not supported: ignore the whole selector rather than broadening it. */
    if (text.find_first_of("+~[:") != std::string_view::npos) return false;

    size_t i = 0;
    uint8_t count = 0;
    while (true) {
        while (i < text.size() && is_space(text[i])) ++i;
        if (i >= text.size()) return false; /* trailing combinator / empty */
        if (count >= kMaxCompounds) return false;
        CompoundSelector compound;
        if (!parse_compound(text, &i, &compound)) return false;
        out->compounds[count++] = compound;

        const size_t after_compound = i;
        while (i < text.size() && is_space(text[i])) ++i;
        if (i >= text.size()) break;

        bool child = false;
        if (text[i] == '>') {
            child = true;
            ++i;
        } else if (i == after_compound) {
            return false; /* invalid character right after the compound */
        }
        out->child[count - 1] = child;
    }

    out->count = count;
    out->first_letter = first_letter;
    uint32_t specificity = 0;
    for (uint8_t c = 0; c < count; ++c) {
        const CompoundSelector& compound = out->compounds[c];
        if (compound.id_hash != 0) specificity += 100;
        specificity += static_cast<uint32_t>(compound.class_count) * 10u;
        if (compound.tag_hash != 0) specificity += 1;
    }
    out->specificity = specificity;
    return true;
}

/* --------------------------------------------------------------------- */
/* Declaration parsing                                                   */
/* --------------------------------------------------------------------- */

/* Store a parsed length into a style field and mark it defined. */
void set_length_field(CssStyle* style, uint32_t bit, CssLength* field, std::string_view value) {
    CssLength length;
    if (!parse_length(value, &length)) return;
    *field = length;
    style->defined |= bit;
}

std::string normalize_family_name(std::string_view family) {
    family = trim_ws(family);
    if (family.size() >= 2) {
        const char quote = family.front();
        if ((quote == '"' || quote == '\'') && family.back() == quote) {
            family = trim_ws(family.substr(1, family.size() - 2));
        }
    }
    if (family.empty() || family.size() > kMaxFamilyName) return {};
    return std::string(family);
}

void parse_border_declaration(std::string_view name, std::string_view value, CssStyle* style) {
    CssBorderSide* sides[4] = {&style->border_top, &style->border_right, &style->border_bottom,
                              &style->border_left};
    const uint32_t bits[4] = {CSS_DEF_BORDER_TOP, CSS_DEF_BORDER_RIGHT, CSS_DEF_BORDER_BOTTOM,
                              CSS_DEF_BORDER_LEFT};
    static constexpr char kSideNames[4][7] = {"top", "right", "bottom", "left"};

    const auto mark = [&](size_t side) {
        sides[side]->set = true;
        style->defined |= bits[side];
    };

    if (ieq(name, "border")) {
        const CssBorderSide side = interpret_border_shorthand(value);
        for (size_t i = 0; i < 4; ++i) {
            *sides[i] = side;
            mark(i);
        }
        return;
    }

    /* border-width / border-style accept 1..4 edge values: top right bottom left. */
    const bool edge_widths = ieq(name, "border-width");
    if (edge_widths || ieq(name, "border-style")) {
        std::string_view tokens[4];
        const size_t token_count = collect_edge_tokens(value, tokens, 4);
        if (token_count == 0) return;
        const size_t source[4] = {0, token_count >= 2 ? 1u : 0u, token_count >= 3 ? 2u : 0u,
                                  token_count >= 4 ? 3u : (token_count >= 2 ? 1u : 0u)};
        for (size_t side = 0; side < 4; ++side) {
            const std::string_view token = tokens[source[side]];
            if (edge_widths) {
                uint8_t width = 0;
                if (interpret_border_width(token, &width)) {
                    sides[side]->width = width;
                    mark(side);
                }
            } else {
                uint8_t border_style = 0;
                if (interpret_border_style(token, &border_style)) {
                    sides[side]->style = border_style;
                    mark(side);
                }
            }
        }
        return;
    }

    /* border-<side>, border-<side>-width, border-<side>-style.  Colors are
     * intentionally ignored (CssStyle has no border color). */
    if (!starts_with_ci(name, "border-")) return;
    const std::string_view rest = name.substr(7);
    for (size_t side = 0; side < 4; ++side) {
        const std::string_view side_name(kSideNames[side]);
        if (!starts_with_ci(rest, side_name)) continue;
        if (rest.size() != side_name.size() && rest[side_name.size()] != '-') continue;
        const std::string_view suffix = rest.substr(side_name.size());
        if (suffix.empty()) {
            *sides[side] = interpret_border_shorthand(value);
            mark(side);
        } else if (ieq(suffix, "-width")) {
            uint8_t width = 0;
            if (interpret_border_width(trim_ws(value), &width)) {
                sides[side]->width = width;
                mark(side);
            }
        } else if (ieq(suffix, "-style")) {
            uint8_t border_style = 0;
            if (interpret_border_style(trim_ws(value), &border_style)) {
                sides[side]->style = border_style;
                mark(side);
            }
        }
        return;
    }
}

std::string_view strip_important(std::string_view value) {
    value = trim_right_ws(value);
    if (!ends_with_ci(value, "!important")) return value;
    value = trim_right_ws(value.substr(0, value.size() - 10));
    return value;
}

void parse_one_declaration(std::string_view declaration, CssStyle* style,
                           FamilyInternFn intern_fn, void* intern_ctx) {
    const size_t colon = find_top_level(declaration, 0, ':');
    if (colon == kNpos || colon == 0) return;
    const std::string_view name = trim_ws(declaration.substr(0, colon));
    const std::string_view value = strip_important(trim_ws(declaration.substr(colon + 1)));
    if (name.empty() || value.empty()) return;

    if (ieq(name, "white-space")) {
        bool preserve = false;
        if (ieq(value, "pre") || ieq(value, "pre-wrap") || ieq(value, "break-spaces")) {
            preserve = true;
        } else if (!(ieq(value, "normal") || ieq(value, "nowrap") || ieq(value, "pre-line") ||
                     ieq(value, "inherit") || ieq(value, "initial"))) {
            return;
        }
        if (ieq(value, "inherit") || ieq(value, "initial")) return;
        style->preserve_whitespace = preserve ? 1 : 0;
        style->defined |= CSS_DEF_WHITE_SPACE;
    } else if (ieq(name, "text-align")) {
        TextAlign align;
        if (!interpret_align(value, &align)) return;
        style->align = align;
        style->defined |= CSS_DEF_ALIGN;
    } else if (ieq(name, "font-style")) {
        uint8_t italic = 0;
        if (!interpret_font_style(value, &italic)) return;
        style->italic = italic;
        style->defined |= CSS_DEF_ITALIC;
    } else if (ieq(name, "font-weight")) {
        uint8_t weight = 0;
        if (!interpret_font_weight(value, &weight)) return;
        style->weight = weight;
        style->defined |= CSS_DEF_WEIGHT;
    } else if (ieq(name, "text-decoration") || ieq(name, "text-decoration-line")) {
        uint8_t decoration = 0;
        if (!interpret_decoration(value, &decoration)) return;
        style->decoration = decoration;
        style->defined |= CSS_DEF_DECORATION;
    } else if (ieq(name, "text-indent")) {
        set_length_field(style, CSS_DEF_INDENT, &style->indent, value);
    } else if (ieq(name, "margin-top")) {
        set_length_field(style, CSS_DEF_MARGIN_TOP, &style->margin_top, value);
    } else if (ieq(name, "margin-bottom")) {
        set_length_field(style, CSS_DEF_MARGIN_BOTTOM, &style->margin_bottom, value);
    } else if (ieq(name, "margin-left")) {
        set_length_field(style, CSS_DEF_MARGIN_LEFT, &style->margin_left, value);
    } else if (ieq(name, "margin-right")) {
        set_length_field(style, CSS_DEF_MARGIN_RIGHT, &style->margin_right, value);
    } else if (ieq(name, "margin") || ieq(name, "padding")) {
        /* Padding has no dedicated fields, so it maps onto the margins. */
        std::string_view tokens[4];
        const size_t count = collect_edge_tokens(value, tokens, 4);
        if (count == 0) return;
        const size_t source[4] = {0, count >= 2 ? 1u : 0u, count >= 3 ? 2u : 0u,
                                  count >= 4 ? 3u : (count >= 2 ? 1u : 0u)};
        CssLength* fields[4] = {&style->margin_top, &style->margin_right, &style->margin_bottom,
                                &style->margin_left};
        const uint32_t bits[4] = {CSS_DEF_MARGIN_TOP, CSS_DEF_MARGIN_RIGHT,
                                  CSS_DEF_MARGIN_BOTTOM, CSS_DEF_MARGIN_LEFT};
        for (size_t i = 0; i < 4; ++i) {
            CssLength length;
            if (!parse_length(tokens[source[i]], &length)) continue;
            *fields[i] = length;
            style->defined |= bits[i];
        }
    } else if (ieq(name, "font-size")) {
        if (ieq(value, "inherit") || ieq(value, "initial") || ieq(value, "unset")) return;
        int16_t scale = 0;
        if (!parse_font_scale(value, &scale)) return;
        style->font_scale_percent = scale;
        style->defined |= CSS_DEF_FONT_SIZE;
    } else if (ieq(name, "font-family")) {
        if (intern_fn == nullptr) return; /* inline styles cannot intern families */
        if (ieq(value, "inherit") || ieq(value, "initial") || ieq(value, "unset")) return;
        const std::string_view family = first_family_name(value);
        if (family.empty()) return;
        const int id = intern_fn(intern_ctx, family);
        if (id < 0) return;
        style->family_id = static_cast<int16_t>(id);
        style->defined |= CSS_DEF_FAMILY;
    } else if (ieq(name, "display")) {
        if (ieq(value, "inherit") || ieq(value, "initial") || ieq(value, "unset")) return;
        style->display_none = ieq(value, "none") ? 1 : 0;
        style->defined |= CSS_DEF_DISPLAY;
    } else if (ieq(name, "page-break-before") || ieq(name, "break-before")) {
        bool known = false;
        const bool forced = interpret_forced_break(value, &known);
        if (!known) return;
        style->page_break_before = forced ? 1 : 0;
        style->defined |= CSS_DEF_PAGE_BREAK_BEFORE;
    } else if (ieq(name, "page-break-after") || ieq(name, "break-after")) {
        bool known = false;
        const bool forced = interpret_forced_break(value, &known);
        if (!known) return;
        style->page_break_after = forced ? 1 : 0;
        style->defined |= CSS_DEF_PAGE_BREAK_AFTER;
    } else if (ieq(name, "vertical-align")) {
        uint8_t vertical = 0;
        if (ieq(value, "baseline")) {
            vertical = 1;
        } else if (ieq(value, "super")) {
            vertical = 2;
        } else if (ieq(value, "sub")) {
            vertical = 3;
        } else {
            return; /* length values (e.g. "0.5em") and keywords are ignored */
        }
        style->vertical_align = vertical;
        style->defined |= CSS_DEF_VERTICAL_ALIGN;
    } else if (ieq(name, "list-style") || ieq(name, "list-style-type")) {
        /* flow suppresses <li> bullets when list_style_none is set.  `none`
         * wins over a marker type in the shorthand; any other marker keyword
         * explicitly re-enables the bullet so it can override inherited none. */
        bool saw_none = false;
        bool saw_marker = false;
        for_each_space_token(value, [&](std::string_view token) {
            if (ieq(token, "none")) {
                saw_none = true;
            } else if (starts_with_ci(token, "url(") || ieq(token, "inside") ||
                       ieq(token, "outside")) {
                /* image/position parts of the shorthand do not select a type */
            } else if (ieq(token, "inherit") || ieq(token, "initial") ||
                       ieq(token, "unset")) {
                /* ignore */
            } else {
                saw_marker = true;
            }
        });
        if (!saw_none && !saw_marker) return;
        style->list_style_none = saw_none ? 1 : 0;
        style->defined |= CSS_DEF_LIST_STYLE;
    } else if (starts_with_ci(name, "border")) {
        parse_border_declaration(name, value, style);
    } else if (ieq(name, "background-color") || ieq(name, "background")) {
        bool shaded = false;
        if (!interpret_shade(value, &shaded)) return;
        style->shaded = shaded ? 1 : 0;
        style->defined |= CSS_DEF_SHADED;
    } else if (ieq(name, "float")) {
        if (ieq(value, "inherit") || ieq(value, "initial") || ieq(value, "unset")) return;
        if (!(ieq(value, "left") || ieq(value, "right") || ieq(value, "none"))) return;
        style->float_left = ieq(value, "left") ? 1 : 0;
        style->defined |= CSS_DEF_FLOAT_LEFT;
    } else if (ieq(name, "initial-letter")) {
        if (ieq(value, "normal")) {
            style->initial_letter = 0;
            style->defined |= CSS_DEF_INITIAL_LETTER;
            return;
        }
        bool found = false;
        uint8_t lines = 0;
        for_each_space_token(value, [&](std::string_view token) {
            if (found) return;
            float number = 0.0f;
            if (!parse_number(token, &number) || number < 1.0f) return;
            const float rounded = number + 0.5f;
            const float clamped = rounded < 1.0f ? 1.0f : (rounded > 4.0f ? 4.0f : rounded);
            lines = static_cast<uint8_t>(clamped);
            found = true;
        });
        if (!found) return;
        style->initial_letter = lines;
        style->defined |= CSS_DEF_INITIAL_LETTER;
    }
}

void parse_declarations_into(std::string_view block, CssStyle* style, FamilyInternFn intern_fn,
                             void* intern_ctx) {
    for_each_declaration(block, [&](std::string_view declaration) {
        parse_one_declaration(declaration, style, intern_fn, intern_ctx);
    });
}

/* --------------------------------------------------------------------- */
/* CSS block scanning                                                    */
/* --------------------------------------------------------------------- */

/* Skip whitespace and comments starting at *index. */
void skip_ws_comments(std::string_view css, size_t* index) {
    size_t i = *index;
    while (i < css.size()) {
        if (is_space(css[i])) {
            ++i;
        } else if (css[i] == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < css.size() && !(css[i] == '*' && css[i + 1] == '/')) ++i;
            i = (i + 1 < css.size()) ? i + 2 : css.size();
        } else {
            break;
        }
    }
    *index = i;
}

/* Find the matching '}' for the '{' at `open`, handling nested braces/strings. */
size_t find_matching_brace(std::string_view css, size_t open) {
    int depth = 0;
    char quote = 0;
    for (size_t i = open; i < css.size(); ++i) {
        const char c = css[i];
        if (quote != 0) {
            if (c == '\\' && i + 1 < css.size()) {
                ++i;
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < css.size() && !(css[i] == '*' && css[i + 1] == '/')) ++i;
            if (i + 1 >= css.size()) return kNpos;
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return kNpos;
}

/* Find the '{' that opens a rule body, skipping stray '}' and comments. */
size_t find_rule_open(std::string_view css, size_t start, size_t* recovered_start) {
    char quote = 0;
    int bracket_depth = 0;
    *recovered_start = start;
    for (size_t i = start; i < css.size(); ++i) {
        const char c = css[i];
        if (quote != 0) {
            if (c == '\\' && i + 1 < css.size()) {
                ++i;
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < css.size() && !(css[i] == '*' && css[i + 1] == '/')) ++i;
            if (i + 1 >= css.size()) return kNpos;
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '[') {
            ++bracket_depth;
        } else if (c == ']' && bracket_depth > 0) {
            --bracket_depth;
        } else if (c == '{' && bracket_depth == 0) {
            return i;
        } else if (c == '}' && bracket_depth == 0) {
            *recovered_start = i + 1; /* recover from stray closing brace */
        }
    }
    return kNpos;
}

/* Extract the first non-data: url(...) href from an @font-face src value. */
std::string extract_font_src(std::string_view value) {
    size_t search = 0;
    while (true) {
        const size_t url_pos = find_ci(value.substr(search), "url(");
        if (url_pos == kNpos) break;
        const size_t open = search + url_pos + 4;
        const size_t close = value.find(')', open);
        if (close == std::string_view::npos) break;
        std::string_view href = trim_ws(value.substr(open, close - open));
        if (href.size() >= 2) {
            const char quote = href.front();
            if ((quote == '"' || quote == '\'') && href.back() == quote) {
                href = trim_ws(href.substr(1, href.size() - 2));
            }
        }
        if (!href.empty() && !starts_with_ci(href, "data:")) {
            if (href.size() > 512) href = href.substr(0, 512);
            return std::string(href);
        }
        search = close + 1;
    }
    return {};
}

int parse_face_weight(std::string_view value) {
    const std::string_view trimmed = trim_ws(value);
    if (ieq(trimmed, "normal")) return 400;
    if (ieq(trimmed, "bold") || ieq(trimmed, "bolder")) return 700;
    if (ieq(trimmed, "lighter")) return 300;
    float number = 0.0f;
    if (!parse_number(trimmed, &number)) return 400;
    if (number < 100.0f) number = 100.0f;
    if (number > 900.0f) number = 900.0f;
    return static_cast<int>(number + 0.5f);
}

}  // namespace

/* --------------------------------------------------------------------- */
/* CssSheet::Impl                                                        */
/* --------------------------------------------------------------------- */

struct CssSheet::Impl {
    std::vector<Rule> rules;
    std::vector<CssFontFace> faces;
    std::vector<std::string> families;
    uint32_t next_order = 0;
    bool has_first_letter = false;

    static int intern_trampoline(void* ctx, std::string_view family) {
        return static_cast<Impl*>(ctx)->intern_family(family);
    }

    void clear();
    int intern_family(std::string_view family);
    void add_stylesheet(std::string_view css);
    void add_rule_block(std::string_view selector_group, std::string_view declaration_block);
    void parse_font_face(std::string_view declaration_block);
};

void CssSheet::Impl::clear() {
    rules.clear();
    faces.clear();
    families.clear();
    next_order = 0;
    has_first_letter = false;
}

int CssSheet::Impl::intern_family(std::string_view family) {
    const std::string name = normalize_family_name(family);
    if (name.empty()) return -1;
    for (size_t i = 0; i < families.size(); ++i) {
        if (iequals_ci(families[i], name)) return static_cast<int>(i);
    }
    if (families.size() >= kMaxFamilies) return -1;
    families.push_back(name);
    return static_cast<int>(families.size() - 1);
}

void CssSheet::Impl::add_rule_block(std::string_view selector_group,
                                    std::string_view declaration_block) {
    if (rules.size() >= kMaxRules) return;
    CssStyle style;
    parse_declarations_into(declaration_block, &style, &Impl::intern_trampoline, this);
    if (!style.any_defined()) return;

    if (selector_group.size() > kMaxSelectorGroup) return;
    const std::string selector_text = remove_comments(selector_group);
    for_each_selector_part(selector_text, [&](std::string_view part) {
        if (rules.size() >= kMaxRules) return;
        Selector selector;
        if (!parse_selector(part, &selector)) return;
        Rule rule;
        rule.selector = selector;
        rule.style = style;
        rule.order = next_order++;
        if (selector.first_letter) has_first_letter = true;
        rules.push_back(std::move(rule));
    });
}

void CssSheet::Impl::parse_font_face(std::string_view declaration_block) {
    if (faces.size() >= kMaxFontFaces) return;
    CssFontFace face;
    std::string family;
    std::string src;
    int weight = 400;
    int italic = 0;

    for_each_declaration(declaration_block, [&](std::string_view declaration) {
        const size_t colon = find_top_level(declaration, 0, ':');
        if (colon == kNpos || colon == 0) return;
        const std::string_view name = trim_ws(declaration.substr(0, colon));
        const std::string_view value = strip_important(trim_ws(declaration.substr(colon + 1)));
        if (name.empty() || value.empty()) return;
        if (ieq(name, "font-family")) {
            const std::string_view first = first_family_name(value);
            family = normalize_family_name(first);
        } else if (ieq(name, "src")) {
            if (src.empty()) src = extract_font_src(value);
        } else if (ieq(name, "font-weight")) {
            weight = parse_face_weight(value);
        } else if (ieq(name, "font-style")) {
            uint8_t style = 0;
            if (interpret_font_style(value, &style)) italic = style == 2 ? 1 : 0;
        }
    });

    if (family.empty() || src.empty()) return;
    face.family = std::move(family);
    face.src_href = std::move(src);
    face.weight = weight;
    face.italic = italic;
    faces.push_back(std::move(face));
}

void CssSheet::Impl::add_stylesheet(std::string_view css) {
    if (css.size() > kMaxCssInput) css = css.substr(0, kMaxCssInput);
    size_t i = 0;
    const size_t size = css.size();
    while (i < size) {
        skip_ws_comments(css, &i);
        if (i >= size) break;

        if (css[i] == '@') {
            const size_t name_start = i + 1;
            size_t j = name_start;
            while (j < size && is_ident_char(css[j])) ++j;
            const std::string_view at_name = css.substr(name_start, j - name_start);

            size_t terminator = j;
            skip_ws_comments(css, &terminator);
            while (terminator < size && css[terminator] != ';' && css[terminator] != '{') {
                if (css[terminator] == '/' && terminator + 1 < size &&
                    css[terminator + 1] == '*') {
                    skip_ws_comments(css, &terminator);
                    continue;
                }
                if (css[terminator] == '"' || css[terminator] == '\'') {
                    const char quote = css[terminator++];
                    while (terminator < size && css[terminator] != quote) {
                        if (css[terminator] == '\\' && terminator + 1 < size) ++terminator;
                        ++terminator;
                    }
                    if (terminator < size) ++terminator;
                    continue;
                }
                ++terminator;
            }
            if (terminator >= size) break; /* unterminated at-rule */
            if (css[terminator] == ';') {
                i = terminator + 1; /* @import/@charset/@namespace: ignore */
                continue;
            }
            const size_t close = find_matching_brace(css, terminator);
            if (close == kNpos) break; /* malformed tail: stop parsing */
            /* @media/@supports/@keyframes/... bodies are skipped entirely. */
            if (ieq(at_name, "font-face")) {
                parse_font_face(css.substr(terminator + 1, close - terminator - 1));
            }
            i = close + 1;
            continue;
        }

        size_t recovered_start = i;
        const size_t open = find_rule_open(css, i, &recovered_start);
        if (open == kNpos) break;
        const size_t close = find_matching_brace(css, open);
        if (close == kNpos) break; /* malformed: ignore the rest */
        if (close > open + 1 && close - open - 1 <= kMaxDeclarationBlock) {
            add_rule_block(css.substr(recovered_start, open - recovered_start),
                           css.substr(open + 1, close - open - 1));
        }
        i = close + 1;
    }
}

/* --------------------------------------------------------------------- */
/* CssLength                                                             */
/* --------------------------------------------------------------------- */

int32_t CssLength::to_px(float em_px, float container_px) const {
    if (!set) return 0;
    float px = value;
    switch (unit) {
        case CssUnit::Px:
            px = value;
            break;
        case CssUnit::Em: /* rem is parsed as em; see parse_length() */
            px = value * em_px;
            break;
        case CssUnit::Percent:
            px = value * container_px / 100.0f;
            break;
        case CssUnit::Pt:
            px = value * 1.333f;
            break;
    }
    if (!std::isfinite(px)) return 0;
    if (px > 1000000.0f) px = 1000000.0f;
    if (px < -1000000.0f) px = -1000000.0f;
    return static_cast<int32_t>(std::lround(px));
}

/* --------------------------------------------------------------------- */
/* CssAncestor                                                           */
/* --------------------------------------------------------------------- */

CssAncestor CssAncestor::make(std::string_view tag, std::string_view class_attr,
                              std::string_view id_attr) {
    CssAncestor ancestor;
    ancestor.tag_hash = tag.empty() ? 0u : hash_ci(tag);
    ancestor.id_hash = id_attr.empty() ? 0u : hash_ci(id_attr);
    for_each_space_token(class_attr, [&](std::string_view cls) {
        if (ancestor.class_count < kMaxElementClasses) {
            ancestor.class_hashes[ancestor.class_count++] = hash_ci(cls);
        }
    });
    return ancestor;
}

/* --------------------------------------------------------------------- */
/* CssStyle                                                              */
/* --------------------------------------------------------------------- */

void CssStyle::apply(const CssStyle& overlay) {
    const uint32_t bits = overlay.defined;
    if (bits & CSS_DEF_ALIGN) {
        align = overlay.align;
        defined |= CSS_DEF_ALIGN;
    }
    if (bits & CSS_DEF_INDENT) {
        indent = overlay.indent;
        defined |= CSS_DEF_INDENT;
    }
    if (bits & CSS_DEF_MARGIN_TOP) {
        margin_top = overlay.margin_top;
        defined |= CSS_DEF_MARGIN_TOP;
    }
    if (bits & CSS_DEF_MARGIN_BOTTOM) {
        margin_bottom = overlay.margin_bottom;
        defined |= CSS_DEF_MARGIN_BOTTOM;
    }
    if (bits & CSS_DEF_MARGIN_LEFT) {
        margin_left = overlay.margin_left;
        defined |= CSS_DEF_MARGIN_LEFT;
    }
    if (bits & CSS_DEF_MARGIN_RIGHT) {
        margin_right = overlay.margin_right;
        defined |= CSS_DEF_MARGIN_RIGHT;
    }
    if (bits & CSS_DEF_FONT_SIZE) {
        font_scale_percent = overlay.font_scale_percent;
        defined |= CSS_DEF_FONT_SIZE;
    }
    if (bits & CSS_DEF_FAMILY) {
        family_id = overlay.family_id;
        defined |= CSS_DEF_FAMILY;
    }
    if (bits & CSS_DEF_WEIGHT) {
        weight = overlay.weight;
        defined |= CSS_DEF_WEIGHT;
    }
    if (bits & CSS_DEF_ITALIC) {
        italic = overlay.italic;
        defined |= CSS_DEF_ITALIC;
    }
    if (bits & CSS_DEF_DECORATION) {
        decoration = overlay.decoration;
        defined |= CSS_DEF_DECORATION;
    }
    if (bits & CSS_DEF_DISPLAY) {
        display_none = overlay.display_none;
        defined |= CSS_DEF_DISPLAY;
    }
    if (bits & CSS_DEF_PAGE_BREAK_BEFORE) {
        page_break_before = overlay.page_break_before;
        defined |= CSS_DEF_PAGE_BREAK_BEFORE;
    }
    if (bits & CSS_DEF_PAGE_BREAK_AFTER) {
        page_break_after = overlay.page_break_after;
        defined |= CSS_DEF_PAGE_BREAK_AFTER;
    }
    if (bits & CSS_DEF_VERTICAL_ALIGN) {
        vertical_align = overlay.vertical_align;
        defined |= CSS_DEF_VERTICAL_ALIGN;
    }
    if (bits & CSS_DEF_WHITE_SPACE) {
        preserve_whitespace = overlay.preserve_whitespace;
        defined |= CSS_DEF_WHITE_SPACE;
    }
    if (bits & CSS_DEF_SHADED) {
        shaded = overlay.shaded;
        defined |= CSS_DEF_SHADED;
    }
    if (bits & CSS_DEF_INITIAL_LETTER) {
        initial_letter = overlay.initial_letter;
        defined |= CSS_DEF_INITIAL_LETTER;
    }
    if (bits & CSS_DEF_FLOAT_LEFT) {
        float_left = overlay.float_left;
        defined |= CSS_DEF_FLOAT_LEFT;
    }
    if (bits & CSS_DEF_LIST_STYLE) {
        list_style_none = overlay.list_style_none;
        defined |= CSS_DEF_LIST_STYLE;
    }

    /* Border sides carry a width and a style.  A side is overwritten by the
     * components the overlay actually spells out so that, for example, a later
     * `border-top-width` does not erase an earlier `border-top-style`.  A side
     * that arrives fully zeroed (e.g. `border: none`) clears the base side. */
    const auto apply_side = [&](CssBorderSide& base, const CssBorderSide& overlay_side,
                                uint32_t bit) {
        if ((bits & bit) == 0) return;
        if (overlay_side.width == 0 && overlay_side.style == 0) {
            base = CssBorderSide{};
        } else {
            if (overlay_side.width > 0) base.width = overlay_side.width;
            if (overlay_side.style != 0) base.style = overlay_side.style;
            base.set = true;
        }
        defined |= bit;
    };
    apply_side(border_top, overlay.border_top, CSS_DEF_BORDER_TOP);
    apply_side(border_right, overlay.border_right, CSS_DEF_BORDER_RIGHT);
    apply_side(border_bottom, overlay.border_bottom, CSS_DEF_BORDER_BOTTOM);
    apply_side(border_left, overlay.border_left, CSS_DEF_BORDER_LEFT);
}

/* --------------------------------------------------------------------- */
/* CssSheet                                                              */
/* --------------------------------------------------------------------- */

CssSheet::CssSheet() : impl_(new Impl()) {}

CssSheet::~CssSheet() { delete impl_; }

void CssSheet::clear() {
    if (impl_ != nullptr) impl_->clear();
}

void CssSheet::add_stylesheet(std::string_view css) {
    if (impl_ != nullptr) impl_->add_stylesheet(css);
}

bool CssSheet::empty() const { return impl_ == nullptr || impl_->rules.empty(); }

size_t CssSheet::rule_count() const { return impl_ == nullptr ? 0 : impl_->rules.size(); }

bool CssSheet::has_first_letter_rules() const {
    return impl_ != nullptr && impl_->has_first_letter;
}

CssStyle CssSheet::resolve(std::string_view tag, std::string_view class_attr,
                           std::string_view id_attr, const CssAncestor* ancestors,
                           size_t ancestor_count) const {
    CssStyle result;
    if (impl_ == nullptr || impl_->rules.empty()) return result;
    if (ancestors == nullptr) ancestor_count = 0;

    const CssAncestor element = CssAncestor::make(tag, class_attr, id_attr);
    std::vector<const Rule*> matches;
    matches.reserve(8);
    for (const Rule& rule : impl_->rules) {
        if (rule.selector.first_letter) continue;
        if (!match_ancestor_chain(rule.selector, element, ancestors, ancestor_count)) continue;
        matches.push_back(&rule);
    }
    if (matches.empty()) return result;

    std::sort(matches.begin(), matches.end(), [](const Rule* a, const Rule* b) {
        if (a->selector.specificity != b->selector.specificity) {
            return a->selector.specificity < b->selector.specificity;
        }
        return a->order < b->order; /* later rule wins on equal specificity */
    });
    for (const Rule* rule : matches) result.apply(rule->style);
    return result;
}

CssStyle CssSheet::resolve_first_letter(std::string_view tag, std::string_view class_attr,
                                        std::string_view id_attr, const CssAncestor* ancestors,
                                        size_t ancestor_count) const {
    CssStyle result;
    if (impl_ == nullptr || !impl_->has_first_letter) return result;
    if (ancestors == nullptr) ancestor_count = 0;

    const CssAncestor element = CssAncestor::make(tag, class_attr, id_attr);
    std::vector<const Rule*> matches;
    matches.reserve(4);
    for (const Rule& rule : impl_->rules) {
        if (!rule.selector.first_letter) continue;
        if (!match_ancestor_chain(rule.selector, element, ancestors, ancestor_count)) continue;
        matches.push_back(&rule);
    }
    if (matches.empty()) return result;

    std::sort(matches.begin(), matches.end(), [](const Rule* a, const Rule* b) {
        if (a->selector.specificity != b->selector.specificity) {
            return a->selector.specificity < b->selector.specificity;
        }
        return a->order < b->order;
    });
    for (const Rule* rule : matches) result.apply(rule->style);
    return result;
}

CssStyle CssSheet::parse_inline(std::string_view style_value) {
    CssStyle style;
    parse_declarations_into(style_value, &style, nullptr, nullptr);
    return style;
}

const std::vector<CssFontFace>& CssSheet::font_faces() const {
    static const std::vector<CssFontFace> kEmpty;
    return impl_ == nullptr ? kEmpty : impl_->faces;
}

int CssSheet::intern_family(std::string_view family) {
    return impl_ == nullptr ? -1 : impl_->intern_family(family);
}

const std::string& CssSheet::family(int id) const {
    static const std::string kEmpty;
    if (impl_ == nullptr || id < 0 || static_cast<size_t>(id) >= impl_->families.size()) {
        return kEmpty;
    }
    return impl_->families[static_cast<size_t>(id)];
}

}  // namespace repub
