/*
 * RollingEPUB font abstraction and manager.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_RENDER_FONT_H
#define REPUB_SRC_RENDER_FONT_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/base.h"

namespace repub {

struct repub_canvas;
struct repub_image;

/* Metrics consumed by the layout engine.  Implemented by FontManager. */
class FontMetrics {
   public:
    virtual ~FontMetrics() = default;
    /* Advance width of a UTF-8 run for a face at a pixel size. */
    virtual int text_width(int face, int px, std::string_view utf8) const = 0;
    virtual int glyph_advance(int face, int px, uint32_t codepoint) const = 0;
    /* Distance from baseline to top of the line box (positive upward). */
    virtual int ascender(int face, int px) const = 0;
    /* Distance from baseline to bottom of the line box (positive downward). */
    virtual int descender(int face, int px) const = 0;
    virtual int line_height(int face, int px) const = 0;
    virtual bool has_glyph(int face, int px, uint32_t codepoint) const = 0;
};

/* Draw flags for FontManager::draw_text. */
enum DrawFlags : uint32_t {
    DRAW_UNDERLINE = 1u << 0,
    DRAW_STRIKE = 1u << 1,
    DRAW_FAKE_BOLD = 1u << 2,
    DRAW_FAKE_ITALIC = 1u << 3
};

class FontManager : public FontMetrics {
   public:
    explicit FontManager(const repub_platform_t* platform);
    ~FontManager() override;
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    /* Register a TTF/OTF face from memory. Bytes are copied. `style` uses
     * repub_font_style_t.  family is matched case-insensitively. */
    repub_err_t add_face(std::string_view family, const void* data, size_t size,
                         int face_index, int style);
    /* Built-in fallback face id (always valid when REPUB_ENABLE_BUILTIN_FONT). */
    int default_face() const;
    int face_count() const;
    /* Resolve family + style bits to a face; falls back through family,
     * style, then default face. */
    int resolve_face(std::string_view family, uint16_t style_flags) const;
    bool face_is_ttf(int face) const;
    /* Font family name of a registered face ("" for the builtin). */
    std::string face_family(int face) const;

    /* Metrics (FontMetrics).  Face < 0 means default. */
    int text_width(int face, int px, std::string_view utf8) const override;
    int glyph_advance(int face, int px, uint32_t codepoint) const override;
    int ascender(int face, int px) const override;
    int descender(int face, int px) const override;
    int line_height(int face, int px) const override;
    bool has_glyph(int face, int px, uint32_t codepoint) const override;

    /* Draw horizontal text with the baseline at (x, baseline). Returns advance. */
    int draw_text(repub_canvas* canvas, int face, int px, int x, int baseline,
                  std::string_view utf8, int color_level, uint32_t flags) const;
    /* Draw vertical text (top-to-bottom) with the top at (x, y). */
    int draw_text_vertical(repub_canvas* canvas, int face, int px, int x, int y,
                           std::string_view utf8, int color_level,
                           uint32_t flags) const;

    /* Rasterize one codepoint into an 8-bit coverage bitmap (for custom
     * renderers). Returns false when the glyph cannot be rendered. */
    bool glyph_bitmap(int face, int px, uint32_t codepoint, repub_image** out) const;

    /* Statistics (cache size, rasterized glyph count). */
    size_t cached_glyph_count() const;

   private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace repub

#endif  // REPUB_SRC_RENDER_FONT_H
