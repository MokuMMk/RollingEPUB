# Porting from CrossSIGO / Read Pico

This document maps the old project APIs to RollingEPUB so an existing reader can
adopt the library incrementally.

## From CrossSIGO / CrossMux (`lib/Epub`)

| Old API | RollingEPUB |
| --- | --- |
| `Epub(filepath, cacheDir)` + `load()` | `repub_book_open(platform, path, options, &err)` |
| `epub.getTitle()/getAuthor()/getLanguage()` | `repub_book_metadata(book)` |
| `epub.getSpineItemsCount()` / `getSpineItem(i)` | `repub_book_spine_count/spine_item` |
| `epub.getTocItemsCount()` / `getTocItem(i)` | `repub_book_toc_count/toc_item` |
| `epub.readItemContentsToBytes(href)` | `repub_book_read_resource` (caller frees) |
| `epub.generateCoverBmp()` | `repub_book_cover_image` (RGBA) + your own encode |
| `Section(epub, spineIndex, renderer)` | `repub_book_chapter_open` + `repub_layout_create` |
| `Section::loadPage(i)` (Page/PageLine) | `repub_layout_page(layout, i)` → `repub_page_span` |
| `Section::findAnchor()` / `getPageForAnchor` | `repub_book_resolve_href` + `repub_layout_page_for_offset` |
| `CssParser` cache on SD | stylesheet parsing is in-memory; disk cache not in v1 |
| `BookMetadataCache` | not needed (parse is fast); optional cache flag for RAM |
| `PageElement` render via `GfxRenderer` | `repub_page_render` or iterate spans into your renderer |
| Hyphenation (`Hyphenator`) | automatically used by layout (`spec.hyphenation`) |
| Image dithering converters | `repub_image_*` + `render_options.text_dither` / `image_view.dither` |

Notes:
- CrossMux caches pagination in `book.bin`/`section.bin`. RollingEPUB v1 keeps
  layouts in RAM. If you need persistent caches, paginate once and serialize
  page display lists yourself, or keep using the old section cache for now.
- CrossMux's renderer is tightly coupled to `GfxRenderer`/`EpdFont`. The library
  draws through its own `FontManager`; the display list API lets you keep
  `GfxRenderer` if desired.
- Tables are flattened to row text in v1 (no grid rules).

## From Read Pico (`main/book`)

| Old API | RollingEPUB |
| --- | --- |
| `book_epub_open(path)` | `repub_book_open` |
| `book_epub_load(chapter, &html_text)` | `repub_book_chapter_open` |
| `book_epub_navigation_*` | `repub_book_toc_*` / navigation heuristics replaced by NCX/NAV + spine |
| `book_epub_chapter_title` | `repub_chapter_title` |
| `book_epub_image(...)` | `repub_book_read_resource` + `repub_image_decode` |
| `book_epub_cover(...)` | `repub_book_cover_image` |
| `book_layout_build_blocks(...)` | `repub_layout_create(chapter, spec, ...)` |
| `book_layout_draw_page(fb, ...)` | `repub_page_render(page, canvas, options)` |
| `book_layout_page_for_offset` | `repub_layout_page_for_offset` |
| `book_layout_set_vertical` | `spec.writing_mode = REPUB_WRITING_VERTICAL_RL` |
| `book_layout_set_spacing/typography/indent` | `repub_layout_spec_t` fields |
| `book_layout_set_inline_images` (footnote icons) | automatic from chapter notes |
| `book_layout_page_note_marker_rect` | `repub_page_note_at` |
| `ttf_font` slot API | `FontManager` (book fonts + built-in face) |
| `book_epub_metadata_cached` | metadata is parsed on open; persist it yourself if needed |
| `book_index_cache` / search | not in v1 (render/search components stay in the reader) |
| `book_txt` / GBK | out of scope (content provider layer) |

### Platform glue

Read Pico uses ESP-IDF VFS directly. Provide RollingEPUB's platform callbacks by
wrapping the same file operations (see `repub_platform_t`). Allocation can stay
on PSRAM (`heap_caps_malloc(MALLOC_CAP_SPIRAM)`) via `allocator.alloc`.

Read Pico's fonts live in `ttf_font.c` with board-specific slots and PIF bitmap
supplements. RollingEPUB keeps a simpler face model: register the same TTF bytes
with `repub_book_add_font`/`repub_book_set_body_font`; the built-in bitmap face
only covers ASCII, so register a CJK TTF for CJK books.

## Adoption order

1. Replace the ZIP/OPF/TOC layer with `repub_book_*`; verify metadata/spine/TOC
   against the old reader on the same books.
2. Replace chapter parsing with `repub_book_chapter_open`; keep your UI/state.
3. Replace pagination with `repub_layout_*`; compare page counts and offsets on
   a fixture set before deleting the old engine.
4. Switch rendering to `repub_page_render` (or consume the display list).
5. Add footnote popups and the image viewer with the ready-made helpers.
6. Delete the duplicated EPUB code and keep only UI/hardware layers.
