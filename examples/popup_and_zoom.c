/*
 * RollingEPUB example: footnote popup + image zoom viewer.
 *
 * Opens a book, finds the first page with a footnote marker, lays the note out
 * as a popup and writes it as PGM; then finds the first page with an
 * illustration, opens the zoom viewer at 2x and writes that as PGM.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolling_epub/repub.h"

static void write_gray_pgm(const char* path, repub_canvas_t* canvas) {
    const int w = repub_canvas_width(canvas);
    const int h = repub_canvas_height(canvas);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(repub_canvas_data(canvas), 1, (size_t)w * (size_t)h, f);
    fclose(f);
    printf("wrote %s (%dx%d)\n", path, w, h);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <book.epub> [out-prefix]\n", argv[0]);
        return 1;
    }
    const char* prefix = argc > 2 ? argv[2] : "demo";

    repub_platform_t platform = repub_platform_default();
    repub_err_t err = REPUB_OK;

    repub_layout_spec_t spec;
    repub_layout_spec_defaults(&spec);
    spec.page_rect = (repub_rect_t){0, 0, 480, 800};
    spec.font_size_px = 22;

    repub_reader_t* reader = repub_reader_open(&platform, argv[1], &spec, NULL, &err);
    if (!reader) {
        fprintf(stderr, "open failed: %s\n", repub_err_str(err));
        return 1;
    }
    /* Optional: load a TTF face for nicer text (skip when the file is absent). */
    if (argc > 3) {
        FILE* f = fopen(argv[3], "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            void* data = malloc((size_t)size);
            if (data && fread(data, 1, (size_t)size, f) == (size_t)size) {
                repub_font_desc_t desc = {0};
                desc.family = "Demo Serif";
                desc.data = data;
                desc.size = (size_t)size;
                desc.style = REPUB_FONT_REGULAR;
                repub_book_add_font(repub_reader_book(reader), &desc);
                repub_book_set_body_font(repub_reader_book(reader), "Demo Serif",
                                        REPUB_FONT_REGULAR);
                repub_reader_set_layout(reader, &spec); /* reflow with the face */
            }
            free(data);
            fclose(f);
        }
    }

    repub_canvas_t* canvas =
        repub_canvas_create(&platform, 480, 800, REPUB_FORMAT_GRAY8, &err);
    repub_render_options_t options;
    repub_render_options_defaults(&options);
    options.background_level = 255;

    /* ---- Footnote popup ------------------------------------------------- */
    char path[512];
    int found_note = -1;
    for (int page = 0; page < 400; ++page) {
        const repub_page_t* p = repub_reader_page(reader);
        if (p && repub_page_info(p)->note_count > 0) {
            found_note = repub_page_note_at(p, 0)->note_index;
            break;
        }
        if (repub_reader_next_page(reader) != REPUB_OK) break;
    }
    if (found_note >= 0) {
        repub_layout_spec_t popup;
        repub_layout_spec_defaults(&popup);
        popup.page_rect = (repub_rect_t){24, 24, 432, 600};
        popup.font_size_px = 18;
        repub_note_popup_t* note =
            repub_reader_note_popup_open(reader, (size_t)found_note, &popup, &err);
        if (note) {
            repub_canvas_clear(canvas, 255);
            repub_note_popup_render(note, 0, canvas, &options);
            snprintf(path, sizeof(path), "%s-note.pgm", prefix);
            write_gray_pgm(path, canvas);
            printf("popup pages: %zu\n", repub_note_popup_page_count(note));
            repub_note_popup_free(note);
        } else {
            fprintf(stderr, "popup failed: %s\n", repub_err_str(err));
        }
    } else {
        printf("no footnote marker found in the first chapter\n");
    }

    /* ---- Image zoom viewer ---------------------------------------------- */
    /* Start again from the first page and look for an illustration. */
    size_t chapter = repub_reader_chapter_index(reader);
    repub_reader_goto_chapter(reader, chapter);
    int found_image = -1;
    for (int page = 0; page < 400; ++page) {
        const repub_page_t* p = repub_reader_page(reader);
        if (p && repub_page_info(p)->image_count > 0) {
            found_image = repub_page_image_at(p, 0)->image_index;
            break;
        }
        if (repub_reader_next_page(reader) != REPUB_OK) break;
    }
    if (found_image >= 0) {
        repub_image_viewer_t* viewer =
            repub_reader_image_viewer_open(reader, (size_t)found_image, &err);
        if (viewer) {
            repub_canvas_clear(canvas, 255);
            repub_rect_t dest = {0, 0, 480, 800};
            repub_image_viewer_render(viewer, canvas, dest, 2.0f, 0.0f, 0.0f, &options);
            snprintf(path, sizeof(path), "%s-zoom.pgm", prefix);
            write_gray_pgm(path, canvas);
            repub_image_viewer_free(viewer);
        } else {
            fprintf(stderr, "viewer failed: %s\n", repub_err_str(err));
        }
    } else {
        printf("no illustration found in the first chapter\n");
    }

    repub_canvas_destroy(canvas);
    repub_reader_close(reader);
    return 0;
}
