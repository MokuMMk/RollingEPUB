/*
 * RollingEPUB - portable EPUB parsing, layout and rendering library.
 *
 * SPDX-License-Identifier: MIT
 *
 * Public C API.  The library merges the EPUB engines of CrossSIGO/CrossMux
 * (MIT) and Read Pico/kiikoread (Apache-2.0).  See LICENSE and NOTICE.
 *
 * Design goals
 * ------------
 *  - One dependency-free library for embedded and desktop readers.
 *  - The application never parses container/OPF/NCX/NAV/HTML/CSS, never
 *    paginates text and never rasterizes glyphs itself: it opens a book,
 *    configures a layout, renders pages into its own framebuffer.
 *  - Built-in convenience features: cover images, TOC, footnotes with popup
 *    layouts, image decoding, scaling and pan/zoom viewing, dithering for
 *    e-ink panels, byte-accurate reading progress.
 *  - Optional compile-time features (see repub_config.h) so a small device
 *    can drop TTF, hyphenation, vertical text, image decoding, ...
 *
 * Threading: a book/reader/layout object must not be used concurrently.
 * Separate reader instances may be used from separate tasks.
 * Strings returned by the API are owned by the object and remain valid until
 * the object is closed, unless explicitly documented otherwise.
 */
#ifndef ROLLING_EPUB_REPUB_H
#define ROLLING_EPUB_REPUB_H

#include <stddef.h>
#include <stdint.h>

#include "rolling_epub/repub_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Version and build information                                             */
/* ------------------------------------------------------------------------- */

#define REPUB_VERSION_MAJOR 1
#define REPUB_VERSION_MINOR 0
#define REPUB_VERSION_PATCH 0
#define REPUB_VERSION_STRING "1.0.0"
#define REPUB_ABI_VERSION 1u

/* Feature bits returned by repub_build_info()/repub_feature_enabled(). */
enum repub_feature {
    REPUB_FEATURE_TOC = 1u << 0,
    REPUB_FEATURE_COVER = 1u << 1,
    REPUB_FEATURE_CSS = 1u << 2,
    REPUB_FEATURE_FOOTNOTES = 1u << 3,
    REPUB_FEATURE_TABLES = 1u << 4,
    REPUB_FEATURE_DROP_CAPS = 1u << 5,
    REPUB_FEATURE_RUBY = 1u << 6,
    REPUB_FEATURE_HYPHENATION = 1u << 7,
    REPUB_FEATURE_VERTICAL_TEXT = 1u << 8,
    REPUB_FEATURE_BIDI = 1u << 9,
    REPUB_FEATURE_TTF = 1u << 10,
    REPUB_FEATURE_BUILTIN_FONT = 1u << 11,
    REPUB_FEATURE_ANTIALIASING = 1u << 12,
    REPUB_FEATURE_DITHERING = 1u << 13,
    REPUB_FEATURE_GRAYSCALE = 1u << 14,
    REPUB_FEATURE_IMAGES = 1u << 15,
    REPUB_FEATURE_IMAGE_ZOOM = 1u << 16,
    REPUB_FEATURE_GIF = 1u << 17,
    REPUB_FEATURE_WEBP = 1u << 18,
    REPUB_FEATURE_CACHE = 1u << 19,
    REPUB_FEATURE_PROGRESS = 1u << 20,
    REPUB_FEATURE_ENCRYPTION_PROBE = 1u << 21
};

typedef struct repub_build_info {
    uint32_t abi_version;
    uint32_t features;      /* bitmask of enum repub_feature */
    const char *version;    /* "1.0.0" */
    const char *platform;   /* compiler / build description */
} repub_build_info_t;

/* Returns static build information. Never NULL. */
const repub_build_info_t *repub_build_info(void);
/* 1 when a feature is compiled in, 0 otherwise. */
int repub_feature_enabled(uint32_t feature_bit);

/* ------------------------------------------------------------------------- */
/* Errors                                                                    */
/* ------------------------------------------------------------------------- */

typedef enum repub_err {
    REPUB_OK = 0,
    REPUB_ERR_IO = -1,
    REPUB_ERR_FORMAT = -2,       /* malformed container/XML/HTML */
    REPUB_ERR_MEMORY = -3,
    REPUB_ERR_UNSUPPORTED = -4,  /* feature compiled out or format unsupported */
    REPUB_ERR_NOT_FOUND = -5,
    REPUB_ERR_INVALID_ARG = -6,
    REPUB_ERR_BUFFER_SMALL = -7,
    REPUB_ERR_CANCELLED = -8,
    REPUB_ERR_LIMIT = -9,        /* configured bound exceeded */
    REPUB_ERR_PROTECTED = -10,   /* encrypted entry without a decryptor */
    REPUB_ERR_INTERNAL = -11
} repub_err_t;

const char *repub_err_str(repub_err_t err);

/* ------------------------------------------------------------------------- */
/* Geometry and pixels                                                       */
/* ------------------------------------------------------------------------- */

typedef struct repub_rect {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} repub_rect_t;

/*
 * Canvas pixel formats.
 *
 * MONO1  : 1 bit per pixel, MSB first, 0 = black, 1 = white.
 * GRAY2  : 1 byte per pixel, value 0..3 (0 = black).
 * GRAY4  : 1 byte per pixel, value 0..15 (0 = black).
 * GRAY8  : 1 byte per pixel, value 0..255 (0 = black).
 */
typedef enum repub_pixel_format {
    REPUB_FORMAT_MONO1 = 0,
    REPUB_FORMAT_GRAY2 = 1,
    REPUB_FORMAT_GRAY4 = 2,
    REPUB_FORMAT_GRAY8 = 3
} repub_pixel_format_t;

typedef enum repub_dither {
    REPUB_DITHER_NONE = 0,
    REPUB_DITHER_THRESHOLD = 1,
    REPUB_DITHER_BAYER4 = 2,
    REPUB_DITHER_BAYER8 = 3,
    REPUB_DITHER_FLOYD_STEINBERG = 4
} repub_dither_t;

/* ------------------------------------------------------------------------- */
/* Platform abstraction                                                      */
/* ------------------------------------------------------------------------- */

typedef struct repub_file repub_file_t; /* opaque platform file handle */

typedef struct repub_allocator {
    void *ctx;
    void *(*alloc)(void *ctx, size_t size);
    void *(*realloc)(void *ctx, void *ptr, size_t size);
    void (*free)(void *ctx, void *ptr);
} repub_allocator_t;

typedef struct repub_platform {
    void *ctx;
    repub_allocator_t allocator;

    /* Open a file for binary reading. Return REPUB_OK and a handle. */
    repub_err_t (*file_open)(void *ctx, const char *path, repub_file_t **out);
    /* Read up to n bytes. Return bytes read, 0 at EOF, or -1 on error. */
    int32_t (*file_read)(void *ctx, repub_file_t *file, void *buf, uint32_t n);
    /* whence: 0 = set, 1 = current, 2 = end. Return 0 on success. */
    int32_t (*file_seek)(void *ctx, repub_file_t *file, int64_t offset, int whence);
    int64_t (*file_tell)(void *ctx, repub_file_t *file);
    int64_t (*file_size)(void *ctx, repub_file_t *file);
    void (*file_close)(void *ctx, repub_file_t *file);

    /* Optional. level: 0 debug, 1 info, 2 warning, 3 error. */
    void (*log)(void *ctx, int level, const char *message);
    /* Optional. Monotonic milliseconds, used for cache invalidation/bench. */
    uint32_t (*ticks_ms)(void *ctx);
} repub_platform_t;

/* Platform backed by stdio + malloc. Suitable for desktop and tests. */
repub_platform_t repub_platform_default(void);

/* Helpers using the platform allocator (never mix them across platforms). */
void *repub_alloc(const repub_platform_t *platform, size_t size);
void *repub_realloc(const repub_platform_t *platform, void *ptr, size_t size);
void repub_free(const repub_platform_t *platform, void *ptr);

/* ------------------------------------------------------------------------- */
/* Fonts                                                                     */
/* ------------------------------------------------------------------------- */

typedef enum repub_font_style {
    REPUB_FONT_REGULAR = 0,
    REPUB_FONT_BOLD = 1,
    REPUB_FONT_ITALIC = 2,
    REPUB_FONT_BOLD_ITALIC = 3
} repub_font_style_t;

typedef struct repub_font_desc {
    /* Family name used by CSS font-family matching, e.g. "serif", "Noto Serif". */
    const char *family;
    /* TTF/OTF file bytes. Copied by repub_book_add_font(); may be freed on return. */
    const void *data;
    size_t size;
    int32_t face_index;
    repub_font_style_t style;
} repub_font_desc_t;

/* ------------------------------------------------------------------------- */
/* Book                                                                      */
/* ------------------------------------------------------------------------- */

typedef struct repub_book repub_book_t;
typedef struct repub_image repub_image_t; /* defined below */

typedef struct repub_open_options {
    uint32_t struct_size; /* sizeof(repub_open_options_t) */
    /* Reserved flags, pass 0. */
    uint32_t flags;
    /* Optional extra CSS loaded after the book's own stylesheets (borrowed). */
    const char *user_css;
    size_t user_css_len;
    /* 0 disables parsing the embedded stylesheets. Default (0 struct) = on. */
    int32_t enable_css;
} repub_open_options_t;

/* Flags for repub_open_options_t.flags */
#define REPUB_OPEN_SKIP_TOC 0x0001u /* do not parse NCX/NAV (small RAM) */

typedef struct repub_metadata {
    const char *title;      /* always non-NULL, may be "" */
    const char *author;
    const char *language;
    const char *publisher;
    const char *identifier; /* ISBN or package unique id when present */
    const char *date;
    const char *description;
    const char *series;
    float series_index;     /* < 0 when unknown */
} repub_metadata_t;

typedef struct repub_spine_item {
    const char *href;       /* package-relative path */
    const char *media_type; /* "application/xhtml+xml" */
    const char *id;
    int32_t linear;         /* 1 = in the linear reading order */
    uint32_t size;          /* uncompressed size in bytes */
} repub_spine_item_t;

typedef struct repub_toc_item {
    const char *title;
    const char *href;       /* may contain a #fragment; NULL for non-linking headings */
    int32_t level;          /* 0 = top level */
    int32_t spine_index;    /* resolved spine index, -1 when external/unknown */
} repub_toc_item_t;

/*
 * Open an EPUB file. Returns NULL on failure and writes the error to *err
 * (err may be NULL). The platform pointer must remain valid until close.
 */
repub_book_t *repub_book_open(const repub_platform_t *platform,
                              const char *path,
                              const repub_open_options_t *options,
                              repub_err_t *err);
void repub_book_close(repub_book_t *book);
const repub_platform_t *repub_book_platform(const repub_book_t *book);
const char *repub_book_path(const repub_book_t *book);

/* Metadata and navigation. */
const repub_metadata_t *repub_book_metadata(const repub_book_t *book);
size_t repub_book_spine_count(const repub_book_t *book);
const repub_spine_item_t *repub_book_spine_item(const repub_book_t *book, size_t index);
size_t repub_book_toc_count(const repub_book_t *book);
const repub_toc_item_t *repub_book_toc_item(const repub_book_t *book, size_t index);
/* Resolve an internal href ("chapter.xhtml#id") to spine index and text offset. */
repub_err_t repub_book_resolve_href(const repub_book_t *book, const char *href,
                                    int32_t *spine_index, char *anchor_buf,
                                    size_t anchor_cap);
/* Cover: href in the package, and decoded image (caller frees). */
const char *repub_book_cover_href(const repub_book_t *book);
repub_err_t repub_book_cover_image(const repub_book_t *book, repub_image_t **out);

/* Raw resource access (CSS, fonts, images, ...) relative to the OPF. */
repub_err_t repub_book_read_resource(const repub_book_t *book, const char *href,
                                     void **data, size_t *size);
repub_err_t repub_book_resource_size(const repub_book_t *book, const char *href,
                                     size_t *size);
/* Probe image dimensions without decoding the whole image. */
repub_err_t repub_book_image_dims(const repub_book_t *book, const char *href,
                                  int32_t *width, int32_t *height);
/* Decode a book image. Same as read_resource + repub_image_decode. */
repub_err_t repub_book_image_decode(const repub_book_t *book, const char *href,
                                    int32_t max_width, int32_t max_height,
                                    repub_image_t **out);

/* Fonts. data is copied. Returns REPUB_OK when the face was added. */
repub_err_t repub_book_add_font(repub_book_t *book, const repub_font_desc_t *desc);
/* Select the body text face. NULL/"" restores the built-in default. */
repub_err_t repub_book_set_body_font(repub_book_t *book, const char *family,
                                     repub_font_style_t style);
/* Enable/disable the book's embedded @font-face/package TTFs. Default on. */
void repub_book_set_embedded_fonts(repub_book_t *book, int enable);

/* ------------------------------------------------------------------------- */
/* Chapter content model                                                     */
/* ------------------------------------------------------------------------- */

typedef struct repub_chapter repub_chapter_t;

enum repub_block_type {
    REPUB_BLOCK_PARAGRAPH = 0,
    REPUB_BLOCK_HEADING = 1,
    REPUB_BLOCK_BLOCKQUOTE = 2,
    REPUB_BLOCK_PRE = 3,
    REPUB_BLOCK_LIST_ITEM = 4,
    REPUB_BLOCK_HR = 5,
    REPUB_BLOCK_IMAGE = 6,
    REPUB_BLOCK_TABLE_ROW = 7,
    REPUB_BLOCK_TABLE_CELL = 8,
    REPUB_BLOCK_OTHER = 9
};

enum repub_block_flags {
    REPUB_BLOCK_FLAG_BOLD = 1u << 0,
    REPUB_BLOCK_FLAG_ITALIC = 1u << 1,
    REPUB_BLOCK_FLAG_UNDERLINE = 1u << 2,
    REPUB_BLOCK_FLAG_STRIKE = 1u << 3,
    REPUB_BLOCK_FLAG_CENTERED = 1u << 4,
    REPUB_BLOCK_FLAG_RIGHT = 1u << 5,
    REPUB_BLOCK_FLAG_JUSTIFY = 1u << 6,
    REPUB_BLOCK_FLAG_NO_INDENT = 1u << 7,
    REPUB_BLOCK_FLAG_LINK = 1u << 8,
    REPUB_BLOCK_FLAG_CODE = 1u << 9
};

typedef struct repub_block_info {
    uint32_t text_offset;     /* offset into repub_chapter_text() */
    uint32_t text_len;
    uint8_t type;             /* enum repub_block_type */
    uint8_t heading_level;    /* 1..6 for headings, 0 otherwise */
    uint8_t flags;            /* enum repub_block_flags */
    uint8_t reserved;
    int16_t margin_before_px; /* relative spacing at body font size */
    int16_t margin_after_px;
    int16_t indent_px;
    int16_t font_scale_percent; /* 100 = body font size */
    int32_t image_index;      /* index into chapter images, -1 for text */
    int32_t link_index;       /* index into chapter links, -1 for none */
} repub_block_info_t;

typedef struct repub_note_info {
    uint32_t marker_text_offset; /* offset in chapter text where the marker sits */
    const char *text;            /* note body, owned by the chapter */
    uint32_t text_len;
    const char *href;            /* original target href, may be NULL */
    int32_t marker_image_index;  /* -1 when the marker is text (e.g. "1") */
} repub_note_info_t;

typedef struct repub_chapter_image_info {
    const char *href;       /* package-relative path */
    int32_t width;          /* probed width, 0 when unknown */
    int32_t height;
} repub_chapter_image_info_t;

typedef struct repub_chapter_link_info {
    const char *href;
    uint8_t kind;           /* 0 = internal, 1 = external, 2 = footnote */
    int32_t note_index;     /* valid when kind == 2 */
} repub_chapter_link_info_t;

/* Open one spine item. Parsing (HTML -> blocks/runs/notes) happens here. */
repub_err_t repub_book_chapter_open(repub_book_t *book, size_t spine_index,
                                    repub_chapter_t **out);
void repub_chapter_free(repub_chapter_t *chapter);

size_t repub_chapter_spine_index(const repub_chapter_t *chapter);
const char *repub_chapter_title(const repub_chapter_t *chapter);
/* Normalized UTF-8 text (no markup). Owned by the chapter. */
const char *repub_chapter_text(const repub_chapter_t *chapter, size_t *len);
size_t repub_chapter_block_count(const repub_chapter_t *chapter);
const repub_block_info_t *repub_chapter_block(const repub_chapter_t *chapter,
                                              size_t index);
size_t repub_chapter_note_count(const repub_chapter_t *chapter);
const repub_note_info_t *repub_chapter_note(const repub_chapter_t *chapter,
                                            size_t index);
size_t repub_chapter_image_count(const repub_chapter_t *chapter);
const repub_chapter_image_info_t *repub_chapter_image(
    const repub_chapter_t *chapter, size_t index);
size_t repub_chapter_link_count(const repub_chapter_t *chapter);
const repub_chapter_link_info_t *repub_chapter_link(const repub_chapter_t *chapter,
                                                    size_t index);
/* Resolve an element id/fragment to a normalized text offset. */
repub_err_t repub_chapter_anchor_offset(const repub_chapter_t *chapter,
                                        const char *anchor, uint32_t *offset);

/* ------------------------------------------------------------------------- */
/* Layout                                                                    */
/* ------------------------------------------------------------------------- */

typedef enum repub_writing_mode {
    REPUB_WRITING_HORIZONTAL = 0,
    REPUB_WRITING_VERTICAL_RL = 1 /* traditional CJK: columns right to left */
} repub_writing_mode_t;

typedef struct repub_layout_spec {
    uint32_t struct_size; /* sizeof(repub_layout_spec_t) */

    /* Body area on the page, in canvas pixels. */
    repub_rect_t page_rect;

    int32_t font_size_px;          /* body size, e.g. 22 */
    int32_t line_height_percent;   /* 100..250, 0 = 160 */
    int32_t paragraph_gap_percent; /* of font size, 0..200, default 60 */
    int32_t tracking_px;           /* extra letter spacing, -8..8 */
    int32_t first_line_indent_em;  /* 0..3, default 2 */
    int32_t margin_left_px;
    int32_t margin_right_px;
    int32_t margin_top_px;
    int32_t margin_bottom_px;

    repub_writing_mode_t writing_mode;
    int32_t images_visible;        /* default 1 */
    int32_t image_full_bleed;      /* illustrations span page_rect width */
    int32_t hyphenation;           /* 0 off, 1 on (needs REPUB_ENABLE_HYPHENATION) */
    const char *hyphenation_lang;  /* "en", "de", ...; NULL = auto from metadata */
    int32_t justify;               /* 1 = justify body text (space stretching) */
    int32_t chapter_heading_height_px; /* reserve on page 0, 0 = none */
    int32_t max_pages;             /* 0 = REPUB_MAX_PAGES */
} repub_layout_spec_t;

typedef struct repub_layout repub_layout_t;
typedef struct repub_page repub_page_t;

/* Fill a layout spec with the documented defaults.  Always call this (or set
 * struct_size) before passing a hand-initialized spec. */
void repub_layout_spec_defaults(repub_layout_spec_t *spec);

repub_err_t repub_layout_create(const repub_chapter_t *chapter,
                                const repub_layout_spec_t *spec,
                                repub_layout_t **out);
void repub_layout_free(repub_layout_t *layout);
const repub_chapter_t *repub_layout_chapter(const repub_layout_t *layout);
const repub_layout_spec_t *repub_layout_spec(const repub_layout_t *layout);
size_t repub_layout_page_count(const repub_layout_t *layout);
const repub_page_t *repub_layout_page(const repub_layout_t *layout, size_t index);
/* Byte-content progress helpers (0-based text offsets). */
uint32_t repub_layout_page_start_offset(const repub_layout_t *layout, size_t page);
size_t repub_layout_page_for_offset(const repub_layout_t *layout, uint32_t offset);

typedef struct repub_page_info {
    uint32_t start_offset; /* chapter text offset at page start */
    uint32_t end_offset;   /* chapter text offset at page end (exclusive) */
    int32_t width;
    int32_t height;
    int32_t primary_image_index; /* image-only page, else -1 */
    size_t span_count;
    size_t image_count;
    size_t note_count;
    size_t link_count;
} repub_page_info_t;

enum repub_span_flags {
    REPUB_SPAN_BOLD = 1u << 0,
    REPUB_SPAN_ITALIC = 1u << 1,
    REPUB_SPAN_UNDERLINE = 1u << 2,
    REPUB_SPAN_STRIKE = 1u << 3,
    REPUB_SPAN_VERTICAL = 1u << 4,
    REPUB_SPAN_SUPERSCRIPT = 1u << 5,
    REPUB_SPAN_SUBSCRIPT = 1u << 6,
    REPUB_SPAN_LINK = 1u << 7
};

/* One run of same-style text on a page.
 *
 * Horizontal writing: x = left edge, y = baseline, width = advance,
 * height = line box height.
 * Vertical writing: x = left edge of the column, y = top of the run,
 * width = column pitch (line height), height = run length down the column.
 * A page's spans are ordered in reading order; line_index groups them into
 * lines (horizontal) or columns (vertical). */
typedef struct repub_span {
    uint32_t text_offset; /* into chapter text */
    uint32_t text_len;    /* UTF-8 bytes */
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint16_t line_index;
    uint16_t font_size_px;
    uint8_t style;        /* enum repub_span_flags */
    uint8_t reserved;
} repub_span_t;

typedef struct repub_page_image {
    int32_t image_index; /* into chapter images */
    repub_rect_t rect;   /* destination rectangle on the page */
} repub_page_image_t;

typedef struct repub_page_note_marker {
    int32_t note_index; /* into chapter notes */
    repub_rect_t rect;  /* tap target */
    int32_t icon;       /* image index for icon markers, -1 for text */
} repub_page_note_marker_t;

typedef struct repub_page_link {
    const char *href;
    repub_rect_t rect;
} repub_page_link_t;

const repub_page_info_t *repub_page_info(const repub_page_t *page);
const repub_span_t *repub_page_span(const repub_page_t *page, size_t index);
const repub_page_image_t *repub_page_image_at(const repub_page_t *page,
                                              size_t index);
const repub_page_note_marker_t *repub_page_note_at(const repub_page_t *page,
                                                   size_t index);
const repub_page_link_t *repub_page_link_at(const repub_page_t *page,
                                            size_t index);
/* Page plain text copy (UTF-8, NUL terminated). Returns bytes written. */
size_t repub_page_text(const repub_page_t *page, char *buf, size_t cap);

/* ------------------------------------------------------------------------- */
/* Canvas                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct repub_canvas repub_canvas_t;

/* Allocate a canvas owned by the library. */
repub_canvas_t *repub_canvas_create(const repub_platform_t *platform,
                                    int32_t width, int32_t height,
                                    repub_pixel_format_t format,
                                    repub_err_t *err);
/* Wrap an existing framebuffer (e.g. e-ink panel buffer). Not freed by the library. */
repub_canvas_t *repub_canvas_wrap(const repub_platform_t *platform,
                                  void *pixels, int32_t width, int32_t height,
                                  int32_t stride_bytes,
                                  repub_pixel_format_t format,
                                  repub_err_t *err);
void repub_canvas_destroy(repub_canvas_t *canvas);

int32_t repub_canvas_width(const repub_canvas_t *canvas);
int32_t repub_canvas_height(const repub_canvas_t *canvas);
repub_pixel_format_t repub_canvas_format(const repub_canvas_t *canvas);
void *repub_canvas_data(const repub_canvas_t *canvas);
int32_t repub_canvas_stride(const repub_canvas_t *canvas);
/* level: 0 = black .. maximum (1/3/15/255) = white. */
void repub_canvas_clear(repub_canvas_t *canvas, int32_t level);
void repub_canvas_fill_rect(repub_canvas_t *canvas, repub_rect_t rect,
                            int32_t level);
void repub_canvas_set_pixel(repub_canvas_t *canvas, int32_t x, int32_t y,
                            int32_t level);

/* ------------------------------------------------------------------------- */
/* Images                                                                    */
/* ------------------------------------------------------------------------- */


/* Decode PNG/JPEG/GIF/BMP (WebP when enabled). max_width/height 0 = no bound. */
repub_err_t repub_image_decode(const repub_platform_t *platform,
                               const void *data, size_t size,
                               int32_t max_width, int32_t max_height,
                               repub_image_t **out);
/* Create an empty RGBA8 image (pixels zeroed). */
repub_image_t *repub_image_create(const repub_platform_t *platform,
                                  int32_t width, int32_t height,
                                  repub_err_t *err);
void repub_image_free(repub_image_t *image);
int32_t repub_image_width(const repub_image_t *image);
int32_t repub_image_height(const repub_image_t *image);
/* RGBA8 pixels, row-major, stride = width * 4. */
const uint8_t *repub_image_pixels(const repub_image_t *image);
/* Scaled copy (bilinear resampling for both enlargement and reduction). */
repub_err_t repub_image_scale(const repub_platform_t *platform,
                              const repub_image_t *src, int32_t width,
                              int32_t height, repub_image_t **out);
/* Probe dimensions from encoded bytes. */
repub_err_t repub_image_probe(const void *data, size_t size, int32_t *width,
                              int32_t *height);

typedef enum repub_image_fit {
    REPUB_IMAGE_FIT_NONE = 0,  /* draw at natural size, centered in dest */
    REPUB_IMAGE_FIT_CONTAIN = 1, /* fit inside dest preserving aspect */
    REPUB_IMAGE_FIT_COVER = 2  /* fill dest preserving aspect, crop overflow */
} repub_image_fit_t;

typedef struct repub_image_view {
    uint32_t struct_size;
    repub_rect_t dest;         /* where the image is drawn */
    float zoom;                /* 1.0 = fit; 2.0 = twice that size */
    float pan_x;               /* normalized pan, 0 = centered */
    float pan_y;
    repub_image_fit_t fit;
    repub_dither_t dither;
    int32_t background_level;  /* canvas level behind transparent pixels */
} repub_image_view_t;

repub_err_t repub_canvas_draw_image(repub_canvas_t *canvas,
                                    const repub_image_t *image,
                                    const repub_image_view_t *view);

/* ------------------------------------------------------------------------- */
/* Page rendering                                                            */
/* ------------------------------------------------------------------------- */

enum repub_render_flags {
    REPUB_RENDER_UNDERLINE_LINKS = 1u << 0,
    REPUB_RENDER_DRAW_IMAGES = 1u << 1,
    REPUB_RENDER_DRAW_NOTE_ICONS = 1u << 2,
    REPUB_RENDER_DEBUG_BOXES = 1u << 3
};

typedef struct repub_render_options {
    uint32_t struct_size; /* sizeof(repub_render_options_t) */
    uint32_t flags;
    repub_dither_t text_dither; /* for MONO1/GRAY2 output */
    int32_t text_level;         /* default 0 (black) */
    int32_t background_level;   /* -1 = leave canvas untouched */
    int32_t threshold;          /* 0..255, 128 default */
} repub_render_options_t;

/* Fill render options with defaults (underlines on, images on, text 0,
 * background -1, threshold 128, dither chosen for the canvas by the caller). */
void repub_render_options_defaults(repub_render_options_t *options);

/* Render a laid-out page into the canvas. Images are decoded on demand. */
repub_err_t repub_page_render(const repub_page_t *page,
                              repub_canvas_t *canvas,
                              const repub_render_options_t *options);
/* Same, but also returns the layout so the caller keeps ownership. */
repub_err_t repub_layout_render(const repub_layout_t *layout, size_t page,
                                repub_canvas_t *canvas,
                                const repub_render_options_t *options);

/* Layout a standalone UTF-8 string (popups, dialogs, excerpts). */
repub_err_t repub_book_layout_text(repub_book_t *book, const char *utf8,
                                   size_t len, const repub_layout_spec_t *spec,
                                   repub_layout_t **out);

/* ------------------------------------------------------------------------- */
/* High level reader                                                         */
/* ------------------------------------------------------------------------- */

typedef struct repub_reader repub_reader_t;

/* Open a book and immediately lay out its first chapter. */
repub_reader_t *repub_reader_open(const repub_platform_t *platform,
                                  const char *path,
                                  const repub_layout_spec_t *layout,
                                  const repub_open_options_t *options,
                                  repub_err_t *err);
/* Attach an already opened (and configured) book, taking ownership of it.
 * Use this to register custom fonts or tweak options before layout. */
repub_reader_t *repub_reader_attach(repub_book_t *book,
                                    const repub_layout_spec_t *layout,
                                    repub_err_t *err);
void repub_reader_close(repub_reader_t *reader);
repub_book_t *repub_reader_book(repub_reader_t *reader);

/* Change typography/page geometry; reflows the current chapter and keeps
 * the reading position at the top of the current page. */
repub_err_t repub_reader_set_layout(repub_reader_t *reader,
                                    const repub_layout_spec_t *layout);
const repub_layout_spec_t *repub_reader_layout_spec(const repub_reader_t *reader);

/* Navigation. Page indices are local to the current chapter. */
size_t repub_reader_chapter_index(const repub_reader_t *reader);
size_t repub_reader_chapter_count(const repub_reader_t *reader);
size_t repub_reader_page_count(const repub_reader_t *reader);
size_t repub_reader_page_index(const repub_reader_t *reader);
repub_err_t repub_reader_goto_chapter(repub_reader_t *reader, size_t chapter);
repub_err_t repub_reader_goto_offset(repub_reader_t *reader, size_t chapter,
                                     uint32_t text_offset);
repub_err_t repub_reader_goto_href(repub_reader_t *reader, const char *href);
repub_err_t repub_reader_goto_progress(repub_reader_t *reader, float fraction);
repub_err_t repub_reader_next_page(repub_reader_t *reader);
repub_err_t repub_reader_prev_page(repub_reader_t *reader);
/* Current page and its layout (owned by the reader). */
const repub_page_t *repub_reader_page(const repub_reader_t *reader);
const repub_layout_t *repub_reader_layout(const repub_reader_t *reader);
const repub_chapter_t *repub_reader_chapter(const repub_reader_t *reader);
repub_err_t repub_reader_render(repub_reader_t *reader, repub_canvas_t *canvas,
                                const repub_render_options_t *options);
/* 0.0 .. 1.0 across the whole book (byte weighted, includes current page). */
float repub_reader_progress(const repub_reader_t *reader);

/* Footnotes: hit test the current page, then open a popup layout. */
int32_t repub_reader_note_at(const repub_reader_t *reader, int32_t x, int32_t y);
/* Note text for the current chapter's note index. */
repub_err_t repub_reader_note_text(const repub_reader_t *reader,
                                   size_t note_index, const char **text,
                                   size_t *len);

typedef struct repub_note_popup repub_note_popup_t;
/* Lay out one note into a paged popup using popup_spec (page_rect is the
 * popup body). Returns NULL when the note does not exist. */
repub_note_popup_t *repub_reader_note_popup_open(repub_reader_t *reader,
                                                 size_t note_index,
                                                 const repub_layout_spec_t *popup_spec,
                                                 repub_err_t *err);
void repub_note_popup_free(repub_note_popup_t *popup);
size_t repub_note_popup_page_count(const repub_note_popup_t *popup);
repub_err_t repub_note_popup_render(const repub_note_popup_t *popup,
                                    size_t page, repub_canvas_t *canvas,
                                    const repub_render_options_t *options);
const repub_layout_t *repub_note_popup_layout(const repub_note_popup_t *popup);

/* Images on the current page: hit test and open a pan/zoom viewer. */
int32_t repub_reader_image_at(const repub_reader_t *reader, int32_t x, int32_t y);
repub_err_t repub_reader_image_decode(const repub_reader_t *reader,
                                      size_t image_index,
                                      int32_t max_width, int32_t max_height,
                                      repub_image_t **out);

typedef struct repub_image_viewer repub_image_viewer_t;
repub_image_viewer_t *repub_reader_image_viewer_open(repub_reader_t *reader,
                                                     size_t image_index,
                                                     repub_err_t *err);
void repub_image_viewer_free(repub_image_viewer_t *viewer);
const repub_image_t *repub_image_viewer_image(const repub_image_viewer_t *viewer);
/* zoom = 1 fits the image into dest; pan is normalized (-1..1) in either axis. */
repub_err_t repub_image_viewer_render(repub_image_viewer_t *viewer,
                                      repub_canvas_t *canvas,
                                      repub_rect_t dest, float zoom,
                                      float pan_x, float pan_y,
                                      const repub_render_options_t *options);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROLLING_EPUB_REPUB_H */

