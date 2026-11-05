/*
 * RollingEPUB public C API: chapter content wrappers.
 *
 * Chapter parsing itself is owned by the content/container modules; this file
 * only validates public arguments, maps the opaque handles and rebuilds the
 * public mirrors after parsing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstring>
#include <new>

#include "api/api_util.h"

repub_err_t repub_book_chapter_open(repub_book_t* book, size_t spine_index,
                                    repub_chapter_t** out) {
    if (!out) return REPUB_ERR_INVALID_ARG;
    *out = nullptr;
    repub::repub_book* b = repub::api::cast_book(book);
    if (!b) return REPUB_ERR_INVALID_ARG;
    if (spine_index >= b->spine.size()) return REPUB_ERR_NOT_FOUND;
    if (!b->fonts) return REPUB_ERR_INTERNAL;

    repub::repub_chapter* chapter = nullptr;
    const repub::ParseOptions options = repub::api::make_parse_options(*b);
    repub_err_t rc = repub::book_chapter_open(b, spine_index, options, &chapter);
    if (rc != REPUB_OK) {
        delete chapter;
        return rc;
    }
    if (!chapter) return REPUB_ERR_INTERNAL;
    repub::chapter_finalize(chapter);
    *out = reinterpret_cast<repub_chapter_t*>(chapter);
    return REPUB_OK;
}

void repub_chapter_free(repub_chapter_t* chapter) {
    if (!chapter) return;
    delete repub::api::cast_chapter(chapter);
}

size_t repub_chapter_spine_index(const repub_chapter_t* chapter) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    if (!ch) return 0;
    return static_cast<size_t>(ch->spine_index);
}

const char* repub_chapter_title(const repub_chapter_t* chapter) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    if (!ch) return "";
    return ch->title.c_str();
}

const char* repub_chapter_text(const repub_chapter_t* chapter, size_t* len) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    if (!ch) {
        if (len) *len = 0;
        return "";
    }
    if (len) *len = ch->flow.text.size();
    return ch->flow.text.c_str();
}

size_t repub_chapter_block_count(const repub_chapter_t* chapter) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    return ch ? ch->public_blocks.size() : 0;
}

const repub_block_info_t* repub_chapter_block(const repub_chapter_t* chapter,
                                              size_t index) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    if (!ch || index >= ch->public_blocks.size()) return nullptr;
    return &ch->public_blocks[index];
}

size_t repub_chapter_note_count(const repub_chapter_t* chapter) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    return ch ? ch->public_notes.size() : 0;
}

const repub_note_info_t* repub_chapter_note(const repub_chapter_t* chapter,
                                            size_t index) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    if (!ch || index >= ch->public_notes.size()) return nullptr;
    return &ch->public_notes[index];
}

size_t repub_chapter_image_count(const repub_chapter_t* chapter) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    return ch ? ch->public_images.size() : 0;
}

const repub_chapter_image_info_t* repub_chapter_image(
    const repub_chapter_t* chapter, size_t index) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    if (!ch || index >= ch->public_images.size()) return nullptr;
    return &ch->public_images[index];
}

size_t repub_chapter_link_count(const repub_chapter_t* chapter) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    return ch ? ch->public_links.size() : 0;
}

const repub_chapter_link_info_t* repub_chapter_link(const repub_chapter_t* chapter,
                                                    size_t index) {
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    if (!ch || index >= ch->public_links.size()) return nullptr;
    return &ch->public_links[index];
}

repub_err_t repub_chapter_anchor_offset(const repub_chapter_t* chapter,
                                        const char* anchor, uint32_t* offset) {
    if (!chapter || !anchor || !offset) return REPUB_ERR_INVALID_ARG;
    const repub::repub_chapter* ch = repub::api::cast_chapter(chapter);
    if (!ch) return REPUB_ERR_INVALID_ARG;
    *offset = 0;
    const char* id = anchor;
    while (*id == '#') ++id;
    if (*id == '\0') {
        *offset = 0;
        return REPUB_OK;
    }
    for (const repub::FlowAnchor& entry : ch->flow.anchors) {
        if (entry.id == id) {
            *offset = entry.offset;
            return REPUB_OK;
        }
    }
    return REPUB_ERR_NOT_FOUND;
}
