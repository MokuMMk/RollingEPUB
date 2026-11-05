/*
 * RollingEPUB high-level reader objects (API layer internals).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_API_API_INTERNAL_H
#define REPUB_SRC_API_API_INTERNAL_H

#include <memory>
#include <string>
#include <vector>

#include "container/book.h"
#include "core/base.h"
#include "layout/layout.h"

namespace repub {

/* Public opaque type. */
struct repub_reader {
    const repub_platform_t* platform = nullptr;
    repub_book* book = nullptr;
    repub_layout_spec_t spec{};
    std::string hyphen_lang;

    repub_chapter* chapter = nullptr;
    size_t chapter_index = 0;
    repub_layout* layout = nullptr;
    size_t page = 0;

    ParseOptions parse_options{};
    bool layout_ready = false;
};

struct repub_note_popup {
    repub_reader* reader = nullptr;
    size_t note_index = 0;
    repub_chapter* chapter = nullptr; /* synthetic chapter holding note text */
    repub_layout* layout = nullptr;
};

struct repub_image_viewer {
    repub_reader* reader = nullptr;
    int32_t image_index = -1;
    repub_image* image = nullptr;
};

/* Shared spec validation/defaulting used by the API layer. */
repub_err_t reader_prepare_layout_spec(repub_reader* reader,
                                       const repub_layout_spec_t* requested);
repub_err_t reader_load_chapter(repub_reader* reader, size_t chapter_index,
                                uint32_t text_offset);
repub_err_t reader_ensure_layout(repub_reader* reader);

}  // namespace repub

#endif  // REPUB_SRC_API_API_INTERNAL_H
