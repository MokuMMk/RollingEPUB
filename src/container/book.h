/*
 * RollingEPUB book container: ZIP + OPF/NCX/NAV + metadata + fonts.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_CONTAINER_BOOK_H
#define REPUB_SRC_CONTAINER_BOOK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "container/zip.h"
#include "content/css.h"
#include "content/flow.h"
#include "core/base.h"
#include "render/font.h"

namespace repub {

struct ManifestItem {
    std::string id;
    std::string href; /* package-relative, normalized */
    std::string media_type;
    std::string properties;
};

struct SpineItem {
    std::string idref;
    std::string href;
    std::string media_type;
    std::string id;
    uint32_t size = 0;
    bool linear = true;
};

struct TocEntry {
    std::string title;
    std::string href;    /* package-relative, may include '#fragment' */
    std::string anchor;  /* fragment without '#' */
    int level = 0;
    int32_t spine_index = -1;
};

struct EmbeddedFontRef {
    std::string family;
    std::string href; /* package-relative */
    int weight = 400;
    int italic = 0;
};

/* Public opaque type. */
struct repub_book {
    const repub_platform_t* platform = nullptr;
    std::string path;
    ZipArchive zip;

    std::string opf_path; /* package-relative */
    std::string opf_dir;  /* package-relative directory of the OPF */

    /* Metadata storage; repub_metadata_t points into these strings. */
    std::string title;
    std::string author;
    std::string language;
    std::string publisher;
    std::string identifier;
    std::string date;
    std::string description;
    std::string series;
    float series_index = -1.0f;
    repub_metadata_t metadata{};

    std::vector<ManifestItem> manifest;
    std::vector<SpineItem> spine;
    std::vector<TocEntry> toc;
    std::vector<std::string> css_hrefs;
    std::vector<EmbeddedFontRef> embedded_fonts;

    /* Discovered navigation documents (package-relative). */
    std::string nav_href;
    std::string ncx_href;

    /* Public C API mirrors.  Their const char* fields point into the
     * corresponding SpineItem/TocEntry strings above, which are stable after
     * book_open/book_load_toc completes.  Rebuilt whenever spine/TOC change. */
    std::vector<repub_spine_item_t> public_spine;
    std::vector<repub_toc_item_t> public_toc;

    CssSheet css;
    std::unique_ptr<FontManager> fonts;

    std::string cover_href;
    std::string body_font;
    std::string user_css;
    repub_open_options_t options{};

    bool toc_loaded = false;
    bool css_loaded = false;
    bool embedded_fonts_enabled = true;
    bool opened = false;
    bool protected_entries = false;
    std::string protection_error;
    size_t content_bytes = 0; /* sum of spine HTML sizes for progress */

    /* Find a manifest item by id, or nullptr. */
    const ManifestItem* manifest_by_id(std::string_view id) const;
    /* Spine index for a package-relative href (fragment stripped), or -1. */
    int32_t spine_index_for_href(std::string_view href) const;
};

repub_err_t book_open(repub_book* book, const repub_platform_t* platform,
                      const char* path, const repub_open_options_t* options);
void book_close(repub_book* book);

repub_err_t book_load_css(repub_book* book);
repub_err_t book_load_toc(repub_book* book);
/* Load the book's embedded TTF files through @font-face when enabled. */
repub_err_t book_load_embedded_fonts(repub_book* book);

repub_err_t book_read_resource(const repub_book& book, std::string_view href,
                               std::vector<uint8_t>* out, size_t max_bytes = 0);
repub_err_t book_read_resource_string(const repub_book& book, std::string_view href,
                                      std::string* out, size_t max_bytes = 0);
repub_err_t book_image_dims(const repub_book& book, std::string_view href,
                            int32_t* width, int32_t* height);

/* Open and parse one spine item into a chapter. */
repub_err_t book_chapter_open(repub_book* book, size_t spine_index,
                              const ParseOptions& options,
                              repub_chapter** out);

/* Title for a spine index from the TOC, or from the chapter's first heading. */
std::string book_toc_title_for_spine(const repub_book& book, size_t spine_index);
/* Whole-book progress weight of a spine item in bytes. */
uint32_t book_spine_byte_offset(const repub_book& book, size_t spine_index);

repub_err_t book_resolve_href(const repub_book& book, std::string_view href,
                              int32_t* spine_index, std::string* anchor);

}  // namespace repub

#endif  // REPUB_SRC_CONTAINER_BOOK_H
