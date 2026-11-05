/*
 * RollingEPUB - portable EPUB parsing, layout and rendering library.
 *
 * Feature configuration.
 *
 * Every REPUB_ENABLE_* switch may be overridden by the build system
 * (-DREPUB_ENABLE_IMAGES=0) or by a project level configuration header
 * included before rolling_epub/repub.h.  Turning a feature off removes its
 * code from the build and makes the matching API report REPUB_ERR_UNSUPPORTED.
 *
 * Defaults are chosen so that a plain build is fully featured, while an
 * embedded build can disable expensive parts (TTF rasterizer, hyphenation,
 * vertical text, image decoding) to fit flash and RAM budgets.
 */
#ifndef ROLLING_EPUB_REPUB_CONFIG_H
#define ROLLING_EPUB_REPUB_CONFIG_H

/* ---- Container / navigation ------------------------------------------- */
#ifndef REPUB_ENABLE_TOC
#define REPUB_ENABLE_TOC 1 /* NCX + EPUB3 nav parsing */
#endif
#ifndef REPUB_ENABLE_COVER
#define REPUB_ENABLE_COVER 1
#endif
#ifndef REPUB_ENABLE_ENCRYPTION_PROBE
#define REPUB_ENABLE_ENCRYPTION_PROBE 1 /* report protected entries, no DRM */
#endif

/* ---- Content parsing --------------------------------------------------- */
#ifndef REPUB_ENABLE_CSS
#define REPUB_ENABLE_CSS 1 /* stylesheet parsing and cascade */
#endif
#ifndef REPUB_ENABLE_FOOTNOTES
#define REPUB_ENABLE_FOOTNOTES 1 /* markers, note text, popup layouts */
#endif
#ifndef REPUB_ENABLE_TABLES
#define REPUB_ENABLE_TABLES 1 /* simple grid/stacked table layout */
#endif
#ifndef REPUB_ENABLE_DROP_CAPS
#define REPUB_ENABLE_DROP_CAPS 1
#endif
#ifndef REPUB_ENABLE_RUBY
#define REPUB_ENABLE_RUBY 1 /* <ruby> annotations (rendered as inline text) */
#endif

/* ---- Layout ------------------------------------------------------------ */
#ifndef REPUB_ENABLE_HYPHENATION
#define REPUB_ENABLE_HYPHENATION 1 /* Liang pattern hyphenation, en/de/fr/... */
#endif
#ifndef REPUB_ENABLE_VERTICAL_TEXT
#define REPUB_ENABLE_VERTICAL_TEXT 1 /* CJK vertical writing mode */
#endif
#ifndef REPUB_ENABLE_BIDI
#define REPUB_ENABLE_BIDI 1 /* minimal RTL reordering for Arabic/Hebrew runs */
#endif

/* ---- Fonts and rendering ---------------------------------------------- */
#ifndef REPUB_ENABLE_TTF
#define REPUB_ENABLE_TTF 1 /* stb_truetype rasterizer */
#endif
#ifndef REPUB_ENABLE_BUILTIN_FONT
#define REPUB_ENABLE_BUILTIN_FONT 1 /* always-available small bitmap font */
#endif
#ifndef REPUB_ENABLE_TEXT_ANTIALIASING
#define REPUB_ENABLE_TEXT_ANTIALIASING 1
#endif
#ifndef REPUB_ENABLE_DITHERING
#define REPUB_ENABLE_DITHERING 1
#endif
#ifndef REPUB_ENABLE_GRAYSCALE
#define REPUB_ENABLE_GRAYSCALE 1 /* gray2/gray4/gray8 canvases */
#endif

/* ---- Images ------------------------------------------------------------ */
#ifndef REPUB_ENABLE_IMAGES
#define REPUB_ENABLE_IMAGES 1
#endif
#ifndef REPUB_ENABLE_IMAGE_ZOOM
#define REPUB_ENABLE_IMAGE_ZOOM 1 /* pan/zoom image viewer helpers */
#endif
#ifndef REPUB_ENABLE_GIF
#define REPUB_ENABLE_GIF 1
#endif
#ifndef REPUB_ENABLE_WEBP
#define REPUB_ENABLE_WEBP 0 /* needs an external decoder; opt-in */
#endif
#ifndef REPUB_MAX_IMAGE_BYTES
#define REPUB_MAX_IMAGE_BYTES (16u * 1024u * 1024u)
#endif
#ifndef REPUB_MAX_IMAGE_PIXELS
#define REPUB_MAX_IMAGE_PIXELS (4096u * 4096u)
#endif

/* ---- Caching / misc ---------------------------------------------------- */
#ifndef REPUB_ENABLE_CACHE
#define REPUB_ENABLE_CACHE 1 /* in-memory chapter/layout caches */
#endif
#ifndef REPUB_ENABLE_PROGRESS
#define REPUB_ENABLE_PROGRESS 1 /* byte-weighted reading progress */
#endif

/* ---- Hard limits (safe defaults, override per device) ----------------- */
#ifndef REPUB_MAX_SPINE_ITEMS
#define REPUB_MAX_SPINE_ITEMS 8192
#endif
#ifndef REPUB_MAX_ZIP_ENTRIES
#define REPUB_MAX_ZIP_ENTRIES 16384
#endif
#ifndef REPUB_MAX_CHAPTER_BYTES
#define REPUB_MAX_CHAPTER_BYTES (8u * 1024u * 1024u)
#endif
#ifndef REPUB_MAX_BLOCKS
#define REPUB_MAX_BLOCKS 65536u
#endif
#ifndef REPUB_MAX_PAGES
#define REPUB_MAX_PAGES 16384u
#endif
#ifndef REPUB_MAX_TEXT_BYTES
#define REPUB_MAX_TEXT_BYTES (8u * 1024u * 1024u)
#endif

#endif /* ROLLING_EPUB_REPUB_CONFIG_H */
