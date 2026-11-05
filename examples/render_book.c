/*
 * RollingEPUB minimal host example.
 *
 * Opens an EPUB, paginates it for a 480x800 page and writes the first pages as
 * binary PGM (P5) images.  Build with the library (see README) or use repub_cli.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolling_epub/repub.h"

static int write_pgm(const char* path, const repub_canvas_t* canvas) {
    const int w = repub_canvas_width(canvas);
    const int h = repub_canvas_height(canvas);
    const uint8_t* pixels = (const uint8_t*)repub_canvas_data(canvas);
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    /* GRAY8 canvas, 0 = black, 255 = white. */
    fwrite(pixels, 1, (size_t)w * (size_t)h, f);
    fclose(f);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <book.epub> [page-count] [prefix]\n", argv[0]);
        return 1;
    }
    const int page_limit = argc > 2 ? atoi(argv[2]) : 3;
    const char* prefix = argc > 3 ? argv[3] : "page";

    repub_platform_t platform = repub_platform_default();
    repub_err_t err = REPUB_OK;

    repub_layout_spec_t spec;
    repub_layout_spec_defaults(&spec);
    spec.page_rect = (repub_rect_t){0, 0, 480, 800};
    spec.font_size_px = 22;
    spec.line_height_percent = 160;
    spec.first_line_indent_em = 2;
    spec.paragraph_gap_percent = 60;
    spec.justify = 1;
    spec.images_visible = 1;

    repub_reader_t* reader =
        repub_reader_open(&platform, argv[1], &spec, NULL, &err);
    if (!reader) {
        fprintf(stderr, "cannot open %s: %s\n", argv[1], repub_err_str(err));
        return 1;
    }

    printf("title:    %s\n", repub_book_metadata(repub_reader_book(reader))->title);
    printf("author:   %s\n", repub_book_metadata(repub_reader_book(reader))->author);
    printf("spine:    %zu\n", repub_book_spine_count(repub_reader_book(reader)));
    printf("toc:      %zu\n", repub_book_toc_count(repub_reader_book(reader)));
    printf("chapter:  %zu/%zu pages=%zu\n", repub_reader_chapter_index(reader) + 1,
           repub_reader_chapter_count(reader), repub_reader_page_count(reader));

    repub_canvas_t* canvas =
        repub_canvas_create(&platform, 480, 800, REPUB_FORMAT_GRAY8, &err);
    if (!canvas) {
        fprintf(stderr, "canvas: %s\n", repub_err_str(err));
        repub_reader_close(reader);
        return 1;
    }
    repub_render_options_t options;
    repub_render_options_defaults(&options);
    options.background_level = 255;

    for (int i = 0; i < page_limit; ++i) {
        char path[512];
        snprintf(path, sizeof(path), "%s-%02d.pgm", prefix, i);
        if (repub_reader_render(reader, canvas, &options) == REPUB_OK) {
            if (write_pgm(path, canvas) == 0) printf("wrote %s\n", path);
        }
        if (repub_reader_next_page(reader) != REPUB_OK) break;
    }

    repub_canvas_destroy(canvas);
    repub_reader_close(reader);
    return 0;
}
