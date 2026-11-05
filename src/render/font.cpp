/*
 * RollingEPUB font manager: built-in bitmap fallback + TrueType faces,
 * glyph rasterization cache, metrics, horizontal/vertical drawing.
 *
 * Rasterization
 * -------------
 * TTF glyphs come from stb_truetype (public domain / MIT, vendored in
 * third_party/stb_truetype.h).  Small pixel sizes are rendered at 2x-4x and
 * box-downsampled so that antialiasing stays smooth for e-ink panels; the
 * cached glyph is an 8-bit coverage bitmap (0 = transparent, 255 = ink).
 *
 * The glyph cache is keyed by (face, pixel size, codepoint) and bounded to
 * kMaxCachedGlyphs entries with LRU eviction, which keeps a full page of
 * CJK text in memory without unbounded growth.
 *
 * The built-in ASCII bitmap fallback lives in font_builtin.cpp and is face 0
 * whenever REPUB_ENABLE_BUILTIN_FONT is on.  A TTF face that lacks a glyph
 * also falls back to the built-in box/ASCII renderer, so text is never
 * silently invisible.
 *
 * SPDX-License-Identifier: MIT
 */

#include "render/font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "render/canvas.h"
#include "render/image.h"
#include "rolling_epub/repub_config.h"

#if REPUB_ENABLE_TTF
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

namespace repub {

/* ---- Built-in bitmap fallback (implemented in font_builtin.cpp) -------- */
namespace builtin_font {
bool has_glyph(uint32_t cp);
int glyph_advance(int px, uint32_t cp);
bool rasterize(int px, uint32_t cp, int* out_w, int* out_h, int* out_advance,
               int* out_bearing_x, int* out_bearing_y, std::vector<uint8_t>* coverage);
int ascender(int px);
int descender(int px);
int line_height(int px);
}  // namespace builtin_font

namespace {

/* RunFlags / REPUB_SPAN_* bits used for style resolution. */
constexpr uint16_t kStyleBold = 1u << 0;
constexpr uint16_t kStyleItalic = 1u << 1;

constexpr size_t kMaxCachedGlyphs = 2048;

struct Glyph {
    int w = 0;
    int h = 0;
    int advance = 0;
    int bearing_x = 0; /* bitmap left edge relative to pen x */
    int bearing_y = 0; /* bitmap top edge above the baseline (positive up) */
    std::vector<uint8_t> coverage;
};

struct Face {
    bool ttf = false;
    bool builtin = false;
    std::string family; /* original spelling, matched case-insensitively */
    int style = 0;      /* repub_font_style_t */
    int face_index = 0;
    std::vector<uint8_t> data;
#if REPUB_ENABLE_TTF
    stbtt_fontinfo font{};
    bool font_ok = false;
#endif
};

struct CacheKey {
    int face = -1;
    int px = 0;
    uint32_t cp = 0;

    bool operator==(const CacheKey& other) const {
        return face == other.face && px == other.px && cp == other.cp;
    }
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& key) const {
        size_t h = static_cast<size_t>(key.face) * 0x9E3779B1u;
        h ^= static_cast<size_t>(key.px) * 0x85EBCA77u;
        h ^= static_cast<size_t>(key.cp) * 0xC2B2AE3Du;
        return h;
    }
};

std::string lower_ascii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            out.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

/* Decode one UTF-8 codepoint; invalid bytes yield U+FFFD. */
size_t decode_utf8(const char* data, size_t size, uint32_t* cp) {
    if (size == 0) {
        *cp = 0xFFFD;
        return 0;
    }
    const uint8_t b0 = static_cast<uint8_t>(data[0]);
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }
    size_t need = 0;
    uint32_t value = 0;
    if ((b0 & 0xE0) == 0xC0) {
        need = 1;
        value = b0 & 0x1Fu;
    } else if ((b0 & 0xF0) == 0xE0) {
        need = 2;
        value = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
        need = 3;
        value = b0 & 0x07u;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    if (need + 1 > size) {
        *cp = 0xFFFD;
        return 1;
    }
    for (size_t i = 1; i <= need; ++i) {
        const uint8_t b = static_cast<uint8_t>(data[i]);
        if ((b & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return i;
        }
        value = (value << 6) | (b & 0x3Fu);
    }
    /* Reject overlong / out of range sequences. */
    static const uint32_t kMinValue[4] = {0, 0x80, 0x800, 0x10000};
    if (value < kMinValue[need] || value > 0x10FFFFu ||
        (value >= 0xD800u && value <= 0xDFFFu)) {
        *cp = 0xFFFD;
        return need + 1;
    }
    *cp = value;
    return need + 1;
}

/* Ideographic codepoints are drawn upright in vertical writing. */
bool is_upright_vertical(uint32_t cp) {
    if (cp < 0x2E80u) return false;
    if (cp >= 0xFF61u && cp <= 0xFF9Fu) return true; /* halfwidth katakana */
    return true;
}

}  // namespace

/* ---------------------------------------------------------------------- */
/* Impl                                                                   */
/* ---------------------------------------------------------------------- */

struct FontManager::Impl {
    struct CacheEntry {
        Glyph glyph;
        std::list<CacheKey>::iterator lru_it;
    };

    const repub_platform_t* platform = nullptr;
    std::vector<Face> faces;
    mutable std::list<CacheKey> lru; /* front = most recently used */
    mutable std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache;

    int normalized_face(int face) const {
        if (face >= 0 && static_cast<size_t>(face) < faces.size()) return face;
        return default_face_id();
    }

    int default_face_id() const {
        if (faces.empty()) return -1;
        for (size_t i = 0; i < faces.size(); ++i) {
            if (faces[i].builtin) return static_cast<int>(i);
        }
        return 0;
    }

    int body_face(int style) const {
        /* Preferred body face: first real TTF matching the style, else the
         * builtin fallback.  Styles fall back regular -> bold -> italic. */
        int order[3];
        int n = 0;
        order[n++] = style;
        if (style & kStyleBold) order[n++] = style & ~static_cast<int>(kStyleBold);
        if (style & kStyleItalic) order[n++] = style & ~static_cast<int>(kStyleItalic);
        order[n++] = 0;
        for (int i = 0; i < n; ++i) {
            for (size_t f = 0; f < faces.size(); ++f) {
                if (faces[f].builtin) continue;
                if (faces[f].style == order[i]) return static_cast<int>(f);
            }
        }
        return default_face_id();
    }

    int find_family_style(std::string_view family_lower, int style) const {
        for (size_t f = 0; f < faces.size(); ++f) {
            if (faces[f].builtin) continue;
            if (faces[f].style != style) continue;
            if (lower_ascii(faces[f].family) == family_lower) return static_cast<int>(f);
        }
        return -1;
    }

    int find_family_any(std::string_view family_lower) const {
        for (size_t f = 0; f < faces.size(); ++f) {
            if (faces[f].builtin) continue;
            if (lower_ascii(faces[f].family) == family_lower) return static_cast<int>(f);
        }
        return -1;
    }

    /* Advance without rasterizing the bitmap. */
    int advance_for(const Face& face, int px, uint32_t cp) const {
        if (px < 1) px = 1;
        if (face.builtin) {
#if REPUB_ENABLE_BUILTIN_FONT
            return builtin_font::glyph_advance(px, cp);
#else
            (void)cp;
            return std::max(1, px / 2);
#endif
        }
#if REPUB_ENABLE_TTF
        if (face.font_ok) {
            const int glyph_index = stbtt_FindGlyphIndex(&face.font, static_cast<int>(cp));
            if (glyph_index != 0) {
                int advance = 0;
                int lsb = 0;
                stbtt_GetGlyphHMetrics(&face.font, glyph_index, &advance, &lsb);
                const float scale = stbtt_ScaleForPixelHeight(&face.font, static_cast<float>(px));
                const int out = static_cast<int>(std::lround(static_cast<float>(advance) * scale));
                return out < 0 ? 0 : out;
            }
        }
#endif
#if REPUB_ENABLE_BUILTIN_FONT
        return builtin_font::glyph_advance(px, cp);
#else
        return std::max(1, px / 2);
#endif
    }

    void rasterize_builtin(int px, uint32_t cp, Glyph* glyph) const {
#if REPUB_ENABLE_BUILTIN_FONT
        builtin_font::rasterize(px, cp, &glyph->w, &glyph->h, &glyph->advance,
                                &glyph->bearing_x, &glyph->bearing_y, &glyph->coverage);
#else
        (void)px;
        (void)cp;
        glyph->w = 0;
        glyph->h = 0;
        glyph->advance = 0;
        glyph->coverage.clear();
#endif
    }

#if REPUB_ENABLE_TTF
    void rasterize_ttf(const Face& face, int px, uint32_t cp, Glyph* glyph) const {
        const int glyph_index = stbtt_FindGlyphIndex(&face.font, static_cast<int>(cp));
        if (glyph_index == 0) {
            rasterize_builtin(px, cp, glyph);
            return;
        }
        /* Supersample small sizes for a smoother 8-bit coverage bitmap. */
        int supersample = 1;
        if (px <= 6) {
            supersample = 4;
        } else if (px <= 12) {
            supersample = 2;
        }
        const int render_px = px * supersample;
        const float render_scale = stbtt_ScaleForPixelHeight(&face.font, static_cast<float>(render_px));

        int w = 0;
        int h = 0;
        int xoff = 0;
        int yoff = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&face.font, render_scale, render_scale,
                                                         static_cast<int>(cp), &w, &h, &xoff, &yoff);

        const float final_scale = stbtt_ScaleForPixelHeight(&face.font, static_cast<float>(px));
        int advance = 0;
        int lsb = 0;
        stbtt_GetGlyphHMetrics(&face.font, glyph_index, &advance, &lsb);
        glyph->advance = static_cast<int>(std::lround(static_cast<float>(advance) * final_scale));
        if (glyph->advance < 0) glyph->advance = 0;

        if (!bitmap || w <= 0 || h <= 0) {
            if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);
            glyph->w = 0;
            glyph->h = 0;
            glyph->coverage.clear();
            return;
        }

        const int out_w = (w + supersample - 1) / supersample;
        const int out_h = (h + supersample - 1) / supersample;
        glyph->w = out_w;
        glyph->h = out_h;
        glyph->bearing_x =
            static_cast<int>(std::lround(static_cast<float>(xoff) / static_cast<float>(supersample)));
        glyph->bearing_y =
            -static_cast<int>(std::lround(static_cast<float>(yoff) / static_cast<float>(supersample)));
        glyph->coverage.assign(static_cast<size_t>(out_w) * static_cast<size_t>(out_h), 0);

        for (int dy = 0; dy < out_h; ++dy) {
            for (int dx = 0; dx < out_w; ++dx) {
                int sum = 0;
                int count = 0;
                for (int sy = 0; sy < supersample; ++sy) {
                    const int src_y = dy * supersample + sy;
                    if (src_y >= h) continue;
                    for (int sx = 0; sx < supersample; ++sx) {
                        const int src_x = dx * supersample + sx;
                        if (src_x >= w) continue;
                        sum += bitmap[static_cast<size_t>(src_y) * static_cast<size_t>(w) +
                                     static_cast<size_t>(src_x)];
                        ++count;
                    }
                }
                if (count > 0) {
                    glyph->coverage[static_cast<size_t>(dy) * static_cast<size_t>(out_w) + dx] =
                        static_cast<uint8_t>((sum + count / 2) / count);
                }
            }
        }
        stbtt_FreeBitmap(bitmap, nullptr);
    }
#endif

    const Glyph* rasterize(int face, int px, uint32_t cp) const {
        const int normalized = normalized_face(face);
        if (normalized < 0) return nullptr;
        if (px < 1) px = 1;
        if (px > 512) px = 512;

        CacheKey key{normalized, px, cp};
        auto found = cache.find(key);
        if (found != cache.end()) {
            lru.splice(lru.begin(), lru, found->second.lru_it);
            return &found->second.glyph;
        }

        Glyph glyph;
        const Face& f = faces[static_cast<size_t>(normalized)];
        if (cp == 0xFFFC) {
            /* U+FFFC object replacement: blank, zero advance. */
            glyph.w = 0;
            glyph.h = 0;
            glyph.advance = 0;
        } else if (f.builtin) {
            rasterize_builtin(px, cp, &glyph);
        } else {
#if REPUB_ENABLE_TTF
            rasterize_ttf(f, px, cp, &glyph);
#else
            rasterize_builtin(px, cp, &glyph);
#endif
        }

        if (cache.size() >= kMaxCachedGlyphs && !lru.empty()) {
            const CacheKey victim = lru.back();
            lru.pop_back();
            cache.erase(victim);
        }
        lru.push_front(key);
        CacheEntry entry;
        entry.glyph = std::move(glyph);
        entry.lru_it = lru.begin();
        auto inserted = cache.emplace(key, std::move(entry));
        return &inserted.first->second.glyph;
    }

    void draw_glyph(repub_canvas* canvas, const Glyph& glyph, int x, int baseline, int color_level,
                    bool fake_bold, bool fake_italic) const {
        if (!canvas || glyph.w <= 0 || glyph.h <= 0) return;
        const float shear = fake_italic ? 0.22f : 0.0f;
        for (int pass = 0; pass < (fake_bold ? 2 : 1); ++pass) {
            const int bold_offset = pass;
            for (int row = 0; row < glyph.h; ++row) {
                const int y = baseline - glyph.bearing_y + row;
                int shift = 0;
                if (fake_italic) {
                    shift = static_cast<int>(std::lround(static_cast<float>(baseline - y) * shear));
                }
                const uint8_t* line = glyph.coverage.data() +
                                      static_cast<size_t>(row) * static_cast<size_t>(glyph.w);
                for (int col = 0; col < glyph.w; ++col) {
                    const uint8_t coverage = line[col];
                    if (!coverage) continue;
                    canvas->blend_pixel(x + glyph.bearing_x + col + shift + bold_offset, y,
                                        color_level, coverage);
                }
            }
        }
    }

    int face_ascender(int face, int px) const {
        if (px < 1) px = 1;
        const int normalized = normalized_face(face);
        if (normalized < 0) return px;
        const Face& f = faces[static_cast<size_t>(normalized)];
        if (f.builtin) {
#if REPUB_ENABLE_BUILTIN_FONT
            return builtin_font::ascender(px);
#else
            return px;
#endif
        }
#if REPUB_ENABLE_TTF
        if (f.font_ok) {
            int ascent = 0;
            int descent = 0;
            int line_gap = 0;
            stbtt_GetFontVMetrics(&f.font, &ascent, &descent, &line_gap);
            const float scale = stbtt_ScaleForPixelHeight(&f.font, static_cast<float>(px));
            const int out = static_cast<int>(std::ceil(static_cast<float>(ascent) * scale));
            return out > 0 ? out : px;
        }
#endif
        return px;
    }

    int face_descender(int face, int px) const {
        if (px < 1) px = 1;
        const int normalized = normalized_face(face);
        if (normalized < 0) return 0;
        const Face& f = faces[static_cast<size_t>(normalized)];
        if (f.builtin) {
#if REPUB_ENABLE_BUILTIN_FONT
            return builtin_font::descender(px);
#else
            return 0;
#endif
        }
#if REPUB_ENABLE_TTF
        if (f.font_ok) {
            int ascent = 0;
            int descent = 0;
            int line_gap = 0;
            stbtt_GetFontVMetrics(&f.font, &ascent, &descent, &line_gap);
            const float scale = stbtt_ScaleForPixelHeight(&f.font, static_cast<float>(px));
            const int out = static_cast<int>(std::ceil(-static_cast<float>(descent) * scale));
            return out > 0 ? out : 0;
        }
#endif
        return 0;
    }
};

/* ---------------------------------------------------------------------- */
/* FontManager public API                                                 */
/* ---------------------------------------------------------------------- */

FontManager::FontManager(const repub_platform_t* platform) : impl_(new Impl()) {
    impl_->platform = effective_platform(platform);
#if REPUB_ENABLE_BUILTIN_FONT
    Face builtin;
    builtin.builtin = true;
    builtin.ttf = false;
    builtin.style = REPUB_FONT_REGULAR;
    impl_->faces.push_back(std::move(builtin));
#endif
}

FontManager::~FontManager() { delete impl_; }

repub_err_t FontManager::add_face(std::string_view family, const void* data, size_t size,
                                  int face_index, int style) {
    if (!data || size == 0) return REPUB_ERR_INVALID_ARG;
#if !REPUB_ENABLE_TTF
    (void)family;
    (void)size;
    (void)face_index;
    (void)style;
    return REPUB_ERR_UNSUPPORTED;
#else
    Face face;
    face.ttf = true;
    face.builtin = false;
    face.family.assign(family);
    face.style = style & 0x3;
    face.face_index = face_index < 0 ? 0 : face_index;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    face.data.assign(bytes, bytes + size);
    const int offset = stbtt_GetFontOffsetForIndex(face.data.data(), face.face_index);
    if (offset < 0) return REPUB_ERR_FORMAT;
    if (!stbtt_InitFont(&face.font, face.data.data(), offset)) return REPUB_ERR_FORMAT;
    face.font_ok = true;
    impl_->faces.push_back(std::move(face));
    return REPUB_OK;
#endif
}

int FontManager::default_face() const { return impl_->default_face_id(); }

int FontManager::face_count() const { return static_cast<int>(impl_->faces.size()); }

bool FontManager::face_is_ttf(int face) const {
    if (face < 0 || static_cast<size_t>(face) >= impl_->faces.size()) return false;
    return impl_->faces[static_cast<size_t>(face)].ttf;
}

std::string FontManager::face_family(int face) const {
    if (face < 0 || static_cast<size_t>(face) >= impl_->faces.size()) return std::string();
    return impl_->faces[static_cast<size_t>(face)].family;
}

int FontManager::resolve_face(std::string_view family, uint16_t style_flags) const {
    const int style = ((style_flags & kStyleBold) ? REPUB_FONT_BOLD : 0) |
                      ((style_flags & kStyleItalic) ? REPUB_FONT_ITALIC : 0);
    /* Style fallback order: exact, without bold, without italic, regular. */
    int order[4];
    int count = 0;
    order[count++] = style;
    if (style & REPUB_FONT_BOLD) order[count++] = style & ~REPUB_FONT_BOLD;
    if (style & REPUB_FONT_ITALIC) order[count++] = style & ~REPUB_FONT_ITALIC;
    order[count++] = REPUB_FONT_REGULAR;

    if (!family.empty()) {
        const std::string wanted = lower_ascii(family);
        for (int i = 0; i < count; ++i) {
            const int face = impl_->find_family_style(wanted, order[i]);
            if (face >= 0) return face;
        }
        /* Family exists but none of the styles do: use any face of that family. */
        const int any = impl_->find_family_any(wanted);
        if (any >= 0) return any;
    }
    return impl_->body_face(style);
}

int FontManager::text_width(int face, int px, std::string_view utf8) const {
    if (utf8.empty()) return 0;
    int width = 0;
    size_t offset = 0;
    while (offset < utf8.size()) {
        uint32_t cp = 0;
        const size_t used = decode_utf8(utf8.data() + offset, utf8.size() - offset, &cp);
        if (used == 0) break;
        offset += used;
        if (cp == 0xFFFC) continue;
        width += glyph_advance(face, px, cp);
    }
    return width;
}

int FontManager::glyph_advance(int face, int px, uint32_t codepoint) const {
    const int normalized = impl_->normalized_face(face);
    if (normalized < 0) return 0;
    if (px < 1) px = 1;
    return impl_->advance_for(impl_->faces[static_cast<size_t>(normalized)], px, codepoint);
}

int FontManager::ascender(int face, int px) const { return impl_->face_ascender(face, px); }

int FontManager::descender(int face, int px) const { return impl_->face_descender(face, px); }

int FontManager::line_height(int face, int px) const {
    if (px < 1) px = 1;
    const int asc = impl_->face_ascender(face, px);
    const int desc = impl_->face_descender(face, px);
    int leading = static_cast<int>(std::lround(static_cast<float>(px) * 0.175f));
    if (leading < 1) leading = 1;
    return asc + desc + leading;
}

bool FontManager::has_glyph(int face, int px, uint32_t codepoint) const {
    const int normalized = impl_->normalized_face(face);
    if (normalized < 0) return false;
    (void)px;
    const Face& f = impl_->faces[static_cast<size_t>(normalized)];
    if (codepoint == 0xFFFC) return true;
    if (f.builtin) {
#if REPUB_ENABLE_BUILTIN_FONT
        return builtin_font::has_glyph(codepoint);
#else
        return false;
#endif
    }
#if REPUB_ENABLE_TTF
    if (f.font_ok) {
        return stbtt_FindGlyphIndex(&f.font, static_cast<int>(codepoint)) != 0;
    }
#endif
    return false;
}

int FontManager::draw_text(repub_canvas* canvas, int face, int px, int x, int baseline,
                           std::string_view utf8, int color_level, uint32_t flags) const {
    if (!canvas || utf8.empty()) return 0;
    if (px < 1) px = 1;
    const bool fake_bold = (flags & DRAW_FAKE_BOLD) != 0;
    const bool fake_italic = (flags & DRAW_FAKE_ITALIC) != 0;

    const int start_x = x;
    int pen = x;
    size_t offset = 0;
    while (offset < utf8.size()) {
        uint32_t cp = 0;
        const size_t used = decode_utf8(utf8.data() + offset, utf8.size() - offset, &cp);
        if (used == 0) break;
        offset += used;
        if (cp == 0xFFFC) continue; /* icons are drawn by the page renderer */
        if (cp == '\r') continue;
        if (cp == '\n') {
            continue;
        }
        const Glyph* glyph = impl_->rasterize(face, px, cp);
        if (!glyph) continue;
        impl_->draw_glyph(canvas, *glyph, pen, baseline, color_level, fake_bold, fake_italic);
        pen += glyph->advance;
    }

    const int total = pen - start_x;
    if (total > 0 && (flags & (DRAW_UNDERLINE | DRAW_STRIKE)) != 0) {
        const int end_x = pen;
        if (flags & DRAW_UNDERLINE) {
            int underline_y = baseline + std::max(1, impl_->face_descender(face, px) / 3);
            underline_y = std::min(underline_y, canvas->height - 1);
            canvas->draw_hline(start_x, underline_y, end_x - start_x + 1, color_level);
        }
        if (flags & DRAW_STRIKE) {
            const int strike_y =
                baseline - static_cast<int>(std::lround(static_cast<float>(px) * 0.35f));
            canvas->draw_hline(start_x, strike_y, end_x - start_x + 1, color_level);
        }
    }
    return total;
}

int FontManager::draw_text_vertical(repub_canvas* canvas, int face, int px, int x, int y,
                                    std::string_view utf8, int color_level, uint32_t flags) const {
    if (!canvas || utf8.empty()) return 0;
    if (px < 1) px = 1;
    const bool fake_bold = (flags & DRAW_FAKE_BOLD) != 0;
    const int cell = px;
    const int start_y = y;
    int pen = y;
    std::vector<uint8_t> rotated;

    size_t offset = 0;
    while (offset < utf8.size()) {
        uint32_t cp = 0;
        const size_t used = decode_utf8(utf8.data() + offset, utf8.size() - offset, &cp);
        if (used == 0) break;
        offset += used;
        if (cp == 0xFFFC) continue;
        if (cp == '\r' || cp == '\n') continue;
        const Glyph* glyph = impl_->rasterize(face, px, cp);
        if (!glyph || glyph->w <= 0 || glyph->h <= 0) {
            pen += cell;
            continue;
        }

        if (is_upright_vertical(cp)) {
            const int gx = x + (cell - glyph->w) / 2;
            const int gy = pen + (cell - glyph->h) / 2;
            for (int pass = 0; pass < (fake_bold ? 2 : 1); ++pass) {
                for (int row = 0; row < glyph->h; ++row) {
                    const uint8_t* line = glyph->coverage.data() +
                                          static_cast<size_t>(row) * static_cast<size_t>(glyph->w);
                    for (int col = 0; col < glyph->w; ++col) {
                        if (!line[col]) continue;
                        canvas->blend_pixel(gx + col + pass, gy + row, color_level, line[col]);
                    }
                }
            }
        } else {
            /* Rotate 90 degrees clockwise: width/height swap, rows become columns. */
            const int rw = glyph->h;
            const int rh = glyph->w;
            rotated.assign(static_cast<size_t>(rw) * static_cast<size_t>(rh), 0);
            for (int row = 0; row < glyph->h; ++row) {
                const uint8_t* line = glyph->coverage.data() +
                                      static_cast<size_t>(row) * static_cast<size_t>(glyph->w);
                for (int col = 0; col < glyph->w; ++col) {
                    const int dst_x = glyph->h - 1 - row;
                    const int dst_y = col;
                    rotated[static_cast<size_t>(dst_y) * static_cast<size_t>(rw) +
                            static_cast<size_t>(dst_x)] = line[col];
                }
            }
            const int gx = x + (cell - rw) / 2;
            const int gy = pen + (cell - rh) / 2;
            for (int pass = 0; pass < (fake_bold ? 2 : 1); ++pass) {
                for (int row = 0; row < rh; ++row) {
                    for (int col = 0; col < rw; ++col) {
                        const uint8_t coverage =
                            rotated[static_cast<size_t>(row) * static_cast<size_t>(rw) +
                                    static_cast<size_t>(col)];
                        if (!coverage) continue;
                        canvas->blend_pixel(gx + col + pass, gy + row, color_level, coverage);
                    }
                }
            }
        }
        pen += cell;
    }

    const int total = pen - start_y;
    if (total > 0 && (flags & (DRAW_UNDERLINE | DRAW_STRIKE)) != 0) {
        if (flags & DRAW_UNDERLINE) {
            const int line_x = std::min(x + cell - 1, canvas->width - 1);
            canvas->draw_vline(line_x, start_y, total, color_level);
        }
        if (flags & DRAW_STRIKE) {
            canvas->draw_vline(x + cell / 2, start_y, total, color_level);
        }
    }
    return total;
}

bool FontManager::glyph_bitmap(int face, int px, uint32_t codepoint, repub_image** out) const {
    if (!out) return false;
    *out = nullptr;
    const Glyph* glyph = impl_->rasterize(face, px, codepoint);
    if (!glyph || glyph->w <= 0 || glyph->h <= 0) return false;
    repub_err_t err = REPUB_OK;
    repub_image* image = image_create(impl_->platform, glyph->w, glyph->h, &err);
    if (!image) return false;
    for (int row = 0; row < glyph->h; ++row) {
        for (int col = 0; col < glyph->w; ++col) {
            const uint8_t coverage =
                glyph->coverage[static_cast<size_t>(row) * static_cast<size_t>(glyph->w) +
                                static_cast<size_t>(col)];
            uint8_t* pixel = image->rgba.data() +
                             (static_cast<size_t>(row) * static_cast<size_t>(glyph->w) +
                              static_cast<size_t>(col)) *
                                 4;
            pixel[0] = 255;
            pixel[1] = 255;
            pixel[2] = 255;
            pixel[3] = coverage;
        }
    }
    *out = image;
    return true;
}

size_t FontManager::cached_glyph_count() const { return impl_->cache.size(); }

}  // namespace repub
