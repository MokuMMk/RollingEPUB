/*
 * RollingEPUB CSS cascade engine tests.
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstdlib>
#include <string>
#include <vector>

#include "content/css.h"
#include "test_harness.h"

using namespace repub;

namespace {

CssStyle R(CssSheet& sheet, const char* tag, const char* class_attr = "",
           const char* id_attr = "") {
    return sheet.resolve(tag, class_attr, id_attr, nullptr, 0);
}

CssStyle RF(CssSheet& sheet, const char* tag, const char* class_attr = "",
            const char* id_attr = "") {
    return sheet.resolve_first_letter(tag, class_attr, id_attr, nullptr, 0);
}

CssStyle Resolve(const char* css, const char* tag, const char* class_attr = "",
                 const char* id_attr = "", const CssAncestor* ancestors = nullptr,
                 size_t ancestor_count = 0, bool first_letter = false) {
    CssSheet sheet;
    sheet.add_stylesheet(css);
    if (first_letter) {
        return sheet.resolve_first_letter(tag, class_attr, id_attr, ancestors,
                                          ancestor_count);
    }
    return sheet.resolve(tag, class_attr, id_attr, ancestors, ancestor_count);
}

std::string Repeat(char c, size_t count) { return std::string(count, c); }

}  // namespace

/* --------------------------------------------------------------------- */
/* CssLength                                                             */
/* --------------------------------------------------------------------- */

REPUB_TEST(length_units) {
    CssLength px;
    px.value = 12.0f;
    px.unit = CssUnit::Px;
    px.set = true;
    REPUB_CHECK_EQ(px.to_px(10.0f), 12);

    CssLength em;
    em.value = 2.0f;
    em.unit = CssUnit::Em;
    em.set = true;
    REPUB_CHECK_EQ(em.to_px(10.0f), 20);

    CssLength percent;
    percent.value = 50.0f;
    percent.unit = CssUnit::Percent;
    percent.set = true;
    REPUB_CHECK_EQ(percent.to_px(10.0f, 200.0f), 100);

    CssLength pt;
    pt.value = 12.0f;
    pt.unit = CssUnit::Pt;
    pt.set = true;
    REPUB_CHECK_EQ(pt.to_px(10.0f), 16); /* 12 * 1.333 = 15.996 -> 16 */

    const CssLength unset;
    REPUB_CHECK_EQ(unset.to_px(10.0f, 100.0f), 0);

    /* Parsed declarations keep their deferred unit. */
    const CssStyle style = Resolve("p { text-indent: 2em; margin-left: 25%; }", "p");
    REPUB_CHECK_EQ(style.indent.value, 2.0f);
    REPUB_CHECK_EQ(static_cast<int>(style.indent.unit), static_cast<int>(CssUnit::Em));
    REPUB_CHECK_EQ(style.margin_left.to_px(10.0f, 400.0f), 100);
    /* rem is represented as em (css.h has no Rem unit). */
    const CssStyle rem_style = Resolve("p { text-indent: 1.5rem; }", "p");
    REPUB_CHECK_EQ(rem_style.indent.to_px(16.0f), 24);
}

/* --------------------------------------------------------------------- */
/* Family interning                                                      */
/* --------------------------------------------------------------------- */

REPUB_TEST(family_interning_case_insensitive) {
    CssSheet sheet;
    const int a = sheet.intern_family("Noto Serif");
    const int b = sheet.intern_family("noto serif");
    const int c = sheet.intern_family("  NOTO SERIF  ");
    REPUB_CHECK(a >= 0);
    REPUB_CHECK_EQ(a, b);
    REPUB_CHECK_EQ(a, c);
    REPUB_CHECK_STR_EQ(sheet.family(a), "Noto Serif");

    const int quoted = sheet.intern_family("\"Book Font\"");
    REPUB_CHECK(quoted >= 0);
    REPUB_CHECK_STR_EQ(sheet.family(quoted), "Book Font");

    REPUB_CHECK_EQ(sheet.intern_family(""), -1);
    REPUB_CHECK(sheet.family(9999).empty());
    REPUB_CHECK(sheet.family(-1).empty());

    /* A stylesheet font-family interns into the same table. */
    CssSheet rule_sheet;
    rule_sheet.add_stylesheet("p { font-family: \"Noto Serif\", serif; }");
    const CssStyle style = R(rule_sheet, "p", "", "");
    REPUB_CHECK(style.defined & CSS_DEF_FAMILY);
    REPUB_CHECK(style.family_id >= 0);
    REPUB_CHECK_STR_EQ(rule_sheet.family(style.family_id), "Noto Serif");
}

/* --------------------------------------------------------------------- */
/* Selectors and cascade                                                 */
/* --------------------------------------------------------------------- */

REPUB_TEST(selector_specificity) {
    CssSheet sheet;
    sheet.add_stylesheet(
        "p { text-align: left; }\n"
        ".cls { text-align: center; }\n"
        "#x { text-align: right; }\n");
    REPUB_CHECK_EQ(static_cast<int>(R(sheet, "p", "", "").align),
                   static_cast<int>(TextAlign::Left));
    REPUB_CHECK_EQ(static_cast<int>(R(sheet, "p", "cls", "").align),
                   static_cast<int>(TextAlign::Center));
    REPUB_CHECK_EQ(static_cast<int>(R(sheet, "p", "cls", "x").align),
                   static_cast<int>(TextAlign::Right));

    /* Descendant compounds add specificity: 2 element/tag contributions beat 1. */
    CssSheet sheet2;
    sheet2.add_stylesheet("div p { text-align: right; } p { text-align: left; }");
    const CssAncestor div = CssAncestor::make("div", "", "");
    REPUB_CHECK_EQ(static_cast<int>(sheet2.resolve("p", "", "", &div, 1).align),
                   static_cast<int>(TextAlign::Right));
}

REPUB_TEST(selector_child_vs_descendant) {
    CssSheet sheet;
    sheet.add_stylesheet(
        "div > p { font-weight: bold; }\n"
        "div p { font-style: italic; }\n");

    CssAncestor direct[1] = {CssAncestor::make("div", "", "")};
    const CssStyle child = sheet.resolve("p", "", "", direct, 1);
    REPUB_CHECK(child.defined & CSS_DEF_WEIGHT);
    REPUB_CHECK_EQ(child.weight, 2);
    REPUB_CHECK(child.defined & CSS_DEF_ITALIC);

    CssAncestor nested[2] = {CssAncestor::make("div", "", ""),
                            CssAncestor::make("span", "", "")};
    const CssStyle deep = sheet.resolve("p", "", "", nested, 2);
    REPUB_CHECK(!(deep.defined & CSS_DEF_WEIGHT)); /* direct child rule must not match */
    REPUB_CHECK(deep.defined & CSS_DEF_ITALIC);

    CssAncestor other[1] = {CssAncestor::make("section", "", "")};
    const CssStyle none = sheet.resolve("p", "", "", other, 1);
    REPUB_CHECK(!(none.defined & CSS_DEF_ITALIC));
}

REPUB_TEST(selector_groups) {
    CssSheet sheet;
    sheet.add_stylesheet("h1, .title, #main { text-align: center; font-weight: bold; }");
    REPUB_CHECK_EQ(static_cast<int>(R(sheet, "h1", "", "").align),
                   static_cast<int>(TextAlign::Center));
    REPUB_CHECK_EQ(static_cast<int>(R(sheet, "p", "title", "").align),
                   static_cast<int>(TextAlign::Center));
    REPUB_CHECK_EQ(static_cast<int>(R(sheet, "p", "", "main").align),
                   static_cast<int>(TextAlign::Center));
    REPUB_CHECK(!(R(sheet, "p", "", "").defined & CSS_DEF_ALIGN));
}

REPUB_TEST(selector_later_rule_wins_on_tie) {
    CssSheet sheet;
    sheet.add_stylesheet(".a { text-align: left; font-weight: normal; }");
    sheet.add_stylesheet(".a { text-align: right; }"); /* later stylesheet, same spec */
    const CssStyle style = R(sheet, "p", "a", "");
    REPUB_CHECK_EQ(static_cast<int>(style.align), static_cast<int>(TextAlign::Right));
    REPUB_CHECK_EQ(style.weight, 1);

    CssSheet ordered;
    ordered.add_stylesheet(".a { text-align: right; } .a { text-align: left; }");
    REPUB_CHECK_EQ(static_cast<int>(R(ordered, "p", "a", "").align),
                   static_cast<int>(TextAlign::Left));
}

REPUB_TEST(selector_matching_is_case_insensitive) {
    CssSheet sheet;
    sheet.add_stylesheet(
        "P { text-align: center; }\n"
        ".Note { font-weight: bold; }\n"
        "#Main { font-style: italic; }\n"
        "DIV.Outer > SPAN { text-indent: 1em; }\n");
    const CssStyle style = R(sheet, "p", "NOTE", "main");
    REPUB_CHECK_EQ(static_cast<int>(style.align), static_cast<int>(TextAlign::Center));
    REPUB_CHECK_EQ(style.weight, 2);
    REPUB_CHECK_EQ(style.italic, 2);

    CssAncestor outer = CssAncestor::make("div", "outer", "");
    REPUB_CHECK(sheet.resolve("span", "", "", &outer, 1).defined & CSS_DEF_INDENT);
}

REPUB_TEST(ancestor_make_hashes) {
    const CssAncestor a = CssAncestor::make("DIV", "one two three four five", "Main");
    const CssAncestor b = CssAncestor::make("div", "ONE TWO THREE FOUR FIVE", "MAIN");
    REPUB_CHECK(a.tag_hash != 0);
    REPUB_CHECK(a.id_hash != 0);
    REPUB_CHECK_EQ(a.tag_hash, b.tag_hash);
    REPUB_CHECK_EQ(a.id_hash, b.id_hash);
    REPUB_CHECK_EQ(a.class_count, 4); /* classes past the fourth are not tracked */
    REPUB_CHECK_EQ(a.class_hashes[0], b.class_hashes[0]);
    REPUB_CHECK_EQ(a.class_hashes[3], b.class_hashes[3]);

    const CssAncestor empty = CssAncestor::make("", "", "");
    REPUB_CHECK_EQ(empty.tag_hash, 0);
    REPUB_CHECK_EQ(empty.id_hash, 0);
    REPUB_CHECK_EQ(empty.class_count, 0);
}

/* --------------------------------------------------------------------- */
/* Inline styles and apply()                                             */
/* --------------------------------------------------------------------- */

REPUB_TEST(inline_overlay) {
    CssSheet sheet;
    sheet.add_stylesheet("p { text-align: center; margin-top: 1em; font-style: italic; }");
    CssStyle base = R(sheet, "p", "", "");
    const CssStyle overlay = CssSheet::parse_inline("text-align: right; font-weight: bold;");
    REPUB_CHECK(overlay.defined & CSS_DEF_ALIGN);
    REPUB_CHECK(overlay.defined & CSS_DEF_WEIGHT);
    REPUB_CHECK(!(overlay.defined & CSS_DEF_MARGIN_TOP));

    base.apply(overlay);
    REPUB_CHECK_EQ(static_cast<int>(base.align), static_cast<int>(TextAlign::Right));
    REPUB_CHECK_EQ(base.weight, 2);
    REPUB_CHECK_EQ(base.italic, 2); /* untouched inherited field is preserved */
    REPUB_CHECK(base.defined & CSS_DEF_MARGIN_TOP);
    REPUB_CHECK_EQ(base.margin_top.value, 1.0f);
    REPUB_CHECK_EQ(base.margin_top.unit, CssUnit::Em);

    /* Inline declaration without a trailing semicolon. */
    const CssStyle no_semicolon =
        CssSheet::parse_inline("text-align: justify; display: none");
    REPUB_CHECK_EQ(static_cast<int>(no_semicolon.align), static_cast<int>(TextAlign::Justify));
    REPUB_CHECK_EQ(no_semicolon.display_none, 1);
    REPUB_CHECK(no_semicolon.defined & CSS_DEF_DISPLAY);
}

/* --------------------------------------------------------------------- */
/* Declarations                                                          */
/* --------------------------------------------------------------------- */

REPUB_TEST(font_size_values) {
    REPUB_CHECK_EQ(Resolve("p { font-size: 120%; }", "p").font_scale_percent, 120);
    REPUB_CHECK_EQ(Resolve("p { font-size: 2em; }", "p").font_scale_percent, 200);
    REPUB_CHECK_EQ(Resolve("p { font-size: 1.5rem; }", "p").font_scale_percent, 150);
    REPUB_CHECK_EQ(Resolve("p { font-size: 32px; }", "p").font_scale_percent, 200);
    REPUB_CHECK_EQ(Resolve("p { font-size: large; }", "p").font_scale_percent, 120);
    REPUB_CHECK_EQ(Resolve("p { font-size: xx-large; }", "p").font_scale_percent, 200);
    REPUB_CHECK_EQ(Resolve("p { font-size: medium; }", "p").font_scale_percent, 100);
    REPUB_CHECK_EQ(Resolve("p { font-size: smaller; }", "p").font_scale_percent, 83);
    REPUB_CHECK_EQ(Resolve("p { font-size: larger; }", "p").font_scale_percent, 120);
    REPUB_CHECK_EQ(Resolve("p { font-size: 12pt; }", "p").font_scale_percent, 100);
    REPUB_CHECK(!(Resolve("p { font-size: inherit; }", "p").defined & CSS_DEF_FONT_SIZE));
    REPUB_CHECK(!(Resolve("p { font-size: huge; }", "p").defined & CSS_DEF_FONT_SIZE));
}

REPUB_TEST(font_style_weight_decoration) {
    REPUB_CHECK_EQ(Resolve("p { font-weight: bold; }", "p").weight, 2);
    REPUB_CHECK_EQ(Resolve("p { font-weight: bolder; }", "p").weight, 2);
    REPUB_CHECK_EQ(Resolve("p { font-weight: 700; }", "p").weight, 2);
    REPUB_CHECK_EQ(Resolve("p { font-weight: 900; }", "p").weight, 2);
    REPUB_CHECK_EQ(Resolve("p { font-weight: normal; }", "p").weight, 1);
    REPUB_CHECK_EQ(Resolve("p { font-weight: lighter; }", "p").weight, 1);
    REPUB_CHECK_EQ(Resolve("p { font-weight: 400; }", "p").weight, 1);
    REPUB_CHECK_EQ(Resolve("p { font-weight: 600; }", "p").weight, 1);

    REPUB_CHECK_EQ(Resolve("p { font-style: italic; }", "p").italic, 2);
    REPUB_CHECK_EQ(Resolve("p { font-style: oblique; }", "p").italic, 2);
    REPUB_CHECK_EQ(Resolve("p { font-style: normal; }", "p").italic, 1);

    const CssStyle under = Resolve("a { text-decoration: underline; }", "a");
    REPUB_CHECK_EQ(under.decoration, static_cast<uint8_t>(CSS_DECOR_UNDERLINE));
    const CssStyle strike_under =
        Resolve("a { text-decoration: line-through underline; }", "a");
    REPUB_CHECK(strike_under.decoration & CSS_DECOR_STRIKE);
    REPUB_CHECK(strike_under.decoration & CSS_DECOR_UNDERLINE);
    const CssStyle none = Resolve("a { text-decoration: none; }", "a");
    REPUB_CHECK_EQ(none.decoration, 0);
    REPUB_CHECK(none.defined & CSS_DEF_DECORATION);
}

REPUB_TEST(margins_and_padding) {
    const CssStyle margin4 = Resolve("p { margin: 1em 2em 3em 4em; }", "p");
    REPUB_CHECK(margin4.defined & CSS_DEF_MARGIN_TOP);
    REPUB_CHECK(margin4.defined & CSS_DEF_MARGIN_LEFT);
    REPUB_CHECK_EQ(margin4.margin_top.value, 1.0f);
    REPUB_CHECK_EQ(margin4.margin_right.value, 2.0f);
    REPUB_CHECK_EQ(margin4.margin_bottom.value, 3.0f);
    REPUB_CHECK_EQ(margin4.margin_left.value, 4.0f);

    const CssStyle margin2 = Resolve("p { margin: 10px 20px; }", "p");
    REPUB_CHECK_EQ(margin2.margin_top.value, 10.0f);
    REPUB_CHECK_EQ(margin2.margin_right.value, 20.0f);
    REPUB_CHECK_EQ(margin2.margin_bottom.value, 10.0f);
    REPUB_CHECK_EQ(margin2.margin_left.value, 20.0f);

    /* padding has no fields; it maps onto margins. */
    const CssStyle padding = Resolve("p { padding-left: 5px; padding: 1px 2px 3px 4px; }",
                                      "p");
    REPUB_CHECK_EQ(padding.margin_top.value, 1.0f);
    REPUB_CHECK_EQ(padding.margin_right.value, 2.0f);
    REPUB_CHECK_EQ(padding.margin_bottom.value, 3.0f);
    REPUB_CHECK_EQ(padding.margin_left.value, 4.0f);

    const CssStyle negative = Resolve("p { margin-left: -10px; }", "p");
    REPUB_CHECK_EQ(negative.margin_left.value, -10.0f);
    REPUB_CHECK_EQ(negative.margin_left.to_px(10.0f), -10);

    /* auto/inherit must not define a margin. */
    const CssStyle auto_margin = Resolve("p { margin-top: auto; margin-left: inherit; }", "p");
    REPUB_CHECK(!(auto_margin.defined & CSS_DEF_MARGIN_TOP));
    REPUB_CHECK(!(auto_margin.defined & CSS_DEF_MARGIN_LEFT));
}

REPUB_TEST(display_break_whitespace) {
    const CssStyle hidden = Resolve(".hidden { display: none; }", "div", "hidden");
    REPUB_CHECK_EQ(hidden.display_none, 1);
    REPUB_CHECK(hidden.defined & CSS_DEF_DISPLAY);
    const CssStyle block = Resolve("div { display: block; }", "div");
    REPUB_CHECK_EQ(block.display_none, 0);
    REPUB_CHECK(block.defined & CSS_DEF_DISPLAY);

    const CssStyle brk = Resolve(
        "h1 { page-break-before: always; break-after: right; }\n"
        "h2 { break-before: page; page-break-after: auto; }",
        "h1");
    REPUB_CHECK_EQ(brk.page_break_before, 1);
    REPUB_CHECK_EQ(brk.page_break_after, 1);
    const CssStyle h2 = Resolve(
        "h1 { page-break-before: always; }\n"
        "h2 { break-before: page; page-break-after: auto; }",
        "h2");
    REPUB_CHECK_EQ(h2.page_break_before, 1);
    REPUB_CHECK_EQ(h2.page_break_after, 0);
    REPUB_CHECK(h2.defined & CSS_DEF_PAGE_BREAK_AFTER);

    const CssStyle pre = Resolve("pre { white-space: pre; }", "pre");
    REPUB_CHECK_EQ(pre.preserve_whitespace, 1);
    REPUB_CHECK(pre.defined & CSS_DEF_WHITE_SPACE);
    const CssStyle prewrap = Resolve("pre { white-space: pre-wrap; }", "pre");
    REPUB_CHECK_EQ(prewrap.preserve_whitespace, 1);
    const CssStyle breakspaces = Resolve("pre { white-space: break-spaces; }", "pre");
    REPUB_CHECK_EQ(breakspaces.preserve_whitespace, 1);
    const CssStyle nowrap = Resolve("p { white-space: nowrap; }", "p");
    REPUB_CHECK_EQ(nowrap.preserve_whitespace, 0);
    REPUB_CHECK(nowrap.defined & CSS_DEF_WHITE_SPACE);

    REPUB_CHECK_EQ(Resolve("sup { vertical-align: super; }", "sup").vertical_align, 2);
    REPUB_CHECK_EQ(Resolve("sub { vertical-align: sub; }", "sub").vertical_align, 3);
    REPUB_CHECK_EQ(Resolve("span { vertical-align: baseline; }", "span").vertical_align, 1);
    REPUB_CHECK(!(Resolve("span { vertical-align: 0.5em; }", "span").defined &
                  CSS_DEF_VERTICAL_ALIGN));
}

REPUB_TEST(float_shading_list_style) {
    REPUB_CHECK_EQ(Resolve("span { float: left; }", "span").float_left, 1);
    const CssStyle no_float = Resolve("span { float: none; }", "span");
    REPUB_CHECK_EQ(no_float.float_left, 0);
    REPUB_CHECK(no_float.defined & CSS_DEF_FLOAT_LEFT);

    const CssStyle shaded = Resolve("div { background-color: #eeeeee; }", "div");
    REPUB_CHECK_EQ(shaded.shaded, 1);
    REPUB_CHECK(shaded.defined & CSS_DEF_SHADED);
    const CssStyle white = Resolve("div { background-color: white; }", "div");
    REPUB_CHECK_EQ(white.shaded, 0);
    REPUB_CHECK(white.defined & CSS_DEF_SHADED);
    const CssStyle transparent = Resolve("div { background-color: transparent; }", "div");
    REPUB_CHECK_EQ(transparent.shaded, 0);

    const CssStyle none = Resolve("li { list-style: none; }", "li");
    REPUB_CHECK_EQ(none.list_style_none, 1);
    REPUB_CHECK(none.defined & CSS_DEF_LIST_STYLE);
    const CssStyle disc = Resolve("ul { list-style-type: disc; }", "ul");
    REPUB_CHECK_EQ(disc.list_style_none, 0);
    REPUB_CHECK(disc.defined & CSS_DEF_LIST_STYLE);

    /* apply(): an explicit marker re-enables bullets over inherited none. */
    CssStyle base = none;
    base.apply(disc);
    REPUB_CHECK_EQ(base.list_style_none, 0);
}

/* --------------------------------------------------------------------- */
/* Borders                                                               */
/* --------------------------------------------------------------------- */

REPUB_TEST(borders) {
    const CssStyle style = Resolve(
        "div { border: 2px solid; border-left: 1px dotted; border-bottom: 3px dashed; }",
        "div");
    REPUB_CHECK(style.border_top.visible());
    REPUB_CHECK_EQ(style.border_top.width, 2);
    REPUB_CHECK_EQ(style.border_top.style, 1);
    REPUB_CHECK(style.border_left.visible());
    REPUB_CHECK_EQ(style.border_left.width, 1);
    REPUB_CHECK_EQ(style.border_left.style, 3);
    REPUB_CHECK_EQ(style.border_bottom.style, 4);
    REPUB_CHECK(style.defined & CSS_DEF_BORDER_TOP);
    REPUB_CHECK(style.defined & CSS_DEF_BORDER_LEFT);

    /* Width-only declarations combine with the style from an earlier rule. */
    CssSheet sheet;
    sheet.add_stylesheet("div { border-top: 2px solid; }");
    CssStyle base = R(sheet, "div", "", "");
    const CssStyle width_overlay = CssSheet::parse_inline("border-top-width: 5px;");
    base.apply(width_overlay);
    REPUB_CHECK_EQ(base.border_top.width, 5);
    REPUB_CHECK_EQ(base.border_top.style, 1); /* style preserved */

    const CssStyle style_overlay = CssSheet::parse_inline("border-top-style: dashed;");
    base.apply(style_overlay);
    REPUB_CHECK_EQ(base.border_top.width, 5);
    REPUB_CHECK_EQ(base.border_top.style, 4);

    /* border: none clears a previously visible side. */
    CssStyle cleared = base;
    cleared.apply(CssSheet::parse_inline("border: none;"));
    REPUB_CHECK(!cleared.border_top.visible());
    REPUB_CHECK_EQ(cleared.border_top.width, 0);
    REPUB_CHECK_EQ(cleared.border_top.style, 0);

    /* Unrelated rules do not disturb borders or other fields. */
    CssSheet plain;
    plain.add_stylesheet("p { border-right: 1px solid; margin-top: 4px; }");
    CssStyle p = R(plain, "p", "", "");
    p.apply(CssSheet::parse_inline("text-align: center;"));
    REPUB_CHECK(p.border_right.visible());
    REPUB_CHECK(p.defined & CSS_DEF_MARGIN_TOP);

    /* border-top shorthand alone. */
    const CssStyle top = Resolve("p { border-top: thick double; }", "p");
    REPUB_CHECK_EQ(top.border_top.width, 3);
    REPUB_CHECK_EQ(top.border_top.style, 2);
    REPUB_CHECK(top.border_top.visible());

    /* Colors are accepted but not tracked; they do not break parsing. */
    const CssStyle colored = Resolve("p { border: 1px solid red; }", "p");
    REPUB_CHECK(colored.border_top.visible());
}

/* --------------------------------------------------------------------- */
/* ::first-letter and initial-letter                                     */
/* --------------------------------------------------------------------- */

REPUB_TEST(first_letter_rules) {
    CssSheet sheet;
    sheet.add_stylesheet(
        "p { text-align: center; }\n"
        "p::first-letter { font-weight: bold; initial-letter: 3; float: left; }\n"
        "div p:first-letter { text-align: right; }");
    REPUB_CHECK(sheet.has_first_letter_rules());
    REPUB_CHECK_EQ(sheet.rule_count(), 3u);

    const CssStyle normal = R(sheet, "p", "", "");
    REPUB_CHECK(!(normal.defined & CSS_DEF_WEIGHT));

    const CssStyle first = RF(sheet, "p", "", "");
    REPUB_CHECK(first.defined & CSS_DEF_WEIGHT);
    REPUB_CHECK_EQ(first.weight, 2);
    REPUB_CHECK_EQ(first.initial_letter, 3);
    REPUB_CHECK_EQ(first.float_left, 1);

    const CssAncestor div = CssAncestor::make("div", "", "");
    const CssStyle scoped = sheet.resolve_first_letter("p", "", "", &div, 1);
    REPUB_CHECK_EQ(static_cast<int>(scoped.align), static_cast<int>(TextAlign::Right));
    REPUB_CHECK_EQ(scoped.initial_letter, 3); /* both first-letter rules apply */

    /* initial-letter clamps to 1..4. */
    REPUB_CHECK_EQ(Resolve("p::first-letter { initial-letter: 9; }", "p", "", "", nullptr, 0,
                           true)
                       .initial_letter,
                   4);
    REPUB_CHECK_EQ(Resolve("p::first-letter { initial-letter: 1; }", "p", "", "", nullptr, 0,
                           true)
                       .initial_letter,
                   1);

    CssSheet empty_sheet;
    REPUB_CHECK(!empty_sheet.has_first_letter_rules());
    REPUB_CHECK(!RF(empty_sheet, "p", "", "").any_defined());
}

/* --------------------------------------------------------------------- */
/* @font-face, @media and at-rules                                       */
/* --------------------------------------------------------------------- */

REPUB_TEST(font_face_collection) {
    CssSheet sheet;
    sheet.add_stylesheet(
        "@font-face {\n"
        "  font-family: \"Book Font\";\n"
        "  src: url(fonts/book.woff);\n"
        "  font-weight: bold;\n"
        "  font-style: italic;\n"
        "}\n"
        "@font-face { font-family: Other; src: local(\"x\"), url('fonts/other.ttf') "
        "format(\"truetype\"); }\n"
        "@font-face { font-family: Encoded; src: url(data:font/ttf;base64,AAAA); }\n"
        "@font-face { font-family: Broken; src: url(); }\n");

    const std::vector<CssFontFace>& faces = sheet.font_faces();
    REPUB_CHECK_EQ(faces.size(), 2u);
    REPUB_CHECK_STR_EQ(faces[0].family, "Book Font");
    REPUB_CHECK_STR_EQ(faces[0].src_href, "fonts/book.woff");
    REPUB_CHECK_EQ(faces[0].weight, 700);
    REPUB_CHECK_EQ(faces[0].italic, 1);
    REPUB_CHECK_STR_EQ(faces[1].family, "Other");
    REPUB_CHECK_STR_EQ(faces[1].src_href, "fonts/other.ttf");
    REPUB_CHECK_EQ(faces[1].weight, 400);
    REPUB_CHECK_EQ(faces[1].italic, 0);

    /* @font-face declarations never become element rules. */
    REPUB_CHECK_EQ(sheet.rule_count(), 0u);
    REPUB_CHECK(sheet.empty());
}

REPUB_TEST(at_rule_skipping) {
    CssSheet sheet;
    sheet.add_stylesheet(
        "@charset \"utf-8\";\n"
        "@import url(other.css);\n"
        "@namespace epub \"http://www.idpf.org/2007/ops\";\n"
        "@media screen and (min-width: 100px) {\n"
        "  p { text-align: center; }\n"
        "  .x { font-weight: bold; }\n"
        "  @media print { p { text-align: right; } }\n"
        "}\n"
        "@supports (display: grid) { p { text-align: justify; } }\n"
        "p { text-align: left; }\n");

    const CssStyle p = R(sheet, "p", "", "");
    REPUB_CHECK_EQ(static_cast<int>(p.align), static_cast<int>(TextAlign::Left));
    const CssStyle x = R(sheet, "div", "x", "");
    REPUB_CHECK(!(x.defined & CSS_DEF_WEIGHT));
    REPUB_CHECK_EQ(sheet.rule_count(), 1u);
}

/* --------------------------------------------------------------------- */
/* Robustness                                                            */
/* --------------------------------------------------------------------- */

REPUB_TEST(comments_and_important) {
    CssSheet sheet;
    sheet.add_stylesheet(
        "/* leading comment */\n"
        "p /* mid-selector */ { text-align: right !important; "
        "font-weight: bold !IMPORTANT; }\n"
        "b/* glued */.c { text-indent: 1em; }\n"
        "/* trailing comment */\n");
    const CssStyle style = R(sheet, "p", "", "");
    REPUB_CHECK_EQ(static_cast<int>(style.align), static_cast<int>(TextAlign::Right));
    REPUB_CHECK_EQ(style.weight, 2);
    /* A comment between tag and class must not turn the selector into a
     * descendant selector ("b.c", not "b .c"). */
    REPUB_CHECK(R(sheet, "b", "c", "").defined & CSS_DEF_INDENT);
}

REPUB_TEST(malformed_css_is_ignored) {
    CssSheet sheet;
    sheet.add_stylesheet(
        "p..bad { text-align: center; }\n"
        "a[href] { text-align: center; }\n"
        "p + p { text-align: center; }\n"
        "p ~ span { text-align: center; }\n"
        "p:first-child { text-align: center; }\n"
        "> p { text-align: center; }\n"
        "p > { text-align: center; }\n"
        "p { text-align: }\n"
        "p { text-align: center; font-weight: bold; }\n"
        "}\n"
        "span { }\n");
    const CssStyle p = R(sheet, "p", "", "");
    REPUB_CHECK_EQ(static_cast<int>(p.align), static_cast<int>(TextAlign::Center));
    REPUB_CHECK_EQ(p.weight, 2);
    REPUB_CHECK(!(R(sheet, "span", "", "").any_defined()));

    /* Unterminated rule: the tail is dropped without crashing. */
    CssSheet broken;
    broken.add_stylesheet("p { text-align: center; ");
    REPUB_CHECK(broken.empty());

    /* Huge/hostile input is bounded instead of trusted. */
    CssSheet hostile;
    std::string huge;
    for (int i = 0; i < 600; ++i) {
        huge += ".c" + std::to_string(i) + " { font-weight: bold; }\n";
    }
    hostile.add_stylesheet(huge);
    REPUB_CHECK(hostile.rule_count() <= 512u);
    REPUB_CHECK_EQ(hostile.rule_count(), 512u);

    CssSheet long_selector;
    long_selector.add_stylesheet(Repeat('a', 400) + " { text-align: center; } p { "
                                                   "font-weight: bold; }");
    REPUB_CHECK_EQ(long_selector.rule_count(), 1u);
    REPUB_CHECK_EQ(R(long_selector, "p", "", "").weight, 2);
}

REPUB_TEST(empty_sheet_and_clear) {
    CssSheet sheet;
    REPUB_CHECK(sheet.empty());
    REPUB_CHECK_EQ(sheet.rule_count(), 0u);
    REPUB_CHECK(!sheet.has_first_letter_rules());
    REPUB_CHECK(!R(sheet, "p", "", "").any_defined());
    REPUB_CHECK(!RF(sheet, "p", "", "").any_defined());
    REPUB_CHECK(sheet.font_faces().empty());
    REPUB_CHECK_EQ(sheet.intern_family("X"), 0);

    sheet.add_stylesheet("p { text-align: center; } @font-face { font-family: F; "
                         "src: url(f.ttf); }");
    REPUB_CHECK(!sheet.empty());
    REPUB_CHECK_EQ(sheet.rule_count(), 1u);
    REPUB_CHECK_EQ(sheet.font_faces().size(), 1u);

    sheet.clear();
    REPUB_CHECK(sheet.empty());
    REPUB_CHECK_EQ(sheet.rule_count(), 0u);
    REPUB_CHECK(sheet.font_faces().empty());
    REPUB_CHECK(sheet.intern_family("Y") >= 0);
}
