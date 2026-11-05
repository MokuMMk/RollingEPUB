/*
 * RollingEPUB hyphenation (Liang patterns), merged from CrossMux (MIT).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_LAYOUT_HYPHEN_H
#define REPUB_SRC_LAYOUT_HYPHEN_H

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace repub {

struct HyphenBreak {
    uint16_t byte_offset = 0;   /* inside the UTF-8 word */
    bool insert_hyphen = false; /* pattern break needs a visible '-' */
};

/* Returns the number of breaks written (<= cap).  Only Latin/Cyrillic
 * languages with compiled patterns are supported; CJK line breaking does not
 * use this. */
int hyphenate_word(std::string_view word, std::string_view language,
                   HyphenBreak* breaks, int cap);

/* True when patterns for `language` (primary tag, e.g. "en") are compiled in. */
bool hyphenation_available(std::string_view language);

/* Default language implied by an EPUB metadata language tag ("zh-CN", "en-US"). */
std::string_view primary_language(std::string_view tag);

}  // namespace repub

#endif  // REPUB_SRC_LAYOUT_HYPHEN_H
