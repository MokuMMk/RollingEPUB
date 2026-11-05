# RollingEPUB — Feature Matrix

Legend: ✅ supported · ◐ optional / partial · ✖ not planned for v1.

Sources:
- **CrossSIGO / CrossMux** — `D:\CrossSIGO\crossmux\lib\Epub` (MIT)
- **Read Pico / kiikoread** — `D:\Read Pico\main\book` (Apache-2.0 for the reused files)

## Why this library exists

Both readers grew their own EPUB stack. CrossSIGO has the richer CSS model,
hyphenation, TOC/metadata and image dithering; Read Pico has the richer CJK and
vertical pagination, footnote extraction (including cross-file notes) and
per-run font switching. RollingEPUB merges the two into one portable library so
a new reader does not have to parse ZIP/OPF/NCX/NAV/HTML/CSS, paginate text or
rasterize glyphs itself.

## Container / package

| Feature | CrossSIGO | Read Pico | RollingEPUB |
| --- | --- | --- | --- |
| Read-only ZIP (stored + deflate) | ✅ | ✅ | ✅ |
| ZIP64 central directory | ◐ | ✖ | ✅ |
| CRC verification on extraction | ◐ | ✅ | ✅ |
| Case-insensitive entry fallback | ◐ | ✖ | ✅ |
| Encrypted/DRM entry detection | ✅ | ✖ | ✅ (report only, no decrypt) |
| Bounded extraction / prefix probes | ✅ | ✅ | ✅ |
| EPUB 2/3 container.xml | ✅ | ✅ | ✅ |
| OPF metadata (title/author/language/publisher/id/date/description) | ✅ | ✅ | ✅ |
| Series + volume metadata | ✅ | ✖ | ✅ |
| Manifest / spine / linear flags | ✅ | ✅ | ✅ |
| EPUB 2 NCX TOC | ✅ | ✅ | ✅ |
| EPUB 3 nav TOC (nested) | ✅ | ✅ | ✅ |
| TOC href → spine/anchor resolution | ✅ | ✅ | ✅ |
| Cover (meta name=cover + cover-image property) | ✅ | ✅ | ✅ |
| Embedded @font-face / packaged TTF | ◐ | ✅ | ✅ (TTF build) |
| Image dimension probing without decode | ✅ | ✅ | ✅ |

## Content parsing (HTML / CSS)

| Feature | CrossSIGO | Read Pico | RollingEPUB |
| --- | --- | --- | --- |
| Tolerant XHTML/HTML tokenizer | ✅ | ✅ | ✅ |
| HTML entities (named + numeric) | ✅ | ◐ | ✅ |
| Paragraphs, headings h1–h6, divs, sections | ✅ | ✅ | ✅ |
| Lists / blockquote / pre / hr / code | ✅ | ◐ | ✅ |
| Tables (row/cell text) | ✅ | ✖ | ✅ (row text, not grid rules) |
| Inline images + block illustrations | ✅ | ✅ | ✅ |
| Links (internal/external) + tap rects | ✅ | ◐ | ✅ |
| Anchors (id/name) → text offset | ✅ | ✅ | ✅ |
| Same-file footnotes | ✅ | ✅ | ✅ |
| Cross-file footnotes (`notes.xhtml#n1`) | ✖ | ✅ | ✅ |
| Icon footnote markers + text markers | ◐ | ✅ | ✅ |
| Ruby annotations | ✖ | ◐ | ◐ base text kept, rt dropped in v1 |
| Drop caps (`initial-letter` / `::first-letter`) | ✅ | ✖ | ✅ (layout support) |
| CSS class/id/element selectors | ✅ | ◐ | ✅ |
| Descendant/child combinators + specificity | ✅ | ◐ | ✅ |
| Inline `style=""` | ✅ | ◐ | ✅ |
| `display:none` | ✅ | ✅ | ✅ |
| `text-align`, `text-indent`, margins/padding | ✅ | ◐ | ✅ |
| `font-size` / `font-weight` / `font-style` / `font-family` | ✅ | ◐ | ✅ |
| `text-decoration` underline/strike | ✅ | ✖ | ✅ |
| `white-space: pre` | ✅ | ◐ | ✅ |
| `page-break-before/after` | ✅ | ✖ | ✅ |
| Borders + background shading | ✅ | ✖ | ✅ (page decorations) |
| `vertical-align: super/sub` | ✅ | ✅ | ✅ |
| `@font-face` collection | ◐ | ✅ | ✅ |
| `@media` / `@import` / sibling selectors | ✖ | ✖ | ✖ (deliberately skipped) |

## Layout / pagination

| Feature | CrossSIGO | Read Pico | RollingEPUB |
| --- | --- | --- | --- |
| Deterministic pixel pagination | ✅ | ✅ | ✅ |
| Latin whole-word wrapping | ✅ | ✅ | ✅ |
| Overlong-word emergency splitting | ✅ | ✅ | ✅ |
| CJK per-character wrapping + kinsoku | ◐ | ✅ | ✅ |
| Dictionary hyphenation | ✅ (11 langs) | ✖ | ✅ English default, other languages optional |
| Justified text (space stretching) | ✅ | ✅ | ✅ |
| First-line indent / margins / line height / tracking | ✅ | ✅ | ✅ |
| Mixed fonts and sizes per span | ◐ | ✅ | ✅ |
| Superscript / subscript | ✅ | ✅ | ✅ |
| Images interleaved with text (block-level, no CSS float wrap) | ✅ | ✅ | ✅ |
| Full-bleed illustrations | ✅ | ✅ | ✅ |
| Image-only pages | ✅ | ✅ | ✅ |
| Footnote markers inline | ✅ | ✅ | ✅ |
| Chapter lead heading (reserved area) | ◐ | ✅ | ✅ |
| Vertical CJK writing (right-to-left columns) | ✖ | ✅ | ✅ (optional build) |
| Page ↔ text-offset mapping / anchors | ✅ | ✅ | ✅ |
| Incremental / resumable pagination | ✅ | ◐ | ◐ (single-pass build in v1) |
| Cache pagination to disk | ✅ | ✅ | ✖ (in-memory only) |

## Rendering

| Feature | CrossSIGO | Read Pico | RollingEPUB |
| --- | --- | --- | --- |
| 1-bit framebuffer | ✅ | ✅ | ✅ |
| 2/4/8-bit gray framebuffer | ✅ | ✅ | ✅ |
| Antialiased glyphs | ✅ | ✅ | ✅ |
| Built-in bitmap fallback font | ✅ | ✅ | ✅ |
| TrueType/OpenType faces | ◐ | ✅ | ✅ |
| Glyph cache | ✅ | ✅ | ✅ |
| Per-run font switching | ◐ | ✅ | ✅ |
| Vertical text drawing (upright CJK, rotated Latin) | ✖ | ✅ | ✅ |
| PNG/JPEG/GIF image decode | ✅ | ✅ | ✅ (BMP too) |
| WebP decode | ✖ | ✅ | ◐ optional hook (no bundled decoder) |
| Image scaling (bilinear enlargement + reduction) | ✅ | ✅ (area reduction + bilinear zoom) | ✅ (bilinear both ways) |
| Dithering (Bayer, Floyd–Steinberg) | ✅ | ✅ | ✅ |
| Image zoom / pan / fit viewer | ✖ | ✅ | ✅ |
| Page display list for custom renderers | ✅ | ◐ | ✅ |
| Debug boxes | ✖ | ✖ | ✅ (library extra) |

## Developer-facing features

| Feature | RollingEPUB |
| --- | --- |
| One-call reader (`repub_reader_open`) | ✅ |
| C API + header-only C++ RAII facade | ✅ |
| Platform abstraction (file IO/alloc/log/ticks) | ✅ |
| Configurable bounds (bytes, pages, entries, pixels) | ✅ |
| Compile-time feature flags | ✅ |
| Footnote popup layout helper | ✅ |
| Image viewer (zoom/pan/fit) | ✅ |
| Cover retrieval / decode | ✅ |
| Chapter text extraction (plain UTF-8) | ✅ |
| Byte-weighted reading progress | ✅ |
| TOC with nesting + href resolution | ✅ |
| Host CLI (inspect/render/export) | ✅ |
| ESP-IDF component + PlatformIO packaging | ✅ |
| Tests + real EPUB fixtures | ✅ |

## Explicit non-goals (v1)

- DRM/encryption decryption (Adobe ADEPT, LCP, etc.), watermark removal.
- JavaScript, network resources, animations, media overlays.
- Fixed-layout EPUB (pre-paginated), comic/manga page modes.
- MathML, full SVG rendering (SVG referenced as `<img>` is reported as unsupported).
- Reader UI: bookshelf, settings screens, WiFi transfer, TTS/audio, page-turn waveforms.
- MOBI/AZW3/PDF/TXT parsers. (TXT support can be added as a separate content provider.)

## Compile-time features

All flags live in `include/rolling_epub/repub_config.h`; CMake exposes matching
`REPUB_ENABLE_*` options. Turning a feature off removes its code and makes the
related API return `REPUB_ERR_UNSUPPORTED` or fall back gracefully.

| Macro | Default | Removes / effect |
| --- | --- | --- |
| `REPUB_ENABLE_TOC` | 1 | NCX/NAV parsing; smaller RAM |
| `REPUB_ENABLE_COVER` | 1 | cover lookup/decode |
| `REPUB_ENABLE_CSS` | 1 | stylesheet parsing/cascade (inline styles still parsed) |
| `REPUB_ENABLE_FOOTNOTES` | 1 | note extraction and popup data |
| `REPUB_ENABLE_TABLES` | 1 | table row blocks |
| `REPUB_ENABLE_DROP_CAPS` | 1 | initial-letter layout |
| `REPUB_ENABLE_RUBY` | 1 | ruby handling |
| `REPUB_ENABLE_HYPHENATION` | 1 | Liang hyphenation engine |
| `REPUB_HYPH_EN/DE/FR/ES/IT/RU` | EN=1 | per-language pattern data (DE is ~1.3 MB) |
| `REPUB_ENABLE_VERTICAL_TEXT` | 1 | vertical CJK layout |
| `REPUB_ENABLE_BIDI` | 1 | RTL reordering |
| `REPUB_ENABLE_TTF` | 1 | stb_truetype rasterizer + TTF faces |
| `REPUB_ENABLE_BUILTIN_FONT` | 1 | bitmap fallback face |
| `REPUB_ENABLE_TEXT_ANTIALIASING` | 1 | AA coverage (off = 1-bit glyphs) |
| `REPUB_ENABLE_DITHERING` | 1 | image dithering (off = threshold) |
| `REPUB_ENABLE_GRAYSCALE` | 1 | gray2/4/8 canvases |
| `REPUB_ENABLE_IMAGES` | 1 | stb_image decoders |
| `REPUB_ENABLE_IMAGE_ZOOM` | 1 | image viewer helpers |
| `REPUB_ENABLE_GIF` | 1 | GIF decode |
| `REPUB_ENABLE_WEBP` | 0 | WebP decode (requires external decoder hook) |
| `REPUB_ENABLE_CACHE` | 1 | in-memory chapter/layout cache |
| `REPUB_ENABLE_PROGRESS` | 1 | byte-weighted progress |

Hard limits (override per device): `REPUB_MAX_SPINE_ITEMS` 8192,
`REPUB_MAX_ZIP_ENTRIES` 16384, `REPUB_MAX_CHAPTER_BYTES` 8 MiB,
`REPUB_MAX_BLOCKS` 65536, `REPUB_MAX_PAGES` 16384,
`REPUB_MAX_IMAGE_BYTES` 16 MiB, `REPUB_MAX_IMAGE_PIXELS` 16 MP.

## Implementation status (v1.0)

Verified on the host (Windows/MinGW g++ 16, C++17):

| Suite | Tests | Status |
| --- | --- | --- |
| Container (ZIP/XML/OPF/NAV/NCX/resources/errors) | 15 | pass |
| CSS cascade | 21 | pass |
| HTML → Flow | 16 | pass |
| Layout (horizontal/CJK/vertical/notes/images) | 24 | pass |
| Hyphenation (English Liang) | 6 | pass |
| Image decode/scale/probe/draw | 14 | pass |
| Canvas/font/page render | 15 | pass |
| Public API, reader, popup, viewer, facade | 10 | pass |
| Rolling Girl themed fixture (song profile, records, artwork, vertical) | 7 | pass |
| **Total** | **128** | **9/9 suites pass** |

Additional verification:
- release build with `-Werror`: clean.
- reduced builds compile: no-TTF, no-images, and a "text-only" configuration
  with CSS/vertical/hyphenation/images/TOC/progress all disabled.
- 18 real EPUB fixtures open with sane metadata/spine/TOC; malformed ZIP/OPF
  and empty-spine inputs return errors without crashing.
- CLI end-to-end: `info`, `toc`, `text`, `blocks`, `notes`, `pages`, `render`
  (PNG/PGM/PPM), `image`, `cover`, `selftest`.
- Visual checks (docs/images): Latin and CJK pages, footnote popup, 2× image
  zoom viewer.

### Known gaps (v1)

1. No visible hyphen glyph for automatic hyphenation (text stays contiguous, no
   bytes are lost). Needs a synthetic-glyph extension to the page display list.
2. Ruby annotations: base text is kept, `<rt>` is dropped.
3. Tables flatten to row text (two spaces between cells); no grid borders.
4. Block-level left/right CSS margins are only represented for blockquotes
   (fixed 2em inset); `margin-left/right` on other blocks is not stored in the
   flow model.
5. Pagination caches are in-memory only; no resumable/incremental pagination
   for very large chapters and no on-disk page cache.
6. Vertical writing: illustrations take a page of their own; paragraph gaps are
   not applied between vertical blocks.
7. DRM is detected, never bypassed.
8. Fixed-layout EPUB, MathML, SVG rendering, scripts, network resources, media
   overlays and reader UI are out of scope.
9. `layout_spec` zero values mean "library default"; an explicit "off" for
   hyphenation/justify needs the compile-time flags or a future explicit-enable
   field.
10. Popup/viewer objects borrow reader state and must be freed before closing
    the reader.

