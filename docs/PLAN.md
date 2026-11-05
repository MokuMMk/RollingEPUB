# RollingEPUB — Master Plan

**Goal:** a single, reusable EPUB parsing + layout + rendering library that replaces the
EPUB code in both reader projects, so an application can add a full reading experience
without writing its own container/HTML/CSS parser, paginator, glyph rasterizer, image
decoder or footnote/image UI plumbing.

Source projects (read-only inputs, never modified):

| Project | Stack | EPUB code | License |
| --- | --- | --- | --- |
| [0x1abin/crossmux](https://github.com/0x1abin/crossmux) | C++17 / PlatformIO / ESP32 | `lib/Epub` (~40k lines), `lib/ZipFile`, `lib/miniz`, `lib/EpdFont`, `lib/Txt`, `lib/Xtc` | MIT |
| [wegooo-cell/read-pico-reader](https://github.com/wegooo-cell/read-pico-reader) | C / ESP-IDF / ESP32-S3/P4 | `main/book` (book_epub, html_text, book_layout, zip_reader, cover, image header) | Apache-2.0 (new material possibly more restrictive; only Apache-2.0 files are reused) |

## Product requirements

1. **Developer-facing:** one include (`rolling_epub/repub.h`) + one static lib. Open book,
   set page rect/font, call render; TOC, footnotes, cover, progress, images included.
2. **Feature union of both engines:** EPUB2/3, OPF metadata, NCX+NAV, CSS cascade,
   per-span fonts, CJK + Latin line breaking, hyphenation, footnotes (same-file and
   cross-file), images with decoding and zoom, vertical CJK writing, drop caps, tables,
   links, anchors, reading progress.
3. **Portable:** no Arduino/ESP-IDF/SDL dependency in the core. Platform callbacks for
   file IO, allocation, logging, ticks. Runs on host (Windows/Linux/macOS) and ESP32.
4. **Configurable:** compile-time feature switches (`repub_config.h`, CMake options) so a
   380 KB-RAM device can drop TTF/dithering/vertical/etc.
5. **Testable:** host test suite + CLI renderer; real EPUB fixtures from both projects.
6. **Licensing:** MIT core license, Apache-2.0/MIT notices preserved, no code taken from
   files under `LicenseRef-Kiikoread-Personal-Study-1.0`.

## Non-goals (v1)

- DRM/encryption decryption (only detection + clear error).
- Browser-grade HTML/CSS (no scripts, no network resources, no animations).
- Reader UI (bookshelf, settings, WiFi, fonts download) — the library renders into a
  caller-owned framebuffer; the application owns the UI.
- Text-to-speech, audio, page-turn waveforms.

## Architecture

```
 application (ESP32 reader / desktop demo / tests)
        │  C API  repub*.h        C++ facade  rolling_epub.hpp
        ▼
 ┌──────────────────────────────────────────────────────────────┐
 │ API layer        reader, chapter, layout, popup, image viewer│
 │                  C API glue + error/bounds policy            │
 ├──────────────────────────────────────────────────────────────┤
 │ layout           flow -> Page[] (spans, images, markers)     │
 │                  line breaker (CJK/Latin), hyphenation,      │
 │                  vertical writing, pagination, anchors       │
 ├──────────────────────────────────────────────────────────────┤
 │ content          CSS cascade + HTML -> Flow                  │
 │                  (text, blocks, runs, notes, links, images)  │
 ├──────────────────────────────────────────────────────────────┤
 │ container        ZIP -> container.xml -> OPF -> spine/TOC    │
 │                  metadata, cover, resources                  │
 ├──────────────────────────────────────────────────────────────┤
 │ render           Canvas, FontManager (bitmap + stb_truetype),│
 │                  image decode (stb_image), scale, dither,    │
 │                  page renderer                               │
 ├──────────────────────────────────────────────────────────────┤
 │ core             platform, allocation, logging, UTF-8, paths,│
 │                  entities, miniz (vendored)                   │
 └──────────────────────────────────────────────────────────────┘
```

Key internal contracts (headers in `src/`):

| Header | Owner | Contract |
| --- | --- | --- |
| `core/base.h` | Lead | platform, Rect, alloc, log, LE readers |
| `core/text_util.h` | container | UTF-8, entities, path/href resolution |
| `container/zip.h` | container | read-only ZIP with platform IO |
| `container/xml.h` | container | XML pull parser |
| `container/book.h` | container | `repub_book`, manifest/spine/TOC, resources |
| `content/css.h` | content | CssStyle + cascade |
| `content/flow.h` | content | `Flow`, `repub_chapter`, parser entry |
| `layout/layout.h` | layout | `FontMetrics`, `repub_page`, `repub_layout`, builder |
| `layout/hyphen.h` | layout | hyphenation API |
| `render/canvas.h` | render | pixel canvas |
| `render/font.h` | render | FontManager/FontMetrics |
| `render/image.h` | render | decode/scale/dither |
| `render/page_render.h` | render | page -> canvas |
| `api/api_internal.h` | api | reader/popup/viewer state |

Public opaque types (`repub_book`, `repub_chapter`, `repub_page`, `repub_layout`,
`repub_reader`, `repub_canvas`, `repub_image`, ...) are defined once in the internal
headers and cast directly in the API layer; no copies.

## Reuse map (what comes from where)

Container/metadata/TOC:
- Read Pico: OPF/spine pipeline, cover detection, navigation heuristics, byte-weighted
  chapter offsets, metadata cache semantics.
- CrossMux: container/OPF/NCX/NAV parser structure, series/ISBN metadata, encryption
  manifest probe, multi-CSS discovery.

Content:
- Read Pico `html_text.c`: CSS subset, block/writer state machine, footnote extraction
  (same-file + cross-file), anchors, per-run font family, image refs.
- CrossMux `CssParser`/`CssStyle`: selector matching (compound/descendant/child,
  specificity), rich properties (borders, shading, page-break, initial-letter,
  vertical-align, white-space), `::first-letter`.
- CrossMux `htmlEntities`: entity table.
- CrossMux `ChapterHtmlSlimParser`: tables, drop caps, list handling, heading detection,
  first-line indent, link targets.

Layout:
- Read Pico `book_layout.c`: pixel pagination, CJK kinsoku/line breaking, vertical
  writing, per-run font switching, footnote icon inline placement, image placement,
  balanced CJK columns, chapter lead, reading position mapping.
- CrossMux `Section`/`Page`: page element model, page<->anchor/paragraph/visible-offset
  mapping, first-page heading, border/shaded block decorations, drop-cap page element.

Render:
- Read Pico `ttf_font.c` + `stb_truetype`: glyph rasterization, 16-level coverage,
  vertical drawing rules, run switching.
- CrossMux image converters/dither: JPEG/PNG/GIF -> 1/2-bit dithering, sample row clamp,
  image dimension probe.
- `stb_image` (vendored) for portable JPEG/PNG/GIF/BMP decode; `miniz` for ZIP.

## Workstreams and file ownership

| Workstream | Owner | Files |
| --- | --- | --- |
| scaffold, build, API glue, docs, integration | lead | `CMakeLists.txt`, `include/`, `src/core/base.*`, `src/api/*` (with api teammate), `docs/`, `esp-idf/`, `examples/`, `tools/` |
| container + core utils | container teammate | `src/core/text_util.*`, `src/container/*` |
| CSS + HTML -> Flow | content teammate | `src/content/*` |
| layout + hyphenation | layout teammate | `src/layout/*` |
| canvas/font/image/dither/page render | render teammate | `src/render/*` |
| C API + reader/popup/viewer + CLI + C++ facade | api teammate | `src/api/*`, `tools/`, `include/rolling_epub/rolling_epub.hpp` |

Rule: only edit files inside your scope. Shared headers are changed by Lead only (send a
message if a contract is wrong). New source files are picked up by CMake globs.

## Build

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Per-module compile check while the library is incomplete:

```powershell
g++ -std=c++17 -Iinclude -Isrc -Ithird_party -c src/content/css.cpp -o build/css.o
```

## Testing strategy

- Unit tests per module (`tests/test_<module>.cpp`, harness `tests/test_harness.h`).
- Real EPUB fixtures in `tests/fixtures/epub/` from both projects.
- CLI `repub_cli` renders pages to PGM/PPM for visual verification via Python/PIL.
- Golden checks: pagination determinism, text extraction, anchor mapping, footnote text,
  image decode dimensions/pixels, CSS cascade values, vertical column ordering.
- Host-only fuzz-ish tests: malformed XML/HTML/ZIP must return errors, never crash.

## Milestones

1. M1 — scaffold + contracts + build (Lead). **Done.**
2. M2 — container/book opens the fixtures, metadata/TOC/resources verified. **Done (15 tests).**
3. M3 — content parses chapters (text/blocks/notes/images/anchors). **Done (16 tests).**
4. M4 — layout paginates and maps offsets for real books. **Done (30 tests incl. hyphenation).**
5. M5 — render draws pages; images decode; dithering works. **Done (29 tests).**
6. M6 — C API/reader/popup/zoom wired; CLI renders a book end to end. **Done (10 tests + CLI smoke).**
7. M7 — tests green, feature flags build in reduced configurations, docs/packaging. **Done.**

Post-v1 candidates (documented gaps): synthetic hyphen glyph in the page display
list, flow-level `margin_left/right`, ruby `<rt>` rendering, on-disk pagination
cache, incremental pagination for giant chapters, richer table layout.
