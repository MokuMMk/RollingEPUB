# RollingEPUB — API Guide

This document explains how to use the library. The complete public contract is
[`include/rolling_epub/repub.h`](../include/rolling_epub/repub.h); the internal
architecture is described in [PLAN.md](PLAN.md).

## 1. Layers

You can use the library at three levels:

| Level | Entry points | Use when |
| --- | --- | --- |
| High-level reader | `repub_reader_*` | you want pages, navigation, footnotes, zoom with minimal code |
| Low-level document | `repub_book_*`, `repub_chapter_*`, `repub_layout_*`, `repub_page_*` | you have your own reader UI/state machine but want parsing/layout |
| Primitives | `repub_canvas_*`, `repub_image_*`, `repub_font_*` | you only need image decode/scale/dither or a canvas |

Everything is C ABI, so C, C++, Rust (FFI), Zig etc. can call it. A header-only
C++ RAII facade lives in `rolling_epub.hpp`.

## 2. Platform

```c
repub_platform_t p = repub_platform_default(); /* stdio + malloc + stderr log */
```

For embedded targets replace the file callbacks. They are the only mandatory
I/O; there is no stdio dependency in the library code.

```c
repub_err_t my_open(void* ctx, const char* path, repub_file_t** out);
int32_t    my_read(void* ctx, repub_file_t* f, void* buf, uint32_t n); /* -1 error, 0 EOF */
int32_t    my_seek(void* ctx, repub_file_t* f, int64_t off, int whence); /* 0 ok */
int64_t    my_tell(void* ctx, repub_file_t* f);
int64_t    my_size(void* ctx, repub_file_t* f);
void       my_close(void* ctx, repub_file_t* f);
```

`repub_file_t` is an opaque pointer type; cast your own handle.
The allocator is optional: when `alloc` is NULL the library uses `malloc`/
`free`. Use a PSRAM allocator when available; large buffers (canvas, images,
chapter text) go through it only if you also provide `repub_canvas_create` with
the same platform.

Threading: one `repub_book_t` / `repub_reader_t` / `repub_layout_t` must not be
used from two tasks at once. Separate books are independent.

## 3. Book

```c
repub_book_t* book = repub_book_open(&p, "book.epub", NULL, &err);
const repub_metadata_t* m = repub_book_metadata(book); /* m->title, m->author, ... */
size_t n = repub_book_spine_count(book);
const repub_spine_item_t* item = repub_book_spine_item(book, 0);
size_t toc_n = repub_book_toc_count(book);
```

`repub_open_options_t` (optional) can inject extra CSS (`user_css`) or skip TOC
with `REPUB_OPEN_SKIP_TOC` (saves RAM). All returned strings are owned by the
book and stay valid until `repub_book_close`.

Resource access:

```c
void* data; size_t size;
repub_book_read_resource(book, "OEBPS/images/pic.png", &data, &size); /* caller frees */
repub_image_t* img;
repub_book_image_decode(book, "OEBPS/images/pic.png", 0, 0, &img);
```

Relative hrefs are resolved against the OPF directory; absolute/relative forms
both work.

### Fonts

```c
repub_font_desc_t f = { .family="Noto Serif", .data=ttf, .size=ttf_len,
                        .face_index=0, .style=REPUB_FONT_REGULAR };
repub_book_add_font(book, &f);        /* bytes are copied */
repub_book_set_body_font(book, "Noto Serif", REPUB_FONT_REGULAR);
```

CSS `font-family` names are matched case-insensitively to registered faces.
Bold/italic faces are selected by style; if missing, the regular face is used
(optionally with synthetic emboldening by the rasterizer). Embedded book fonts
(`@font-face`, packaged TTF) load automatically when `REPUB_ENABLE_TTF=1`;
disable with `repub_book_set_embedded_fonts(book, 0)`. Face 0 is always the
built-in bitmap font, so rendering works with no external font.

## 4. Chapters and content model

```c
repub_chapter_t* ch;
repub_book_chapter_open(book, spine_index, &ch);
const char* text = repub_chapter_text(ch, &len);   /* normalized UTF-8 */
size_t blocks = repub_chapter_block_count(ch);
const repub_block_info_t* b = repub_chapter_block(ch, 0);
```

- `text` contains paragraph separators (`\n`); block offsets index into it.
- Blocks carry type (paragraph/heading/blockquote/pre/list item/hr/image/table
  row), heading level, style flags, margins/indent, font scale, image and link
  index.
- Notes (`repub_chapter_note`) give marker offset, note text, original href and
  an optional icon image index.
- Images (`repub_chapter_image`) give resolved href and probed dimensions.
- Anchors: `repub_chapter_anchor_offset(ch, "n1", &offset)`.

Chapter parsing is the expensive step. The high-level reader caches the current
chapter; use it unless you need your own cache.

## 5. Layout

```c
repub_layout_spec_t spec;
repub_layout_spec_defaults(&spec);
spec.page_rect = (repub_rect_t){0, 0, 480, 800};
spec.font_size_px = 22;         /* body size */
spec.line_height_percent = 160;
spec.first_line_indent_em = 2;
spec.paragraph_gap_percent = 60;
spec.justify = 1;
spec.hyphenation = 1;           /* needs REPUB_ENABLE_HYPHENATION */
spec.writing_mode = REPUB_WRITING_HORIZONTAL; /* or REPUB_WRITING_VERTICAL_RL */
spec.images_visible = 1;
spec.image_full_bleed = 0;
spec.margin_left_px = spec.margin_right_px = 0;

repub_layout_t* layout;
repub_layout_create(chapter, &spec, &layout);
size_t pages = repub_layout_page_count(layout);
const repub_page_t* page = repub_layout_page(layout, 0);
```

Spec fields not initialized by `repub_layout_spec_defaults` (page_rect,
font_size_px) must be set. `page_rect` is the body area inside the screen; the
caller draws headers/footers outside it.

The layout is deterministic: same bytes + same spec + same font metrics produce
identical pages on any platform (font metrics may differ across fonts/devices).

### Page display list

`repub_page_*` gives a renderer-independent display list:

- `repub_page_info`: start/end text offset, size, primary image, counts.
- `repub_span`: text range, position, size, style flags, line/column index,
  font size. Horizontal: x=left, y=baseline, width=advance, height=line height.
  Vertical: x=column left, y=top, width=column pitch, height=run length.
- `repub_page_image_at`: image index (into chapter images) + destination rect.
- `repub_page_note_at`: note index + tap rect (+ icon index).
- `repub_page_link_at`: href + tap rect.

Use these if you render with your own engine (e.g. a GPU or a custom e-ink
driver). Otherwise call `repub_page_render`.

## 6. Canvas and rendering

```c
repub_canvas_t* canvas = repub_canvas_create(&p, 480, 800, REPUB_FORMAT_MONO1, &err);
/* or wrap your panel buffer: */
repub_canvas_t* c2 = repub_canvas_wrap(&p, panel_buf, 480, 800, 60, REPUB_FORMAT_MONO1, &err);

repub_render_options_t ro;
repub_render_options_defaults(&ro);
ro.background_level = 1;   /* 1 = white for MONO1; 15 for GRAY4; 255 for GRAY8 */
ro.flags |= REPUB_RENDER_UNDERLINE_LINKS;
repub_page_render(page, canvas, &ro);
```

Formats: `MONO1` (1bpp packed, 0=black, 1=white), `GRAY2` (0..3), `GRAY4`
(0..15), `GRAY8` (0..255). Text antialiasing uses coverage; for 1/2-bit output
the renderer dithers/thresholds per `text_dither`/`threshold`.

`background_level = -1` keeps the existing canvas content (useful for partial
updates/overlays).

## 7. Reader

```c
repub_reader_t* r = repub_reader_open(&p, "book.epub", &spec, NULL, &err);
repub_reader_render(r, canvas, &ro);

repub_reader_next_page(r);
repub_reader_prev_page(r);
repub_reader_goto_chapter(r, 3);
repub_reader_goto_href(r, "chapter2.xhtml#target-anchor");
repub_reader_goto_progress(r, 0.42f);
float progress = repub_reader_progress(r);   /* 0..1 byte weighted */
```

`repub_reader_set_layout` reflows the current chapter while keeping the reading
position at the top of the current page (progress preserving). Page indices are
local to the current chapter; `repub_reader_page_count` is the current chapter's
page count (use `repub_reader_progress` for whole-book position).

`repub_reader_attach(book, spec, &err)` lets you configure the book (custom
fonts, options) before the first layout; the reader takes ownership of the book.

## 8. Footnotes (popup)

```c
int note = repub_reader_note_at(r, x, y);       /* -1 when no marker hit */
if (note >= 0) {
    const char* text; size_t len;
    repub_reader_note_text(r, note, &text, &len);   /* if you render it yourself */

    repub_layout_spec_t popup;
    repub_layout_spec_defaults(&popup);
    popup.page_rect = (repub_rect_t){24, 24, 432, 500};  /* popup body */
    repub_note_popup_t* p = repub_reader_note_popup_open(r, note, &popup, &err);
    for (size_t i = 0; i < repub_note_popup_page_count(p); i++)
        repub_note_popup_render(p, i, canvas, &ro);
    repub_note_popup_free(p);
}
```

The popup is a normal layout over the note text, so long notes paginate and
scroll. Marker hit-testing covers both text markers and icon markers (icon
markers report `icon >= 0` in the page note table).

## 9. Images and zoom

```c
int image = repub_reader_image_at(r, x, y);       /* page image under point */
repub_image_t* img;
repub_reader_image_decode(r, image, 0, 0, &img);  /* RGBA8 */
/* simple draw without the viewer: */
repub_image_view_t view = {0};
view.struct_size = sizeof(view);
view.dest = (repub_rect_t){0, 0, 480, 800};
view.fit = REPUB_IMAGE_FIT_CONTAIN;
view.zoom = 2.0f;      /* 2x fit size */
view.pan_x = 0.2f;     /* normalized pan */
view.dither = REPUB_DITHER_FLOYD_STEINBERG;
repub_canvas_draw_image(canvas, img, &view);

/* or the stateful viewer (keeps the decoded image): */
repub_image_viewer_t* v = repub_reader_image_viewer_open(r, image, &err);
repub_image_viewer_render(v, canvas, (repub_rect_t){0,0,480,800}, 2.0f, 0, 0, &ro);
repub_image_viewer_free(v);
```

Zoom is relative to the fit size; the viewer clamps zoom to a sane range and
clips panning so no empty border appears. Transparent pixels are composited on
`background_level`.

## 10. Standalone text layout

Useful for dialogs, search results, dictionary popups, excerpts:

```c
repub_layout_t* l;
repub_book_layout_text(book, text, len, &spec, &l);
```

The text is treated as a single paragraph (newlines preserved) with the book's
fonts. The layout is owned by the caller (`repub_layout_free`).

## 11. Errors and bounds

Every fallible function returns `repub_err_t` or takes `repub_err_t*`. Strings
from `repub_err_str` are static. The library never throws; it never aborts on
malformed input. Hard bounds (`REPUB_MAX_*`) limit memory and page counts; when
a limit is hit the call returns `REPUB_ERR_LIMIT` (or truncates and logs, for
pagination).

`struct_size` fields: pass `sizeof(...)`; a value of 0 means "use defaults".
Fields added in future versions keep working when `struct_size` is correct.

## 12. Feature flags

Check at runtime:

```c
if (repub_feature_enabled(REPUB_FEATURE_TTF)) { ... }
```

and at compile time with `REPUB_ENABLE_*` macros. Build a reduced library for
small devices (see README). APIs for disabled features return
`REPUB_ERR_UNSUPPORTED` instead of failing silently.

## 13. Memory model

- Book/reader own their chapters, layouts and fonts; free them with the
  matching close/free function.
- Canvas can be owned by the library or wrap your framebuffer; only owned
  canvases are freed by `repub_canvas_destroy`.
- Images returned by decode functions are caller-owned (`repub_image_free`).
- `repub_read_resource` returns a caller-owned buffer (`repub_free`).
- Strings reached through const pointers are borrowed from their owner; do not
  free them.
- The high-level reader caches one chapter and one layout; memory peaks at
  roughly chapter bytes + page display lists. Close the reader when switching
  books to release caches.

## 14. Best practices for e-ink readers

- Reuse one canvas (wrap the panel buffer) and call `repub_reader_render` per
  page; pass `background_level` to control full vs partial refresh.
- Set `REPUB_RENDER_DRAW_IMAGES` off for fast text-only page turns, then render
  images lazily (image-only pages may need a full refresh on e-ink).
- Prefer `MONO1` for fast turns and `GRAY4` for image pages; `GRAY4` layouts are
  identical (layout only depends on metrics, not canvas format).
- Keep `REPUB_ENABLE_TTF` on only if you need quality typography or CJK; the
  built-in bitmap font is tiny but ASCII-only.
- Use `repub_reader_set_layout` on font/margin changes instead of reopening the
  book; it preserves the reading position.
