# ESP-IDF integration

RollingEPUB is a normal ESP-IDF component. The repository root `CMakeLists.txt`
detects `ESP_PLATFORM` and calls `idf_component_register()` instead of building a
standalone project, so the whole repository can be used directly as a component.

## Add as a local component

```bash
your_project/
  components/
    rolling_epub/        <- this repository (copy, subtree, or symlink)
  main/
```

```cmake
# main/CMakeLists.txt
idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES rolling_epub)
```

The Kconfig menu **RollingEPUB** (see the root `Kconfig`) lets you disable
features such as the TTF rasterizer, image decoding, vertical writing,
hyphenation or CSS. They map to the `REPUB_ENABLE_*` macros.

## Platform callbacks

The default platform uses stdio + malloc, which works on IDF but bypasses VFS
mount checks. For production firmware wrap the VFS calls:

```c
#include "rolling_epub/repub.h"
#include <stdio.h>

static repub_err_t sd_open(void* ctx, const char* path, repub_file_t** out) {
    FILE* f = fopen(path, "rb");
    if (!f) return REPUB_ERR_IO;
    *out = (repub_file_t*)f;
    return REPUB_OK;
}
static int32_t sd_read(void* ctx, repub_file_t* f, void* buf, uint32_t n) {
    size_t got = fread(buf, 1, n, (FILE*)f);
    return got ? (int32_t)got : (ferror((FILE*)f) ? -1 : 0);
}
/* ... seek/tell/size/close ... */

void app_platform(repub_platform_t* p) {
    *p = repub_platform_default();
    p->file_open = sd_open;
    p->file_read = sd_read;
    /* ... */
    p->allocator.alloc = psram_alloc; /* optional */
    p->allocator.realloc = psram_realloc;
    p->allocator.free = psram_free;
}
```

Files must be opened as **binary**; `file_read` returns 0 at EOF and -1 on
error. Paths inside the library are UTF-8. On Windows the default platform
converts to UTF-16 and uses `_wfopen`, but an embedded platform callback receives
the raw UTF-8 path from the API caller.

## Recommended budgets

| Target | Suggestions |
| --- | --- |
| ESP32-C3 (~380 KB RAM) | `REPUB_ENABLE_TTF=OFF`, `REPUB_ENABLE_VERTICAL_TEXT=OFF`, `REPUB_ENABLE_HYPHENATION=OFF`, `REPUB_ENABLE_GRAYSCALE=OFF`, `REPUB_ENABLE_IMAGES=OFF`; wrap a static panel buffer with `repub_canvas_wrap` |
| ESP32-S3 + PSRAM | keep TTF/vertical; put the canvas, chapter text and decoded images in PSRAM |
| Image pages | decode with `repub_book_image_decode(book, href, max_w, max_h, &img)` and draw via `repub_canvas_draw_image` at the fit/zoom you want |

Notes:
- One miniz implementation TU lives in `src/container/zip.cpp`; do not add
  another.
- `stb_image`/`stb_truetype` implementations are single-TU as well.
- The C API is exception-free; verification builds for ESP-IDF should keep the
  default `-fno-exceptions` settings.
