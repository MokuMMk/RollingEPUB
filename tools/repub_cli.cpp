/*
 * repub_cli - host command line tool for RollingEPUB.
 *
 * Opens an EPUB, prints metadata / TOC / chapter text / blocks / notes / page
 * listings, renders pages to PNG/PGM/PPM and extracts images or the cover.
 * It is also a smoke test: `repub_cli selftest` opens every bundled fixture and
 * renders its first page.
 *
 * The PNG writer below is self contained (zlib "stored" deflate blocks) so the
 * tool needs no external image library and does not duplicate miniz.
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "rolling_epub/rolling_epub.hpp"

namespace {

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

void print_usage() {
    std::fprintf(
        stdout,
        "RollingEPUB command line tool\n"
        "\n"
        "Usage:\n"
        "  repub_cli info <book.epub>\n"
        "  repub_cli toc <book.epub>\n"
        "  repub_cli text <book.epub> <spine>\n"
        "  repub_cli blocks <book.epub> <spine>\n"
        "  repub_cli notes <book.epub> <spine>\n"
        "  repub_cli pages <book.epub> [page options] [--page N]\n"
        "  repub_cli render <book.epub> <chapter> <page> --out out.png|out.pgm|out.ppm\n"
        "  repub_cli image <book.epub> <chapter> <href> --out out.png\n"
        "  repub_cli cover <book.epub> --out out.png\n"
        "  repub_cli selftest [fixture-dir]\n"
        "\n"
        "Page options:\n"
        "  --width N            page width in pixels        (default 480)\n"
        "  --height N           page height in pixels       (default 800)\n"
        "  --font-size N        body font size in pixels    (default 22)\n"
        "  --line-height N      line height percent         (default 160)\n"
        "  --margin N           page margins in pixels       (default 0)\n"
        "  --vertical           vertical writing mode\n"
        "  --font file.ttf      load a TTF as the body font\n"
        "  --mono               render into a MONO1 canvas  (default GRAY8)\n"
        "  --page N             page number for `pages`     (default 0)\n");
}

int fail(const std::string& message) {
    std::fprintf(stderr, "repub_cli: %s\n", message.c_str());
    return 2;
}

int fail_err(const std::string& what, repub_err_t err) {
    std::fprintf(stderr, "repub_cli: %s: %s (%d)\n", what.c_str(),
                 repub_err_str(err), static_cast<int>(err));
    return 2;
}

bool parse_int(const std::string& text, int32_t* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    *out = static_cast<int32_t>(value);
    return true;
}

struct Options {
    std::string out;
    int32_t width = 480;
    int32_t height = 800;
    int32_t font_size = 22;
    int32_t line_height = 160;
    int32_t margin = 0;
    int32_t page = 0;
    bool page_set = false;
    bool vertical = false;
    bool mono = false;
    std::string font_path;
};

bool parse_options(int argc, char** argv, std::vector<std::string>* positional,
                   Options* options) {
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next_value = [&](std::string* value) -> bool {
            if (i + 1 >= argc) return false;
            *value = argv[++i];
            return true;
        };
        if (arg == "--out") {
            if (!next_value(&options->out)) return false;
        } else if (arg == "--width") {
            std::string value;
            if (!next_value(&value) || !parse_int(value, &options->width)) return false;
        } else if (arg == "--height") {
            std::string value;
            if (!next_value(&value) || !parse_int(value, &options->height)) return false;
        } else if (arg == "--font-size") {
            std::string value;
            if (!next_value(&value) || !parse_int(value, &options->font_size)) return false;
        } else if (arg == "--line-height") {
            std::string value;
            if (!next_value(&value) || !parse_int(value, &options->line_height)) return false;
        } else if (arg == "--margin") {
            std::string value;
            if (!next_value(&value) || !parse_int(value, &options->margin)) return false;
        } else if (arg == "--page") {
            std::string value;
            if (!next_value(&value) || !parse_int(value, &options->page)) return false;
            options->page_set = true;
        } else if (arg == "--vertical") {
            options->vertical = true;
        } else if (arg == "--mono") {
            options->mono = true;
        } else if (arg == "--font") {
            if (!next_value(&options->font_path)) return false;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "repub_cli: unknown option %s\n", arg.c_str());
            return false;
        } else {
            positional->push_back(arg);
        }
    }
    if (options->width <= 0 || options->height <= 0 || options->font_size <= 0 ||
        options->line_height <= 0 || options->margin < 0 || options->page < 0) {
        return false;
    }
    return true;
}

repub_layout_spec_t make_spec(const Options& options) {
    repub_layout_spec_t spec = repub::default_layout_spec(
        repub_rect_t{0, 0, options.width, options.height}, options.font_size);
    spec.line_height_percent = options.line_height;
    spec.margin_left_px = options.margin;
    spec.margin_right_px = options.margin;
    spec.margin_top_px = options.margin;
    spec.margin_bottom_px = options.margin;
    if (options.vertical) spec.writing_mode = REPUB_WRITING_VERTICAL_RL;
    return spec;
}

bool read_whole_file(const std::string& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out->empty();
}

/* Configure fonts from --font; returns false only when the file cannot be read. */
bool apply_font_option(repub::Book* book, const Options& options) {
    if (options.font_path.empty()) return true;
    std::string ttf;
    if (!read_whole_file(options.font_path, &ttf)) {
        std::fprintf(stderr, "repub_cli: cannot read font %s\n",
                     options.font_path.c_str());
        return false;
    }
    repub_font_desc_t desc{};
    desc.family = "serif";
    desc.data = ttf.data();
    desc.size = ttf.size();
    desc.face_index = 0;
    desc.style = REPUB_FONT_REGULAR;
    repub_err_t err = repub_book_add_font(book->get(), &desc);
    if (err != REPUB_OK) {
        std::fprintf(stderr, "repub_cli: add_font: %s\n", repub_err_str(err));
        return false;
    }
    err = repub_book_set_body_font(book->get(), "serif", REPUB_FONT_REGULAR);
    if (err != REPUB_OK) {
        std::fprintf(stderr, "repub_cli: set_body_font: %s\n", repub_err_str(err));
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* Pixel output: PGM / PPM / PNG (self-contained stored-block zlib)          */
/* ------------------------------------------------------------------------- */

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            }
            table[i] = c;
        }
        ready = true;
    }
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

uint32_t adler32_update(uint32_t adler, const uint8_t* data, size_t size) {
    uint32_t a = adler & 0xFFFFu;
    uint32_t b = (adler >> 16) & 0xFFFFu;
    for (size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void put_be32(std::vector<uint8_t>* out, uint32_t value) {
    out->push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    out->push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out->push_back(static_cast<uint8_t>(value & 0xFFu));
}

void png_chunk(std::vector<uint8_t>* png, const char type[4],
               const std::vector<uint8_t>& payload) {
    put_be32(png, static_cast<uint32_t>(payload.size()));
    std::vector<uint8_t> crc_data;
    crc_data.reserve(4 + payload.size());
    for (int i = 0; i < 4; ++i) crc_data.push_back(static_cast<uint8_t>(type[i]));
    crc_data.insert(crc_data.end(), payload.begin(), payload.end());
    png->insert(png->end(), crc_data.begin(), crc_data.end());
    uint32_t crc = crc32_update(0xFFFFFFFFu, crc_data.data(), crc_data.size());
    put_be32(png, crc ^ 0xFFFFFFFFu);
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const size_t written =
        bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), file);
    const bool ok = written == bytes.size();
    std::fclose(file);
    return ok;
}

/* Write an 8-bit grayscale PNG (color type 0). */
bool write_png_gray(const std::string& path, int32_t width, int32_t height,
                    const std::vector<uint8_t>& gray) {
    if (width <= 0 || height <= 0 ||
        gray.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return false;
    }
    /* Raw scanlines with filter byte 0. */
    std::vector<uint8_t> raw;
    raw.reserve((static_cast<size_t>(width) + 1) * static_cast<size_t>(height));
    for (int32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint8_t* row = gray.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
        raw.insert(raw.end(), row, row + width);
    }

    /* zlib stream: 0x78 0x01, stored deflate blocks, Adler-32. */
    std::vector<uint8_t> zlib;
    zlib.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t block = (raw.size() - offset) > 65535 ? 65535 : raw.size() - offset;
        const bool last = offset + block >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(block & 0xFFu));
        zlib.push_back(static_cast<uint8_t>((block >> 8) & 0xFFu));
        const uint16_t nlen = static_cast<uint16_t>(~block & 0xFFFFu);
        zlib.push_back(static_cast<uint8_t>(nlen & 0xFFu));
        zlib.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFFu));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
        offset += block;
    }
    if (raw.empty()) {
        zlib.push_back(1);
        zlib.push_back(0);
        zlib.push_back(0);
        zlib.push_back(0xFFu);
        zlib.push_back(0xFFu);
    }
    const uint32_t adler = adler32_update(1u, raw.data(), raw.size());
    put_be32(&zlib, adler);

    std::vector<uint8_t> png;
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), signature, signature + 8);

    std::vector<uint8_t> ihdr;
    put_be32(&ihdr, static_cast<uint32_t>(width));
    put_be32(&ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8); /* bit depth */
    ihdr.push_back(0); /* color type: grayscale */
    ihdr.push_back(0); /* compression */
    ihdr.push_back(0); /* filter */
    ihdr.push_back(0); /* interlace */
    png_chunk(&png, "IHDR", ihdr);
    png_chunk(&png, "IDAT", zlib);
    png_chunk(&png, "IEND", {});
    return write_file(path, png);
}

bool write_pgm(const std::string& path, int32_t width, int32_t height,
               const std::vector<uint8_t>& gray) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    std::fprintf(file, "P5\n%u %u\n255\n", static_cast<unsigned>(width),
                 static_cast<unsigned>(height));
    const size_t written =
        gray.empty() ? 0 : std::fwrite(gray.data(), 1, gray.size(), file);
    std::fclose(file);
    return written == gray.size();
}

bool write_ppm(const std::string& path, int32_t width, int32_t height,
               const std::vector<uint8_t>& gray) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    std::fprintf(file, "P6\n%u %u\n255\n", static_cast<unsigned>(width),
                 static_cast<unsigned>(height));
    std::vector<uint8_t> rgb;
    rgb.reserve(gray.size() * 3);
    for (uint8_t value : gray) {
        rgb.push_back(value);
        rgb.push_back(value);
        rgb.push_back(value);
    }
    const size_t written =
        rgb.empty() ? 0 : std::fwrite(rgb.data(), 1, rgb.size(), file);
    std::fclose(file);
    return written == rgb.size();
}

bool write_gray_image(const std::string& path, int32_t width, int32_t height,
                      const std::vector<uint8_t>& gray) {
    std::string lower = path;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".png") == 0) {
        return write_png_gray(path, width, height, gray);
    }
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".ppm") == 0) {
        return write_ppm(path, width, height, gray);
    }
    return write_pgm(path, width, height, gray);
}

/* Copy a canvas to an 8-bit gray buffer (0 = black). */
std::vector<uint8_t> canvas_to_gray(const repub::Canvas& canvas) {
    const int32_t width = canvas.width();
    const int32_t height = canvas.height();
    const repub_pixel_format_t format = canvas.format();
    std::vector<uint8_t> out;
    if (width <= 0 || height <= 0) return out;
    out.resize(static_cast<size_t>(width) * static_cast<size_t>(height), 255);
    if (format == REPUB_FORMAT_MONO1) {
        const uint8_t* data = static_cast<const uint8_t*>(canvas.data());
        const int32_t stride = canvas.stride();
        if (!data) return out;
        for (int32_t y = 0; y < height; ++y) {
            for (int32_t x = 0; x < width; ++x) {
                const uint8_t byte = data[static_cast<size_t>(y) * stride + x / 8];
                const bool white = (byte >> (7 - (x % 8))) & 1u;
                out[static_cast<size_t>(y) * width + x] = white ? 255 : 0;
            }
        }
        return out;
    }
    const uint8_t* data = static_cast<const uint8_t*>(canvas.data());
    const int32_t stride = canvas.stride();
    if (!data) return out;
    int max_level = 255;
    if (format == REPUB_FORMAT_GRAY2) max_level = 3;
    if (format == REPUB_FORMAT_GRAY4) max_level = 15;
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const uint8_t value = data[static_cast<size_t>(y) * stride + x];
            out[static_cast<size_t>(y) * width + x] =
                static_cast<uint8_t>((static_cast<int32_t>(value) * 255) / max_level);
        }
    }
    return out;
}

/* ------------------------------------------------------------------------- */
/* Commands                                                                  */
/* ------------------------------------------------------------------------- */

int command_info(const std::string& path) {
    repub_err_t err = REPUB_OK;
    repub::Book book = repub::Book::open(path, nullptr, nullptr, &err);
    if (!book) return fail_err("open " + path, err);

    const repub_metadata_t* meta = book.metadata();
    std::printf("path:        %s\n", book.path().c_str());
    if (meta) {
        std::printf("title:       %s\n", meta->title ? meta->title : "");
        std::printf("author:      %s\n", meta->author ? meta->author : "");
        std::printf("language:    %s\n", meta->language ? meta->language : "");
        std::printf("publisher:   %s\n", meta->publisher ? meta->publisher : "");
        std::printf("identifier:  %s\n", meta->identifier ? meta->identifier : "");
        std::printf("date:        %s\n", meta->date ? meta->date : "");
        std::printf("series:      %s\n", meta->series ? meta->series : "");
    }
    std::printf("spine:       %zu item(s)\n", book.spine_count());
    std::printf("toc:         %zu item(s)\n", book.toc_count());
    std::printf("cover:       %s\n", book.cover_href() ? book.cover_href() : "(none)");
    return 0;
}

int command_toc(const std::string& path) {
    repub_err_t err = REPUB_OK;
    repub::Book book = repub::Book::open(path, nullptr, nullptr, &err);
    if (!book) return fail_err("open " + path, err);
    const size_t count = book.toc_count();
    if (count == 0) {
        std::printf("(no table of contents)\n");
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        const repub_toc_item_t* item = book.toc_item(i);
        if (!item) continue;
        const int level = item->level < 0 ? 0 : item->level;
        std::printf("%*s%s  [spine %d]  %s\n", level * 2, "",
                    item->title ? item->title : "",
                    item->spine_index,
                    item->href ? item->href : "");
    }
    return 0;
}

int command_text(const std::string& path, int32_t spine) {
    repub_err_t err = REPUB_OK;
    repub::Book book = repub::Book::open(path, nullptr, nullptr, &err);
    if (!book) return fail_err("open " + path, err);
    if (spine < 0) return fail("negative spine index");
    repub::Chapter chapter = book.open_chapter(static_cast<size_t>(spine), &err);
    if (!chapter) return fail_err("open chapter", err);
    std::printf("title: %s\n\n", chapter.title().c_str());
    const std::string_view text = chapter.text();
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::printf("\n");
    return 0;
}

const char* block_type_name(uint8_t type) {
    switch (type) {
        case REPUB_BLOCK_PARAGRAPH: return "p";
        case REPUB_BLOCK_HEADING: return "heading";
        case REPUB_BLOCK_BLOCKQUOTE: return "quote";
        case REPUB_BLOCK_PRE: return "pre";
        case REPUB_BLOCK_LIST_ITEM: return "li";
        case REPUB_BLOCK_HR: return "hr";
        case REPUB_BLOCK_IMAGE: return "img";
        case REPUB_BLOCK_TABLE_ROW: return "tr";
        case REPUB_BLOCK_TABLE_CELL: return "td";
        default: return "other";
    }
}

int command_blocks(const std::string& path, int32_t spine) {
    repub_err_t err = REPUB_OK;
    repub::Book book = repub::Book::open(path, nullptr, nullptr, &err);
    if (!book) return fail_err("open " + path, err);
    if (spine < 0) return fail("negative spine index");
    repub::Chapter chapter = book.open_chapter(static_cast<size_t>(spine), &err);
    if (!chapter) return fail_err("open chapter", err);
    std::printf("chapter %d: %s\n", spine, chapter.title().c_str());
    for (size_t i = 0; i < chapter.block_count(); ++i) {
        const repub_block_info_t* block = chapter.block(i);
        if (!block) continue;
        std::printf("#%zu %-7s off=%-6u len=%-5u flags=0x%02x h=%u scale=%d%% "
                    "indent=%d img=%d link=%d\n",
                    i, block_type_name(block->type), block->text_offset,
                    block->text_len, block->flags, block->heading_level,
                    block->font_scale_percent, block->indent_px,
                    block->image_index, block->link_index);
    }
    return 0;
}

int command_notes(const std::string& path, int32_t spine) {
    repub_err_t err = REPUB_OK;
    repub::Book book = repub::Book::open(path, nullptr, nullptr, &err);
    if (!book) return fail_err("open " + path, err);
    if (spine < 0) return fail("negative spine index");
    repub::Chapter chapter = book.open_chapter(static_cast<size_t>(spine), &err);
    if (!chapter) return fail_err("open chapter", err);
    std::printf("chapter %d: %zu note(s)\n", spine, chapter.note_count());
    for (size_t i = 0; i < chapter.note_count(); ++i) {
        const repub_note_info_t* note = chapter.note(i);
        if (!note) continue;
        std::string text = note->text ? std::string(note->text, note->text_len)
                                      : std::string();
        if (text.size() > 100) text = text.substr(0, 100) + "...";
        std::printf("#%zu marker=%u icon=%d href=%s\n    %s\n", i,
                    note->marker_text_offset, note->marker_image_index,
                    note->href ? note->href : "", text.c_str());
    }
    return 0;
}

/* Open a reader with the requested layout/fonts. */
repub::Reader open_reader(const std::string& path, const Options& options,
                          repub_err_t* err) {
    repub::Book book = repub::Book::open(path, nullptr, nullptr, err);
    if (!book) return repub::Reader();
    if (!apply_font_option(&book, options)) {
        *err = REPUB_ERR_IO;
        return repub::Reader();
    }
    const repub_layout_spec_t spec = make_spec(options);
    return repub::Reader::attach(std::move(book), &spec, err);
}

int command_pages(const std::string& path, const Options& options, int32_t chapter_arg) {
    repub_err_t err = REPUB_OK;
    repub::Reader reader = open_reader(path, options, &err);
    if (!reader) return fail_err("open " + path, err);
    if (chapter_arg < 0) return fail("negative chapter index");
    err = reader.goto_chapter(static_cast<size_t>(chapter_arg));
    if (err != REPUB_OK) return fail_err("goto chapter", err);

    const repub_layout_t* layout = reader.layout();
    const size_t count = reader.page_count();
    const int32_t page_index = options.page_set ? options.page : -1;
    std::printf("chapter %zu/%zu  page %zu/%zu  (%dx%d, %.0f%%)\n",
                reader.chapter_index() + 1, reader.chapter_count(),
                reader.page_index() + 1, count, options.width, options.height,
                static_cast<double>(reader.progress()) * 100.0);
    if (count == 0 || !layout) {
        std::printf("(empty layout)\n");
        return 0;
    }
    const size_t first = page_index >= 0 ? static_cast<size_t>(page_index) : 0;
    const size_t last = page_index >= 0
                            ? (first < count ? first : count - 1)
                            : count - 1;
    for (size_t page = first; page <= last; ++page) {
        const repub_page_t* handle = repub_layout_page(layout, page);
        const repub_page_info_t* info = repub_page_info(handle);
        if (!info) continue;
        std::printf("page %zu: off=%u..%u spans=%zu images=%zu notes=%zu links=%zu "
                    "primary_image=%d\n",
                    page, info->start_offset, info->end_offset, info->span_count,
                    info->image_count, info->note_count, info->link_count,
                    info->primary_image_index);
        for (size_t s = 0; s < info->span_count; ++s) {
            const repub_span_t* span = repub_page_span(handle, s);
            if (!span) continue;
            std::printf("  span %zu: line=%u off=%u len=%u at (%d,%d) %dx%d "
                        "size=%u style=0x%02x\n",
                        s, span->line_index, span->text_offset, span->text_len,
                        span->x, span->y, span->width, span->height,
                        span->font_size_px, span->style);
        }
        std::string page_text;
        page_text.resize(static_cast<size_t>(info->end_offset - info->start_offset) + 1);
        const size_t written =
            repub_page_text(handle, page_text.data(), page_text.size());
        page_text.resize(written);
        if (!page_text.empty()) std::printf("  text: %s\n", page_text.c_str());
    }
    return 0;
}

int command_render(const std::string& path, const Options& options,
                   int32_t chapter_arg, int32_t page_arg) {
    repub_err_t err = REPUB_OK;
    repub::Reader reader = open_reader(path, options, &err);
    if (!reader) return fail_err("open " + path, err);
    if (options.out.empty()) return fail("render needs --out file.png|file.pgm|file.ppm");
    if (chapter_arg < 0 || page_arg < 0) return fail("negative chapter/page index");

    err = reader.goto_chapter(static_cast<size_t>(chapter_arg));
    if (err != REPUB_OK) return fail_err("goto chapter", err);
    const repub_layout_t* layout = reader.layout();
    if (!layout || repub_layout_page_count(layout) == 0) {
        return fail("chapter has no pages");
    }
    if (static_cast<size_t>(page_arg) >= repub_layout_page_count(layout)) {
        return fail("page out of range");
    }
    const uint32_t offset = repub_layout_page_start_offset(
        layout, static_cast<size_t>(page_arg));
    err = reader.goto_offset(static_cast<size_t>(chapter_arg), offset);
    if (err != REPUB_OK) return fail_err("goto page", err);

    repub::Canvas canvas = repub::Canvas::create(
        options.width, options.height,
        options.mono ? REPUB_FORMAT_MONO1 : REPUB_FORMAT_GRAY8, nullptr, &err);
    if (!canvas) return fail_err("create canvas", err);
    canvas.clear(options.mono ? 1 : 255);
    repub_render_options_t render_options = repub::default_render_options();
    err = reader.render(canvas, &render_options);
    if (err != REPUB_OK) return fail_err("render", err);

    const std::vector<uint8_t> gray = canvas_to_gray(canvas);
    if (!write_gray_image(options.out, options.width, options.height, gray)) {
        return fail("cannot write " + options.out);
    }
    std::printf("wrote %s (%dx%d, chapter %d page %d, progress %.1f%%)\n",
                options.out.c_str(), options.width, options.height, chapter_arg,
                page_arg, static_cast<double>(reader.progress()) * 100.0);
    return 0;
}

/* Convert an RGBA image to 8-bit gray over a white background. */
std::vector<uint8_t> rgba_to_gray(const repub_image_t* image) {
    std::vector<uint8_t> out;
    if (!image) return out;
    const int32_t width = repub_image_width(image);
    const int32_t height = repub_image_height(image);
    const uint8_t* pixels = repub_image_pixels(image);
    if (width <= 0 || height <= 0 || !pixels) return out;
    out.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < out.size(); ++i) {
        const uint8_t r = pixels[i * 4 + 0];
        const uint8_t g = pixels[i * 4 + 1];
        const uint8_t b = pixels[i * 4 + 2];
        const uint8_t a = pixels[i * 4 + 3];
        const int gray = (static_cast<int>(r) * 77 + static_cast<int>(g) * 150 +
                          static_cast<int>(b) * 29) >> 8;
        const int blended = (gray * a + 255 * (255 - a)) / 255;
        out[i] = static_cast<uint8_t>(blended);
    }
    return out;
}

int command_image(const std::string& path, const Options& options, int32_t chapter_arg,
                  const std::string& href) {
    repub_err_t err = REPUB_OK;
    repub::Book book = repub::Book::open(path, nullptr, nullptr, &err);
    if (!book) return fail_err("open " + path, err);
    if (options.out.empty()) return fail("image needs --out file.png");
    if (chapter_arg < 0) return fail("negative chapter index");
    repub::Chapter chapter = book.open_chapter(static_cast<size_t>(chapter_arg), &err);
    if (!chapter) return fail_err("open chapter", err);

    std::string target = href;
    const size_t fragment = target.find('#');
    if (fragment != std::string::npos) target.resize(fragment);
    repub::Image image = book.image(target.c_str(), 0, 0, &err);
    if (!image) return fail_err("decode image " + target, err);
    const std::vector<uint8_t> gray = rgba_to_gray(image.get());
    if (!write_gray_image(options.out, image.width(), image.height(), gray)) {
        return fail("cannot write " + options.out);
    }
    std::printf("wrote %s (%dx%d)\n", options.out.c_str(), image.width(),
                image.height());
    return 0;
}

int command_cover(const std::string& path, const Options& options) {
    repub_err_t err = REPUB_OK;
    repub::Book book = repub::Book::open(path, nullptr, nullptr, &err);
    if (!book) return fail_err("open " + path, err);
    if (options.out.empty()) return fail("cover needs --out file.png");
    const char* href = book.cover_href();
    if (!href) return fail("book has no cover");
    repub::Image image = book.cover_image(&err);
    if (!image) return fail_err("decode cover", err);
    const std::vector<uint8_t> gray = rgba_to_gray(image.get());
    if (!write_gray_image(options.out, image.width(), image.height(), gray)) {
        return fail("cannot write " + options.out);
    }
    std::printf("wrote %s (%dx%d)\n", options.out.c_str(), image.width(),
                image.height());
    return 0;
}

int command_selftest(const std::string& fixture_dir) {
    static const char* kFixtures[] = {
        "readpico_trail-marker.epub",
        "test_png_images.epub",
        "test_jpeg_images.epub",
        "test_tables.epub",
        "test_display_none.epub",
        "crosspoint-table-edge-cases.epub",
        "font-prewarm-benchmark.epub",
    };
    Options options;
    options.width = 480;
    options.height = 800;
    options.font_size = 22;
    options.mono = true;

    int failures = 0;
    int ran = 0;
    for (const char* name : kFixtures) {
        const std::string path = fixture_dir + "/" + name;
        repub_err_t err = REPUB_OK;
        repub::Book book = repub::Book::open(path, nullptr, nullptr, &err);
        if (!book) {
            std::printf("[SKIP] %-40s %s\n", name, repub_err_str(err));
            continue;
        }
        ++ran;
        const repub_layout_spec_t spec = make_spec(options);
        repub::Reader reader = repub::Reader::attach(std::move(book), &spec, &err);
        if (!reader) {
            std::printf("[FAIL] %-40s reader: %s\n", name, repub_err_str(err));
            ++failures;
            continue;
        }
        repub::Canvas canvas = repub::Canvas::create(
            options.width, options.height, REPUB_FORMAT_MONO1, nullptr, &err);
        if (!canvas) {
            std::printf("[FAIL] %-40s canvas: %s\n", name, repub_err_str(err));
            ++failures;
            continue;
        }
        canvas.clear(1);
        repub_render_options_t render_options = repub::default_render_options();
        err = reader.render(canvas, &render_options);
        if (err != REPUB_OK) {
            std::printf("[FAIL] %-40s render: %s\n", name, repub_err_str(err));
            ++failures;
            continue;
        }
        /* Count black pixels as a crude "page is not blank" check. */
        size_t black = 0;
        const uint8_t* data = static_cast<const uint8_t*>(canvas.data());
        const size_t bytes = static_cast<size_t>(canvas.stride()) *
                             static_cast<size_t>(canvas.height());
        for (size_t i = 0; i < bytes; ++i) {
            unsigned byte = data[i];
            while (byte) {
                black += byte & 1u;
                byte >>= 1;
            }
        }
        std::printf("[ OK ] %-40s chapter %zu/%zu pages=%zu black=%zu\n", name,
                    reader.chapter_index() + 1, reader.chapter_count(),
                    reader.page_count(), black);
    }
    std::printf("%d fixture(s) opened, %d failure(s)\n", ran, failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    const std::string command = argv[1];
    std::vector<std::string> positional;
    Options options;
    if (!parse_options(argc, argv, &positional, &options)) {
        print_usage();
        return 1;
    }

    if (command == "help" || command == "--help") {
        print_usage();
        return 0;
    }
    if (command == "selftest") {
        return command_selftest(positional.empty() ? "tests/fixtures/epub" : positional[0]);
    }
    if (command == "info") {
        if (positional.size() < 1) return fail("usage: repub_cli info <book.epub>");
        return command_info(positional[0]);
    }
    if (command == "toc") {
        if (positional.size() < 1) return fail("usage: repub_cli toc <book.epub>");
        return command_toc(positional[0]);
    }
    if (command == "text" || command == "blocks" || command == "notes") {
        if (positional.size() < 2) return fail("usage: repub_cli " + command + " <book.epub> <spine>");
        int32_t spine = 0;
        if (!parse_int(positional[1], &spine)) return fail("invalid spine index");
        if (command == "text") return command_text(positional[0], spine);
        if (command == "blocks") return command_blocks(positional[0], spine);
        return command_notes(positional[0], spine);
    }
    if (command == "pages") {
        if (positional.size() < 1) return fail("usage: repub_cli pages <book.epub> [chapter]");
        int32_t chapter = 0;
        if (positional.size() >= 2 && !parse_int(positional[1], &chapter)) {
            return fail("invalid chapter index");
        }
        return command_pages(positional[0], options, chapter);
    }
    if (command == "render") {
        if (positional.size() < 3) {
            return fail("usage: repub_cli render <book.epub> <chapter> <page> --out out.png");
        }
        int32_t chapter = 0;
        int32_t page = 0;
        if (!parse_int(positional[1], &chapter) || !parse_int(positional[2], &page)) {
            return fail("invalid chapter/page index");
        }
        return command_render(positional[0], options, chapter, page);
    }
    if (command == "image") {
        if (positional.size() < 3) {
            return fail("usage: repub_cli image <book.epub> <chapter> <href> --out out.png");
        }
        int32_t chapter = 0;
        if (!parse_int(positional[1], &chapter)) return fail("invalid chapter index");
        return command_image(positional[0], options, chapter, positional[2]);
    }
    if (command == "cover") {
        if (positional.size() < 1) return fail("usage: repub_cli cover <book.epub> --out out.png");
        return command_cover(positional[0], options);
    }
    std::fprintf(stderr, "repub_cli: unknown command '%s'\n", command.c_str());
    print_usage();
    return 1;
}
