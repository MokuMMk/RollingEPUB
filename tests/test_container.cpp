/*
 * RollingEPUB container layer tests: text utilities, XML pull parser, ZIP
 * reader and the public repub_book_* container API against real fixtures.
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "container/book.h"
#include "container/xml.h"
#include "container/zip.h"
#include "core/base.h"
#include "core/text_util.h"
#include "test_harness.h"

using namespace repub;

namespace {

const char* kMatrix = "epub/test_reader_rendering_matrix.epub";
const char* kNavBook = "epub/readpico_\u6cb3\u8fb9\u8bb0\u4e8b_nav.epub";
const char* kNcxBook = "epub/readpico_\u6cb3\u8fb9\u8bb0\u4e8b_ncx.epub";
const char* kPngBook = "epub/test_png_images.epub";
const char* kJpegBook = "epub/test_jpeg_images.epub";

/* Small RAII wrapper around the public book handle. */
struct BookHandle {
    repub_book_t* book = nullptr;
    explicit BookHandle(const char* fixture,
                        const repub_open_options_t* options = nullptr) {
        const std::string path = fixture_path(fixture);
        repub_err_t err = REPUB_OK;
        book = repub_book_open(&platform, path.c_str(), options, &err);
        REPUB_CHECK(book != nullptr);
        REPUB_CHECK_EQ(err, REPUB_OK);
    }
    ~BookHandle() {
        if (book) repub_book_close(book);
    }
    repub_platform_t platform = repub_platform_default();
};

std::string fixture_bytes(const char* name) { return read_file(fixture_path(name)); }

}  // namespace

/* ------------------------------------------------------------------------- */
/* text utilities                                                            */
/* ------------------------------------------------------------------------- */

REPUB_TEST(text_util_strings) {
    REPUB_CHECK_STR_EQ(to_lower("AbC-12"), "abc-12");
    REPUB_CHECK_STR_EQ(trim("  \t x y \r\n"), "x y");
    REPUB_CHECK_STR_EQ(trim_left("  x"), "x");
    REPUB_CHECK_STR_EQ(trim_right("x  "), "x");
    REPUB_CHECK(starts_with("hello", "he"));
    REPUB_CHECK(!starts_with("he", "hello"));
    REPUB_CHECK(starts_with_ci("HTTP://X", "http:"));
    REPUB_CHECK(ends_with_ci("Cover.JPG", ".jpg"));
    REPUB_CHECK(contains_ci("The Quick Fox", "quick"));
    const std::vector<std::string> parts = split("a,b,,c", ',');
    REPUB_CHECK_EQ(parts.size(), 4u);
    REPUB_CHECK_STR_EQ(parts[2], "");
    REPUB_CHECK_STR_EQ(join(parts, "-"), "a-b--c");
    REPUB_CHECK_STR_EQ(collapse_whitespace("  a \n\t b  "), "a b");
    REPUB_CHECK_EQ(parse_int(" 42 ", -1), 42);
    REPUB_CHECK_EQ(parse_int("nope", -7), -7);
    REPUB_CHECK_EQ(parse_int("12x", -7), -7);
    REPUB_CHECK(parse_float("1.5", -1.0f) == 1.5f);
    REPUB_CHECK(parse_float("junk", -1.0f) == -1.0f);
    float strict = 0.0f;
    REPUB_CHECK(parse_float_strict(" 2.25 ", &strict));
    REPUB_CHECK(strict == 2.25f);
    REPUB_CHECK(!parse_float_strict("2.25x", &strict));
    REPUB_CHECK(compare_ci("AbC", "abc") == 0);
    REPUB_CHECK(compare_ci("abc", "abd") < 0);
}

REPUB_TEST(text_util_utf8) {
    const char* valid = "a\u00e9\u4e2d\U0001f600";
    REPUB_CHECK(utf8_valid(valid, std::strlen(valid)));
    REPUB_CHECK_EQ(utf8_length(valid, std::strlen(valid)), 4u);
    const char* broken = "\xe4\xb8";
    uint32_t cp = 0;
    REPUB_CHECK_EQ(utf8_decode(broken, 2, &cp), 1u);
    REPUB_CHECK_EQ(cp, 0xFFFDu);
    char encoded[4];
    REPUB_CHECK_EQ(utf8_encode(0x4E2D, encoded), 3u);
    REPUB_CHECK(std::memcmp(encoded, "\xe4\xb8\xad", 3) == 0);
    REPUB_CHECK_EQ(utf8_encode(0xD800, encoded), 3u); /* surrogate -> U+FFFD */
    const char* text = "a\xe4\xb8\xad";
    REPUB_CHECK_EQ(utf8_prev(text, 4), 1u);

    REPUB_CHECK(is_space(' '));
    REPUB_CHECK(is_space(0x3000));
    REPUB_CHECK(!is_horizontal_space('\n'));
    REPUB_CHECK(is_newline('\r'));
    REPUB_CHECK(is_cjk(0x4E2D));
    REPUB_CHECK(is_cjk_punctuation(0x3002));
    REPUB_CHECK(is_wide(0x4E2D));
    REPUB_CHECK(is_latin_digit('7'));
    REPUB_CHECK(!is_latin_digit(0xFF17));
    REPUB_CHECK(is_combining(0x0301));
    REPUB_CHECK(is_rtl(0x05D0));
    REPUB_CHECK(!is_rtl('A'));
}

REPUB_TEST(text_util_entities) {
    uint32_t cp = 0;
    REPUB_CHECK_EQ(decode_entity("&amp;", 5, &cp), 5u);
    REPUB_CHECK_EQ(cp, '&');
    REPUB_CHECK_EQ(decode_entity("&#65;", 5, &cp), 5u);
    REPUB_CHECK_EQ(cp, 'A');
    REPUB_CHECK_EQ(decode_entity("&#x42;", 6, &cp), 6u);
    REPUB_CHECK_EQ(cp, 'B');
    REPUB_CHECK_EQ(decode_entity("&nbsp;", 6, &cp), 6u);
    REPUB_CHECK_EQ(cp, 0xA0u);
    REPUB_CHECK_EQ(decode_entity("&mdash;", 7, &cp), 7u);
    REPUB_CHECK_EQ(cp, 0x2014u);
    REPUB_CHECK_EQ(decode_entity("&hellip;", 8, &cp), 8u);
    REPUB_CHECK_EQ(cp, 0x2026u);
    REPUB_CHECK_EQ(decode_entity("&#x110000;", 10, &cp), 10u);
    REPUB_CHECK_EQ(cp, 0xFFFDu); /* clamped */
    REPUB_CHECK_EQ(decode_entity("&bogus;", 7, &cp), 0u);
    REPUB_CHECK_STR_EQ(decode_entities("a&amp;b&#x4E2D;&quot;"), "a&b\u4e2d\"");
    /* Invalid UTF-8 is normalized, not dropped. */
    REPUB_CHECK_STR_EQ(decode_entities("\xff"), "\xef\xbf\xbd");
}

REPUB_TEST(text_util_paths) {
    REPUB_CHECK_STR_EQ(normalize_path("a/b/../c"), "a/c");
    REPUB_CHECK_STR_EQ(normalize_path("a//b\\.\\c"), "a/b/c");
    REPUB_CHECK_STR_EQ(normalize_path("/a/b/"), "a/b");
    REPUB_CHECK_STR_EQ(normalize_path("../../a"), "a");
    REPUB_CHECK_STR_EQ(dirname_of("OEBPS/content.opf"), "OEBPS");
    REPUB_CHECK_STR_EQ(dirname_of("content.opf"), "");
    REPUB_CHECK_STR_EQ(resolve_path("OEBPS", "text/ch1.xhtml#frag"), "OEBPS/text/ch1.xhtml");
    REPUB_CHECK_STR_EQ(resolve_path("OEBPS/Text", "../Images/a.png"), "OEBPS/Images/a.png");
    REPUB_CHECK_STR_EQ(resolve_path("OEBPS", "/Images/a.png"), "Images/a.png");
    REPUB_CHECK_STR_EQ(resolve_path("OEBPS", "a%20b.xhtml"), "OEBPS/a b.xhtml");
    REPUB_CHECK_STR_EQ(resolve_path("", "content.opf"), "content.opf");

    std::string file;
    std::string fragment;
    split_fragment("a/b.xhtml#p2", &file, &fragment);
    REPUB_CHECK_STR_EQ(file, "a/b.xhtml");
    REPUB_CHECK_STR_EQ(fragment, "p2");
    split_fragment("just.xhtml", &file, &fragment);
    REPUB_CHECK_STR_EQ(file, "just.xhtml");
    REPUB_CHECK_STR_EQ(fragment, "");
    REPUB_CHECK_STR_EQ(percent_decode("a%20b%2Fc"), "a b/c");
    REPUB_CHECK_STR_EQ(percent_decode("a%zz"), "a%zz");
    REPUB_CHECK_STR_EQ(strip_url_suffix("a.xhtml?x=1#f"), "a.xhtml");
    REPUB_CHECK(is_external_href("https://example.com"));
    REPUB_CHECK(is_external_href("mailto:a@b"));
    REPUB_CHECK(is_external_href("JavaScript:void(0)"));
    REPUB_CHECK(!is_external_href("chapter1.xhtml"));
    REPUB_CHECK(!is_external_href("/OEBPS/chapter1.xhtml"));
}

/* ------------------------------------------------------------------------- */
/* XML pull parser                                                           */
/* ------------------------------------------------------------------------- */

REPUB_TEST(xml_parser_basics) {
    const std::string xml =
        "\xef\xbb\xbf<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE html [ <!ENTITY foo \"bar\"> ]>\n"
        "<root a='1' b=two c=\"x &amp; y\">\n"
        "  <child>Hi &amp; &lt; &#65; &#x42; &nbsp;</child>\n"
        "  <empty/>\n"
        "  <![CDATA[raw & < > stuff]]>\n"
        "  <!-- note -->\n"
        "  <ns:tag xmlns:ns=\"urn:u\" title=\"&#169;\"/>\n"
        "</root>";
    XmlParser parser(xml);
    bool saw_doctype = false;
    bool saw_empty = false;
    bool saw_cdata = false;
    bool saw_comment = false;
    bool saw_ns = false;
    int root_depth = 0;
    std::string child_text;
    while (parser.next()) {
        const XmlParser::Token token = parser.token();
        if (token == XmlParser::Token::Doctype) saw_doctype = true;
        if (token == XmlParser::Token::StartElement) {
            if (parser.name() == "root") {
                root_depth = parser.depth();
                REPUB_CHECK_EQ(root_depth, 1);
                REPUB_CHECK_STR_EQ(parser.attr("a"), "1");
                REPUB_CHECK_STR_EQ(parser.attr("b"), "two");
                REPUB_CHECK_STR_EQ(parser.attr("c"), "x & y");
            } else if (parser.name() == "child") {
                REPUB_CHECK_EQ(parser.depth(), 2);
            } else if (parser.name() == "empty") {
                saw_empty = parser.self_closing();
            } else if (parser.name() == "ns:tag") {
                saw_ns = true;
                REPUB_CHECK(parser.self_closing());
                std::string local;
                parser.local_name(&local);
                REPUB_CHECK_STR_EQ(local, "tag");
                REPUB_CHECK_STR_EQ(parser.attr("title"), "\xc2\xa9");
                parser.skip_subtree();
            }
        } else if (token == XmlParser::Token::Cdata) {
            saw_cdata = true;
            REPUB_CHECK_STR_EQ(parser.text(), "raw & < > stuff");
        } else if (token == XmlParser::Token::Comment) {
            saw_comment = true;
            REPUB_CHECK_STR_EQ(parser.text(), " note ");
        } else if (token == XmlParser::Token::Text) {
            if (parser.depth() == 2 && parser.text().find("Hi") != std::string::npos) {
                child_text = parser.text();
            }
        }
    }
    REPUB_CHECK(!parser.failed());
    REPUB_CHECK(saw_doctype);
    REPUB_CHECK(saw_empty);
    REPUB_CHECK(saw_cdata);
    REPUB_CHECK(saw_comment);
    REPUB_CHECK(saw_ns);
    REPUB_CHECK_STR_EQ(child_text, "Hi & < A B \xc2\xa0");
}

REPUB_TEST(xml_parser_malformed) {
    {
        XmlParser parser("<root><a></root>");
        while (parser.next()) {
        }
        /* Mismatched close tags are tolerated; parsing must not crash. */
        REPUB_CHECK(true);
    }
    {
        XmlParser parser("<root attr=\"unterminated");
        while (parser.next()) {
        }
        REPUB_CHECK(parser.failed());
        REPUB_CHECK(!parser.error().empty());
    }
    {
        XmlParser parser("<root><!-- unterminated");
        while (parser.next()) {
        }
        REPUB_CHECK(parser.failed());
    }
    {
        XmlParser parser("<root><![CDATA[unterminated");
        while (parser.next()) {
        }
        REPUB_CHECK(parser.failed());
    }
    {
        XmlParser parser("<root><child></child></root>");
        bool saw_root = false;
        while (parser.next()) {
            if (parser.token() == XmlParser::Token::StartElement && parser.name() == "root") {
                saw_root = true;
                parser.skip_subtree();
            }
        }
        REPUB_CHECK(saw_root);
        REPUB_CHECK(!parser.failed());
    }
}

/* ------------------------------------------------------------------------- */
/* ZIP reader                                                                */
/* ------------------------------------------------------------------------- */

REPUB_TEST(zip_reader) {
    const std::string path = fixture_path(kMatrix);
    const repub_platform_t* platform = effective_platform(nullptr);
    ZipArchive zip;
    REPUB_CHECK_EQ(zip.open(platform, path), REPUB_OK);
    REPUB_CHECK(zip.is_open());
    REPUB_CHECK(zip.file_size() > 0);
    REPUB_CHECK_EQ(zip.path(), path);
    REPUB_CHECK(zip.entry_count() > 5);

    const int container = zip.find("META-INF/container.xml");
    REPUB_CHECK(container >= 0);
    REPUB_CHECK(zip.entry_size(static_cast<size_t>(container)) > 0);
    REPUB_CHECK_EQ(zip.entry_crc(static_cast<size_t>(container)),
                   zip.entry_crc(static_cast<size_t>(container)));

    /* Case-insensitive fallback. */
    REPUB_CHECK(zip.find("meta-inf/CONTAINER.xml") == container);
    REPUB_CHECK_EQ(zip.find("does/not/exist.xml"), -1);

    std::string xml;
    REPUB_CHECK_EQ(zip.extract_to_string(static_cast<size_t>(container), &xml), REPUB_OK);
    REPUB_CHECK(xml.find("container") != std::string::npos);
    std::vector<uint8_t> bytes;
    REPUB_CHECK_EQ(zip.extract(static_cast<size_t>(container), &bytes), REPUB_OK);
    REPUB_CHECK_EQ(bytes.size(), zip.entry_size(static_cast<size_t>(container)));

    uint8_t prefix[16] = {0};
    size_t written = 0;
    REPUB_CHECK_EQ(zip.extract_prefix(static_cast<size_t>(container), prefix, sizeof(prefix),
                                      &written),
                   REPUB_OK);
    REPUB_CHECK_EQ(written, sizeof(prefix));
    REPUB_CHECK(std::memcmp(prefix, xml.data(), sizeof(prefix)) == 0);

    /* Bounds are enforced by the caller-supplied cap. */
    REPUB_CHECK_EQ(zip.extract(static_cast<size_t>(container), &bytes, 4), REPUB_ERR_LIMIT);
    zip.close();
    REPUB_CHECK(!zip.is_open());
    REPUB_CHECK_EQ(zip.open(platform, path + ".missing"), REPUB_ERR_IO);
}

/* ------------------------------------------------------------------------- */
/* Book open / metadata / spine / TOC / resources                           */
/* ------------------------------------------------------------------------- */

REPUB_TEST(book_matrix_fixture) {
    BookHandle handle(kMatrix);
    repub_book_t* book = handle.book;

    const repub_metadata_t* metadata = repub_book_metadata(book);
    REPUB_CHECK(metadata != nullptr);
    REPUB_CHECK(metadata->title != nullptr);
    REPUB_CHECK_STR_EQ(metadata->title, "Test: Reader Rendering Matrix");
    REPUB_CHECK_STR_EQ(metadata->author, "CrossPoint Test Fixture");
    REPUB_CHECK_STR_EQ(metadata->language, "en");
    REPUB_CHECK_STR_EQ(repub_book_path(book), fixture_path(kMatrix));

    REPUB_CHECK_EQ(repub_book_spine_count(book), 5u);
    const repub_spine_item_t* spine0 = repub_book_spine_item(book, 0);
    REPUB_CHECK(spine0 != nullptr);
    REPUB_CHECK_STR_EQ(spine0->href, "OEBPS/chapter1.xhtml");
    REPUB_CHECK_STR_EQ(spine0->media_type, "application/xhtml+xml");
    REPUB_CHECK_EQ(spine0->linear, 1);
    REPUB_CHECK(spine0->size > 0);
    REPUB_CHECK_EQ(repub_book_spine_item(book, 99), nullptr);

    REPUB_CHECK(repub_book_toc_count(book) >= 5u);
    const repub_toc_item_t* toc0 = repub_book_toc_item(book, 0);
    REPUB_CHECK(toc0 != nullptr);
    REPUB_CHECK_STR_EQ(toc0->title, "How to Use This Fixture");
    REPUB_CHECK(toc0->spine_index >= 0);
    REPUB_CHECK(toc0->href != nullptr);
    REPUB_CHECK(std::strstr(toc0->href, "chapter1.xhtml") != nullptr);
    REPUB_CHECK_EQ(repub_book_toc_item(book, 999), nullptr);

    int32_t spine_index = -1;
    char anchor[64];
    anchor[0] = '\0';
    REPUB_CHECK_EQ(repub_book_resolve_href(book, "chapter3.xhtml#note", &spine_index, anchor,
                                          sizeof(anchor)),
                   REPUB_OK);
    REPUB_CHECK_EQ(spine_index, 2);
    REPUB_CHECK_STR_EQ(anchor, "note");
    REPUB_CHECK_EQ(repub_book_resolve_href(book, "OEBPS/chapter2.xhtml", &spine_index, nullptr, 0),
                   REPUB_OK);
    REPUB_CHECK_EQ(spine_index, 1);
    REPUB_CHECK_EQ(repub_book_resolve_href(book, "missing.xhtml", &spine_index, nullptr, 0),
                   REPUB_ERR_NOT_FOUND);
    REPUB_CHECK_EQ(spine_index, -1);

    /* The matrix fixture has no cover. */
    REPUB_CHECK(repub_book_cover_href(book) == nullptr);

    /* CSS is discovered and loaded. */
    size_t css_size = 0;
    REPUB_CHECK_EQ(repub_book_resource_size(book, "styles/test.css", &css_size), REPUB_OK);
    REPUB_CHECK(css_size > 0);
    void* css_data = nullptr;
    size_t css_len = 0;
    REPUB_CHECK_EQ(repub_book_read_resource(book, "styles/test.css", &css_data, &css_len),
                   REPUB_OK);
    REPUB_CHECK(css_data != nullptr);
    REPUB_CHECK_EQ(css_len, css_size);
    REPUB_CHECK(std::memcmp(css_data, "/*", 2) == 0 || std::memcmp(css_data, "body", 4) == 0 ||
                static_cast<const char*>(css_data)[0] != '\0');
    repub_free(&handle.platform, css_data);

    /* Missing resource. */
    REPUB_CHECK_EQ(repub_book_resource_size(book, "nope.css", &css_size), REPUB_ERR_NOT_FOUND);
}

REPUB_TEST(book_nav_fixture) {
    BookHandle handle(kNavBook);
    repub_book_t* book = handle.book;

    const repub_metadata_t* metadata = repub_book_metadata(book);
    REPUB_CHECK(metadata != nullptr);
    REPUB_CHECK_STR_EQ(metadata->title, "\u6cb3\u8fb9\u8bb0\u4e8b");
    REPUB_CHECK_STR_EQ(metadata->language, "zh-CN");
    REPUB_CHECK_STR_EQ(metadata->identifier, "urn:read-pico:fixture:nav:0");

    REPUB_CHECK_EQ(repub_book_spine_count(book), 4u);
    REPUB_CHECK_EQ(repub_book_toc_count(book), 4u);
    const repub_toc_item_t* toc1 = repub_book_toc_item(book, 1);
    REPUB_CHECK(toc1 != nullptr);
    REPUB_CHECK_STR_EQ(toc1->title, "\u7b2c\u4e8c\u7ae0 \u96e8\u540e\u5c0f\u8def");
    REPUB_CHECK_EQ(toc1->spine_index, 1);
    REPUB_CHECK_STR_EQ(toc1->href, "OEBPS/text/ch2.xhtml#start");

    int32_t spine_index = -1;
    char anchor[32];
    REPUB_CHECK_EQ(repub_book_resolve_href(book, "text/ch3.xhtml#start", &spine_index, anchor,
                                          sizeof(anchor)),
                   REPUB_OK);
    REPUB_CHECK_EQ(spine_index, 2);
    REPUB_CHECK_STR_EQ(anchor, "start");

    REPUB_CHECK_EQ(repub_book_resource_size(book, "text/ch1.xhtml", nullptr),
                   REPUB_ERR_INVALID_ARG);

    /* Skip-TOC option. */
    repub_open_options_t options{};
    options.struct_size = sizeof(options);
    options.flags = REPUB_OPEN_SKIP_TOC;
    BookHandle skipped(kNavBook, &options);
    REPUB_CHECK_EQ(repub_book_spine_count(skipped.book), 4u);
    REPUB_CHECK_EQ(repub_book_toc_count(skipped.book), 0u);
}

REPUB_TEST(book_ncx_fixture) {
    BookHandle handle(kNcxBook);
    repub_book_t* book = handle.book;
    REPUB_CHECK_EQ(repub_book_spine_count(book), 4u);
    REPUB_CHECK_EQ(repub_book_toc_count(book), 4u);
    const repub_toc_item_t* toc3 = repub_book_toc_item(book, 3);
    REPUB_CHECK(toc3 != nullptr);
    REPUB_CHECK_STR_EQ(toc3->title, "\u7b2c\u56db\u7ae0 \u706f\u4e0b\u5f52\u9014");
    REPUB_CHECK_EQ(toc3->spine_index, 3);
    REPUB_CHECK_EQ(toc3->level, 0);
    int32_t spine_index = -1;
    REPUB_CHECK_EQ(repub_book_resolve_href(book, "text/ch4.xhtml#start", &spine_index, nullptr, 0),
                   REPUB_OK);
    REPUB_CHECK_EQ(spine_index, 3);
}

REPUB_TEST(book_image_fixtures) {
    BookHandle png(kPngBook);
    REPUB_CHECK_EQ(repub_book_spine_count(png.book), 10u);
    REPUB_CHECK(repub_book_toc_count(png.book) >= 10u);
    int32_t width = 0;
    int32_t height = 0;
    REPUB_CHECK_EQ(repub_book_image_dims(png.book, "images/png_format.png", &width, &height),
                   REPUB_OK);
    REPUB_CHECK(width > 0);
    REPUB_CHECK(height > 0);
    size_t png_size = 0;
    REPUB_CHECK_EQ(repub_book_resource_size(png.book, "images/png_format.png", &png_size),
                   REPUB_OK);
    REPUB_CHECK(png_size > 8);
    void* data = nullptr;
    size_t size = 0;
    REPUB_CHECK_EQ(
        repub_book_read_resource(png.book, "images/png_format.png", &data, &size), REPUB_OK);
    REPUB_CHECK(data != nullptr);
    REPUB_CHECK_EQ(size, png_size);
    REPUB_CHECK(std::memcmp(data, "\x89PNG", 4) == 0);
    repub_free(&png.platform, data);

    BookHandle jpeg(kJpegBook);
    REPUB_CHECK_EQ(repub_book_spine_count(jpeg.book), 10u);
    REPUB_CHECK_EQ(repub_book_image_dims(jpeg.book, "images/jpeg_format.jpg", &width, &height),
                   REPUB_OK);
    REPUB_CHECK(width > 0);
    REPUB_CHECK(height > 0);
    REPUB_CHECK_EQ(repub_book_image_dims(jpeg.book, "images/nope.png", &width, &height),
                   REPUB_ERR_NOT_FOUND);
}

/* ------------------------------------------------------------------------- */
/* Malformed input must produce errors, never crashes                        */
/* ------------------------------------------------------------------------- */

REPUB_TEST(book_malformed_inputs) {
    repub_platform_t platform = repub_platform_default();
    repub_err_t err = REPUB_OK;

    /* Missing file. */
    repub_book_t* missing = repub_book_open(&platform, "no-such-book.epub", nullptr, &err);
    REPUB_CHECK(missing == nullptr);
    REPUB_CHECK(err == REPUB_ERR_IO || err == REPUB_ERR_NOT_FOUND);

    /* Not a ZIP at all. */
    const std::string garbage_path = "repub_test_garbage.epub";
    {
        std::ofstream out(garbage_path, std::ios::binary | std::ios::trunc);
        out << "this is definitely not a zip archive";
    }
    repub_book_t* garbage = repub_book_open(&platform, garbage_path.c_str(), nullptr, &err);
    REPUB_CHECK(garbage == nullptr);
    REPUB_CHECK(err != REPUB_OK);
    std::remove(garbage_path.c_str());

    /* Truncated (corrupt central directory) ZIP. */
    const std::string full = fixture_bytes(kMatrix);
    REPUB_CHECK(full.size() > 200u);
    const std::string truncated_path = "repub_test_truncated.epub";
    {
        std::ofstream out(truncated_path, std::ios::binary | std::ios::trunc);
        out.write(full.data(), static_cast<std::streamsize>(full.size() / 2));
    }
    repub_book_t* truncated = repub_book_open(&platform, truncated_path.c_str(), nullptr, &err);
    REPUB_CHECK(truncated == nullptr);
    REPUB_CHECK(err != REPUB_OK);
    std::remove(truncated_path.c_str());

    /* Truncated tail only (EOCD destroyed). */
    const std::string tail_path = "repub_test_truncated_tail.epub";
    {
        std::ofstream out(tail_path, std::ios::binary | std::ios::trunc);
        out.write(full.data(), static_cast<std::streamsize>(full.size() - 10));
    }
    repub_book_t* tail = repub_book_open(&platform, tail_path.c_str(), nullptr, &err);
    REPUB_CHECK(tail == nullptr);
    REPUB_CHECK(err != REPUB_OK);
    std::remove(tail_path.c_str());

    /* Null arguments. */
    REPUB_CHECK(repub_book_open(nullptr, nullptr, nullptr, &err) == nullptr);
    REPUB_CHECK(repub_book_metadata(nullptr) == nullptr);
    REPUB_CHECK_EQ(repub_book_spine_count(nullptr), 0u);
    REPUB_CHECK_EQ(repub_book_toc_count(nullptr), 0u);
    REPUB_CHECK(repub_book_spine_item(nullptr, 0) == nullptr);
    REPUB_CHECK(repub_book_resolve_href(nullptr, "x", nullptr, nullptr, 0) ==
                REPUB_ERR_INVALID_ARG);
}
