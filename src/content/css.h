/*
 * RollingEPUB CSS cascade engine.
 *
 * Parses the subset of CSS that matters for reflowable book reading and
 * resolves an element style by specificity.  Merged from the CrossMux
 * CssParser (MIT) and the Read Pico html_text CSS subset (Apache-2.0).
 *
 * Supported: element/id/class compounds, descendant and child combinators,
 * selector groups, inline style attributes, @media/@supports skipping,
 * @font-face collection, font-size/family/weight/style, text-align,
 * text-indent, margins/paddings, text-decoration, display:none,
 * page-break-before/after, vertical-align, white-space, borders, shading,
 * initial-letter/::first-letter.
 *
 * Not supported: sibling combinators, attribute selectors, pseudo-classes
 * other than ::first-letter, CSS variables, external @import.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_CONTENT_CSS_H
#define REPUB_SRC_CONTENT_CSS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/base.h"

namespace repub {

enum class TextAlign : uint8_t { Unset = 0, Left, Center, Right, Justify };
enum class CssUnit : uint8_t { Px = 0, Em = 1, Percent = 2, Pt = 3 };
enum class VerticalAlign : uint8_t { Unset = 0, Baseline, Super, Sub };

struct CssLength {
    float value = 0.0f;
    CssUnit unit = CssUnit::Px;
    bool set = false;

    int32_t to_px(float em_px, float container_px = 0.0f) const;
};

struct CssBorderSide {
    uint8_t width = 0;
    uint8_t style = 0; /* 0 none, 1 solid, 2 double, 3 dotted, 4 dashed */
    bool set = false;
    bool visible() const { return set && width > 0 && style != 0; }
};

/* Property bits for CssStyle::defined. */
enum CssDefined : uint32_t {
    CSS_DEF_ALIGN = 1u << 0,
    CSS_DEF_INDENT = 1u << 1,
    CSS_DEF_MARGIN_TOP = 1u << 2,
    CSS_DEF_MARGIN_BOTTOM = 1u << 3,
    CSS_DEF_MARGIN_LEFT = 1u << 4,
    CSS_DEF_MARGIN_RIGHT = 1u << 5,
    CSS_DEF_FONT_SIZE = 1u << 6,
    CSS_DEF_FAMILY = 1u << 7,
    CSS_DEF_WEIGHT = 1u << 8,
    CSS_DEF_ITALIC = 1u << 9,
    CSS_DEF_DECORATION = 1u << 10,
    CSS_DEF_DISPLAY = 1u << 11,
    CSS_DEF_PAGE_BREAK_BEFORE = 1u << 12,
    CSS_DEF_PAGE_BREAK_AFTER = 1u << 13,
    CSS_DEF_VERTICAL_ALIGN = 1u << 14,
    CSS_DEF_WHITE_SPACE = 1u << 15,
    CSS_DEF_BORDER_TOP = 1u << 16,
    CSS_DEF_BORDER_RIGHT = 1u << 17,
    CSS_DEF_BORDER_BOTTOM = 1u << 18,
    CSS_DEF_BORDER_LEFT = 1u << 19,
    CSS_DEF_SHADED = 1u << 20,
    CSS_DEF_INITIAL_LETTER = 1u << 21,
    CSS_DEF_FLOAT_LEFT = 1u << 22
};

enum CssDecoration : uint8_t {
    CSS_DECOR_UNDERLINE = 1u << 0,
    CSS_DECOR_STRIKE = 1u << 1
};

/* A resolved element style.  Unset fields fall back to inherited/default. */
struct CssStyle {
    uint32_t defined = 0;
    TextAlign align = TextAlign::Unset;
    CssLength indent;
    CssLength margin_top;
    CssLength margin_bottom;
    CssLength margin_left;
    CssLength margin_right;
    int16_t font_scale_percent = 0; /* relative to reader body size, 0 = inherit */
    int16_t family_id = -1;         /* index into the chapter's family table */
    uint8_t weight = 0;             /* 0 unset, 1 normal, 2 bold */
    uint8_t italic = 0;             /* 0 unset, 1 normal, 2 italic */
    uint8_t decoration = 0;         /* CssDecoration bits */
    uint8_t display_none = 0;
    uint8_t page_break_before = 0;
    uint8_t page_break_after = 0;
    uint8_t vertical_align = 0;     /* 0 unset, 1 baseline, 2 super, 3 sub */
    uint8_t preserve_whitespace = 0;
    uint8_t shaded = 0;
    uint8_t initial_letter = 0;
    uint8_t float_left = 0;
    CssBorderSide border_top, border_right, border_bottom, border_left;

    void apply(const CssStyle& overlay); /* overlay's defined fields win */
    bool any_defined() const { return defined != 0; }
    void reset() { *this = CssStyle{}; }
};

struct CssFontFace {
    std::string family;
    std::string src_href; /* package-relative path when inside the book */
    int weight = 400;
    int italic = 0;
};

/* A hashed snapshot of one open element for contextual selector matching. */
struct CssAncestor {
    uint32_t tag_hash = 0;
    uint32_t id_hash = 0;
    uint32_t class_hashes[4] = {0, 0, 0, 0};
    uint8_t class_count = 0;

    static CssAncestor make(std::string_view tag, std::string_view class_attr,
                            std::string_view id_attr);
};

class CssSheet {
   public:
    CssSheet();
    ~CssSheet();
    CssSheet(const CssSheet&) = delete;
    CssSheet& operator=(const CssSheet&) = delete;

    void clear();
    /* May be called for multiple stylesheets; later rules win on equal specificity. */
    void add_stylesheet(std::string_view css);

    bool empty() const;
    size_t rule_count() const;
    bool has_first_letter_rules() const;

    /* Resolve the merged style for an element.  ancestors are outermost first,
     * the last entry is the direct parent. */
    CssStyle resolve(std::string_view tag, std::string_view class_attr,
                     std::string_view id_attr, const CssAncestor* ancestors,
                     size_t ancestor_count) const;
    /* Resolve ::first-letter rules for an element (used for drop caps). */
    CssStyle resolve_first_letter(std::string_view tag, std::string_view class_attr,
                                  std::string_view id_attr, const CssAncestor* ancestors,
                                  size_t ancestor_count) const;

    static CssStyle parse_inline(std::string_view style_value);

    const std::vector<CssFontFace>& font_faces() const;

    /* Intern a family name for stylesheet-internal use; returns -1 on failure. */
    int intern_family(std::string_view family);
    const std::string& family(int id) const;

   private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace repub

#endif  // REPUB_SRC_CONTENT_CSS_H
