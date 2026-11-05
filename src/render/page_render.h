/*
 * RollingEPUB page renderer (text spans, images, footnote markers, links).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_RENDER_PAGE_RENDER_H
#define REPUB_SRC_RENDER_PAGE_RENDER_H

#include "core/base.h"
#include "layout/layout.h"
#include "render/canvas.h"
#include "render/font.h"

namespace repub {

struct repub_book;

/* Render one page.  Images are read from the book and decoded on demand;
 * decode failures leave the reserved rectangle empty (never fail the page). */
repub_err_t render_page(const repub_page& page, const repub_book& book,
                        repub_canvas& canvas, const FontManager& fonts,
                        const repub_render_options_t& options);

/* Fill a helper options struct with defaults. */
void render_options_defaults(repub_render_options_t* options);

}  // namespace repub

#endif  // REPUB_SRC_RENDER_PAGE_RENDER_H
