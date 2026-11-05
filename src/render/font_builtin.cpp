/*
 * RollingEPUB built-in fallback bitmap font (face 0).
 *
 * The glyph data below is the classic fixed-spacing 5x7 ASCII bitmap font
 * used by Adafruit_GFX (glcdfont.c), which carries this notice:
 *
 *   Software License Agreement (BSD License)
 *   Copyright (c) 2012 Adafruit Industries.  All rights reserved.
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *   - Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   - Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *   AND ANY EXPRESS OR IMPLIED WARRANTIES ... (full BSD text in NOTICE).
 *
 * The table is stored column-major: five bytes per glyph, bit N of a byte is
 * row N (top row = bit 0).  It is drawn into an 8x8 cell (5 glyph columns +
 * 3 blank columns, 7 glyph rows + 1 blank row) so that the built-in face is
 * a deterministic, always-available monospace fallback.
 *
 * Latin-1 letters and typographic punctuation are mapped to their unaccented
 * ASCII base letters; any other codepoint renders as a hollow box.
 *
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 */

#include <cmath>
#include <cstdint>
#include <vector>

#include "rolling_epub/repub_config.h"

namespace repub {
namespace builtin_font {

namespace {

constexpr int kCellW = 8;
constexpr int kCellH = 8;
constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;

/* ASCII 32..127, 5 bytes per glyph, column-major. */
const uint8_t kAscii[96 * 5] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00,
    0x14, 0x7F, 0x14, 0x7F, 0x14, 0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x23, 0x13, 0x08, 0x64, 0x62,
    0x36, 0x49, 0x56, 0x20, 0x50, 0x00, 0x08, 0x07, 0x03, 0x00,
    0x00, 0x1C, 0x22, 0x41, 0x00, 0x00, 0x41, 0x22, 0x1C, 0x00, 0x2A, 0x1C, 0x7F, 0x1C, 0x2A,
    0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x80, 0x70, 0x30, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x00, 0x00, 0x60, 0x60, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02,
    0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x72, 0x49, 0x49, 0x49, 0x46,
    0x21, 0x41, 0x49, 0x4D, 0x33, 0x18, 0x14, 0x12, 0x7F, 0x10, 0x27, 0x45, 0x45, 0x45, 0x39,
    0x3C, 0x4A, 0x49, 0x49, 0x31, 0x41, 0x21, 0x11, 0x09, 0x07,
    0x36, 0x49, 0x49, 0x49, 0x36, 0x46, 0x49, 0x49, 0x29, 0x1E, 0x00, 0x00, 0x14, 0x00, 0x00,
    0x00, 0x40, 0x34, 0x00, 0x00, 0x00, 0x08, 0x14, 0x22, 0x41, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x00, 0x41, 0x22, 0x14, 0x08, 0x02, 0x01, 0x59, 0x09, 0x06,
    0x3E, 0x41, 0x5D, 0x59, 0x4E, 0x7C, 0x12, 0x11, 0x12, 0x7C, 0x7F, 0x49, 0x49, 0x49, 0x36,
    0x3E, 0x41, 0x41, 0x41, 0x22, 0x7F, 0x41, 0x41, 0x41, 0x3E, 0x7F, 0x49, 0x49, 0x49, 0x41,
    0x7F, 0x09, 0x09, 0x09, 0x01, 0x3E, 0x41, 0x41, 0x51, 0x73,
    0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x41, 0x7F, 0x41, 0x00, 0x20, 0x40, 0x41, 0x3F, 0x01,
    0x7F, 0x08, 0x14, 0x22, 0x41, 0x7F, 0x40, 0x40, 0x40, 0x40, 0x7F, 0x02, 0x1C, 0x02, 0x7F,
    0x7F, 0x04, 0x08, 0x10, 0x7F, 0x3E, 0x41, 0x41, 0x41, 0x3E,
    0x7F, 0x09, 0x09, 0x09, 0x06, 0x3E, 0x41, 0x51, 0x21, 0x5E, 0x7F, 0x09, 0x19, 0x29, 0x46,
    0x26, 0x49, 0x49, 0x49, 0x32, 0x03, 0x01, 0x7F, 0x01, 0x03, 0x3F, 0x40, 0x40, 0x40, 0x3F,
    0x1F, 0x20, 0x40, 0x20, 0x1F, 0x3F, 0x40, 0x38, 0x40, 0x3F,
    0x63, 0x14, 0x08, 0x14, 0x63, 0x03, 0x04, 0x78, 0x04, 0x03, 0x61, 0x59, 0x49, 0x4D, 0x43,
    0x00, 0x7F, 0x41, 0x41, 0x41, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x41, 0x41, 0x41, 0x7F,
    0x04, 0x02, 0x01, 0x02, 0x04, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x00, 0x03, 0x07, 0x08, 0x00, 0x20, 0x54, 0x54, 0x78, 0x40, 0x7F, 0x28, 0x44, 0x44, 0x38,
    0x38, 0x44, 0x44, 0x44, 0x28, 0x38, 0x44, 0x44, 0x28, 0x7F, 0x38, 0x54, 0x54, 0x54, 0x18,
    0x00, 0x08, 0x7E, 0x09, 0x02, 0x18, 0xA4, 0xA4, 0x9C, 0x78,
    0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, 0x44, 0x7D, 0x40, 0x00, 0x20, 0x40, 0x40, 0x3D, 0x00,
    0x7F, 0x10, 0x28, 0x44, 0x00, 0x00, 0x41, 0x7F, 0x40, 0x00, 0x7C, 0x04, 0x78, 0x04, 0x78,
    0x7C, 0x08, 0x04, 0x04, 0x78, 0x38, 0x44, 0x44, 0x44, 0x38,
    0xFC, 0x18, 0x24, 0x24, 0x18, 0x18, 0x24, 0x24, 0x18, 0xFC, 0x7C, 0x08, 0x04, 0x04, 0x08,
    0x48, 0x54, 0x54, 0x54, 0x24, 0x04, 0x04, 0x3F, 0x44, 0x24, 0x3C, 0x40, 0x40, 0x20, 0x7C,
    0x1C, 0x20, 0x40, 0x20, 0x1C, 0x3C, 0x40, 0x30, 0x40, 0x3C,
    0x44, 0x28, 0x10, 0x28, 0x44, 0x4C, 0x90, 0x90, 0x90, 0x7C, 0x44, 0x64, 0x54, 0x4C, 0x44,
    0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x00, 0x77, 0x00, 0x00, 0x00, 0x41, 0x36, 0x08, 0x00,
    0x02, 0x01, 0x02, 0x04, 0x02, 0x3C, 0x26, 0x23, 0x26, 0x3C,
};

/* ASCII index (0-based from 32) for a renderable codepoint, -1 otherwise. */
int ascii_index(uint32_t cp) {
    if (cp >= 32 && cp <= 126) return static_cast<int>(cp) - 32;
    return -1;
}

/* Map Latin-1 letters / typographic punctuation to a base ASCII codepoint. */
int mapped_ascii(uint32_t cp) {
    if (cp == 0x00A0) return ' ';
    if (cp == 0x00AD) return '-';
    if (cp == 0x00B7) return '.';
    if (cp == 0x00AB) return '<';
    if (cp == 0x00BB) return '>';
    if (cp == 0x00D7) return 'x';
    if (cp == 0x00F7) return '/';
    if (cp >= 0x2010 && cp <= 0x2015) return '-';
    if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A || cp == 0x2032) return '\'';
    if (cp == 0x201C || cp == 0x201D || cp == 0x201E || cp == 0x2033) return '"';
    if (cp == 0x2022) return '*';
    if (cp == 0x2026) return '.';
    if (cp == 0x2039) return '<';
    if (cp == 0x203A) return '>';
    if (cp == 0x2044) return '/';
    if (cp == 0x20AC) return 'E';
    if (cp == 0xFFFD) return '?';
    if (cp >= 0x00C0 && cp <= 0x00C5) return 'A';
    if (cp == 0x00C6) return 'A';
    if (cp == 0x00C7) return 'C';
    if (cp >= 0x00C8 && cp <= 0x00CB) return 'E';
    if (cp >= 0x00CC && cp <= 0x00CF) return 'I';
    if (cp == 0x00D0) return 'D';
    if (cp == 0x00D1) return 'N';
    if (cp >= 0x00D2 && cp <= 0x00D6) return 'O';
    if (cp == 0x00D8) return 'O';
    if (cp >= 0x00D9 && cp <= 0x00DC) return 'U';
    if (cp == 0x00DD) return 'Y';
    if (cp == 0x00DE) return 'P';
    if (cp == 0x00DF) return 's';
    if (cp >= 0x00E0 && cp <= 0x00E5) return 'a';
    if (cp == 0x00E6) return 'a';
    if (cp == 0x00E7) return 'c';
    if (cp >= 0x00E8 && cp <= 0x00EB) return 'e';
    if (cp >= 0x00EC && cp <= 0x00EF) return 'i';
    if (cp == 0x00F0) return 'd';
    if (cp == 0x00F1) return 'n';
    if (cp >= 0x00F2 && cp <= 0x00F6) return 'o';
    if (cp == 0x00F8) return 'o';
    if (cp >= 0x00F9 && cp <= 0x00FC) return 'u';
    if (cp == 0x00FD) return 'y';
    if (cp == 0x00FE) return 'p';
    if (cp == 0x00FF) return 'y';
    return -1;
}

int resolved_ascii(uint32_t cp) {
    const int direct = ascii_index(cp);
    if (direct >= 0) return direct;
    const int mapped = mapped_ascii(cp);
    if (mapped >= 0) return ascii_index(static_cast<uint32_t>(mapped));
    return -1;
}

}  // namespace

bool has_glyph(uint32_t cp) {
    if (cp == 0xFFFC) return true; /* placeholder: intentionally blank */
    return resolved_ascii(cp) >= 0;
}

int glyph_advance(int px, uint32_t cp) {
    (void)cp; /* monospace fallback */
    if (px < 1) px = 1;
    const int w = static_cast<int>(std::lround(static_cast<float>(kCellW) * px / kCellH));
    return w < 1 ? 1 : w;
}

bool rasterize(int px, uint32_t cp, int* out_w, int* out_h, int* out_advance,
               int* out_bearing_x, int* out_bearing_y, std::vector<uint8_t>* coverage) {
    if (!out_w || !out_h || !out_advance || !out_bearing_x || !out_bearing_y || !coverage) {
        return false;
    }
    if (px < 1) px = 1;
    if (px > 512) px = 512;
    const float scale = static_cast<float>(px) / static_cast<float>(kCellH);
    int dst_w = static_cast<int>(std::lround(static_cast<float>(kCellW) * scale));
    int dst_h = static_cast<int>(std::lround(static_cast<float>(kCellH) * scale));
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;

    *out_w = dst_w;
    *out_h = dst_h;
    *out_advance = dst_w;
    *out_bearing_x = 0;
    *out_bearing_y = dst_h; /* bitmap top sits one cell above the baseline */
    coverage->assign(static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h), 0);

    if (cp == 0xFFFC) return true; /* object replacement: blank */

    const int index = resolved_ascii(cp);
    if (index < 0) {
        /* Missing glyph: hollow box, thickness scales with the pixel size. */
        int thickness = static_cast<int>(std::lround(scale * 0.5f));
        if (thickness < 1) thickness = 1;
        for (int y = 0; y < dst_h; ++y) {
            for (int x = 0; x < dst_w; ++x) {
                const bool border = x < thickness || y < thickness ||
                                    x >= dst_w - thickness || y >= dst_h - thickness;
                if (border) {
                    (*coverage)[static_cast<size_t>(y) * dst_w + x] = 255;
                }
            }
        }
        return true;
    }

    const uint8_t* glyph = kAscii + index * kGlyphW;
    /* Box-filter resample the 8x8 cell to the requested pixel size. */
    constexpr int kSubs = 4;
    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            int hits = 0;
            for (int sy = 0; sy < kSubs; ++sy) {
                const float src_y =
                    (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / kSubs) *
                    static_cast<float>(kCellH) / static_cast<float>(dst_h);
                if (src_y >= kGlyphH) continue;
                const int row = static_cast<int>(src_y);
                for (int sx = 0; sx < kSubs; ++sx) {
                    const float src_x =
                        (static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / kSubs) *
                        static_cast<float>(kCellW) / static_cast<float>(dst_w);
                    if (src_x >= kGlyphW) continue;
                    const int col = static_cast<int>(src_x);
                    if ((glyph[col] >> row) & 1u) ++hits;
                }
            }
            if (hits) {
                (*coverage)[static_cast<size_t>(y) * dst_w + x] =
                    static_cast<uint8_t>((hits * 255) / (kSubs * kSubs));
            }
        }
    }
    return true;
}

int ascender(int px) {
    if (px < 1) px = 1;
    return px; /* full 8x8 cell height above the baseline */
}

int descender(int px) {
    (void)px;
    return 0;
}

int line_height(int px) {
    if (px < 1) px = 1;
    int leading = px / 5; /* ~20% */
    if (leading < 1) leading = 1;
    return ascender(px) + descender(px) + leading;
}

}  // namespace builtin_font
}  // namespace repub
