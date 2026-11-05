/*
 * RollingEPUB hyphenation: Liang pattern evaluator.
 *
 * SPDX-License-Identifier: MIT
 *
 * Provenance
 * ----------
 * Ported from CrossSIGO CrossMux (MIT):
 *   lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
 *   lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
 *   lib/Epub/Epub/hyphenation/Hyphenator.cpp
 *   lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
 *   lib/Epub/Epub/hyphenation/SerializedHyphenationTrie.h
 * The pattern trie data lives in src/layout/hyphen_en_patterns.h (copied
 * verbatim from generated/hyph-en.trie.h, MIT).  The evaluator is unchanged
 * in behaviour; the entry points are adapted to the rolling_epub::hyphen.h
 * contract (UTF-8 byte offsets, language tags instead of a global selector).
 *
 * Only languages whose trie data is compiled in are available.  English is
 * enabled by default (REPUB_HYPH_EN, CMake REPUB_ENABLE_HYPHEN_EN); German /
 * French / Spanish / Italian / Russian data headers are picked up when
 * REPUB_HYPH_<LANG> is enabled and src/layout/hyphen_<lang>_patterns.h
 * exists.  Unknown/disabled languages report 0 breaks.
 */

#include "layout/hyphen.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rolling_epub/repub_config.h"

/* ------------------------------------------------------------------------- */
/* Serialized pattern trie descriptor + data (only when compiled in).         */
/* ------------------------------------------------------------------------- */

#if REPUB_ENABLE_HYPHENATION

#ifndef REPUB_HYPH_EN
#define REPUB_HYPH_EN 1
#endif

struct SerializedHyphenationPatterns {
    size_t rootOffset;
    const std::uint8_t* data;
    size_t size;
};

#if REPUB_HYPH_EN
#include "layout/hyphen_en_patterns.h"
#endif

#if defined(REPUB_HYPH_DE) && REPUB_HYPH_DE && defined(__has_include)
#if __has_include("layout/hyphen_de_patterns.h")
#include "layout/hyphen_de_patterns.h"
#define REPUB_HAVE_HYPH_DE 1
#endif
#endif
#if defined(REPUB_HYPH_FR) && REPUB_HYPH_FR && defined(__has_include)
#if __has_include("layout/hyphen_fr_patterns.h")
#include "layout/hyphen_fr_patterns.h"
#define REPUB_HAVE_HYPH_FR 1
#endif
#endif
#if defined(REPUB_HYPH_ES) && REPUB_HYPH_ES && defined(__has_include)
#if __has_include("layout/hyphen_es_patterns.h")
#include "layout/hyphen_es_patterns.h"
#define REPUB_HAVE_HYPH_ES 1
#endif
#endif
#if defined(REPUB_HYPH_IT) && REPUB_HYPH_IT && defined(__has_include)
#if __has_include("layout/hyphen_it_patterns.h")
#include "layout/hyphen_it_patterns.h"
#define REPUB_HAVE_HYPH_IT 1
#endif
#endif
#if defined(REPUB_HYPH_RU) && REPUB_HYPH_RU && defined(__has_include)
#if __has_include("layout/hyphen_ru_patterns.h")
#include "layout/hyphen_ru_patterns.h"
#define REPUB_HAVE_HYPH_RU 1
#endif
#endif

namespace repub {
namespace {

constexpr size_t kMaxWordBytes = 160;
constexpr size_t kMaxWordChars = 70;

/* ------------------------------------------------------------------ */
/* Character classification (ported from HyphenationCommon.cpp).       */
/* ------------------------------------------------------------------ */

bool is_latin_letter(uint32_t cp) {
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return true;
    if (((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00F6) ||
         (cp >= 0x00F8 && cp <= 0x00FF)) &&
        cp != 0x00D7 && cp != 0x00F7)
        return true;
    if (cp >= 0x0100 && cp <= 0x017F) return true;
    switch (cp) {
        case 0x0152:
        case 0x0153:
        case 0x0178:
        case 0x1E9E:
            return true;
        default:
            return false;
    }
}

bool is_cyrillic_letter(uint32_t cp) { return cp >= 0x0400 && cp <= 0x052F; }

bool is_alphabetic(uint32_t cp) { return is_latin_letter(cp) || is_cyrillic_letter(cp); }

uint32_t to_lower_latin(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp - 'A' + 'a';
    if ((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00DE)) return cp + 0x20;
    if ((cp >= 0x0100 && cp <= 0x0137 && (cp % 2 == 0)) ||
        (cp >= 0x0139 && cp <= 0x0148 && (cp % 2 == 1)) ||
        (cp >= 0x014A && cp <= 0x0177 && (cp % 2 == 0)) ||
        (cp >= 0x0179 && cp <= 0x017E && (cp % 2 == 1)))
        return cp + 1;
    switch (cp) {
        case 0x0178:
            return 0x00FF;
        case 0x1E9E:
            return 0x00DF;
        default:
            return cp;
    }
}

uint32_t to_lower_cyrillic(uint32_t cp) {
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
    if (cp == 0x0401) return 0x0451;
    return cp;
}

bool is_punctuation(uint32_t cp) {
    switch (cp) {
        case '-':
        case '.':
        case ',':
        case '!':
        case '?':
        case ';':
        case ':':
        case '"':
        case '\'':
        case ')':
        case '(':
        case 0x00AB:
        case 0x00BB:
        case 0x2018:
        case 0x2019:
        case 0x201A:
        case 0x201C:
        case 0x201D:
        case 0x201E:
        case 0x00A0:
        case '{':
        case '}':
        case '[':
        case ']':
        case '/':
        case 0x2039:
        case 0x203A:
        case 0x2026:
            return true;
        default:
            return false;
    }
}

bool is_ascii_digit(uint32_t cp) { return cp >= '0' && cp <= '9'; }

bool is_apostrophe(uint32_t cp) {
    return cp == '\'' || cp == 0x2018 || cp == 0x2019;
}

bool is_explicit_hyphen(uint32_t cp) {
    switch (cp) {
        case '-':
        case 0x00AD:
        case 0x058A:
        case 0x2010:
        case 0x2011:
        case 0x2012:
        case 0x2013:
        case 0x2014:
        case 0x2015:
        case 0x2043:
        case 0x207B:
        case 0x208B:
        case 0x2212:
        case 0x2E17:
        case 0x2E3A:
        case 0x2E3B:
        case 0xFE58:
        case 0xFE63:
        case 0xFF0D:
        case 0x005F:
        case 0x2026:
            return true;
        default:
            return false;
    }
}

bool is_soft_hyphen(uint32_t cp) { return cp == 0x00AD; }

/* ------------------------------------------------------------------ */
/* UTF-8 codepoint collection with byte offsets.                      */
/* ------------------------------------------------------------------ */

struct CodepointInfo {
    uint32_t value = 0;
    uint32_t byte_offset = 0;
};

size_t decode_one(const char* data, size_t size, uint32_t* cp) {
    if (!size) return 0;
    const unsigned char* u = reinterpret_cast<const unsigned char*>(data);
    if (u[0] < 0x80) {
        *cp = u[0];
        return 1;
    }
    size_t n = 0;
    if (u[0] >= 0xC2 && u[0] <= 0xDF) {
        n = 2;
    } else if (u[0] >= 0xE0 && u[0] <= 0xEF) {
        n = 3;
    } else if (u[0] >= 0xF0 && u[0] <= 0xF4) {
        n = 4;
    } else {
        *cp = 0xFFFDu;
        return 1;
    }
    if (n > size) {
        *cp = 0xFFFDu;
        return 1;
    }
    for (size_t i = 1; i < n; ++i) {
        if ((u[i] & 0xC0) != 0x80) {
            *cp = 0xFFFDu;
            return 1;
        }
    }
    uint32_t value = static_cast<uint32_t>(u[0] & (0x7F >> n));
    for (size_t i = 1; i < n; ++i) value = (value << 6) | static_cast<uint32_t>(u[i] & 0x3F);
    *cp = value;
    return n;
}

std::vector<CodepointInfo> collect_codepoints(std::string_view word) {
    std::vector<CodepointInfo> cps;
    cps.reserve(word.size());
    size_t at = 0;
    while (at < word.size()) {
        uint32_t cp = 0;
        size_t n = decode_one(word.data() + at, word.size() - at, &cp);
        if (!n) break;
        cps.push_back(CodepointInfo{cp, static_cast<uint32_t>(at)});
        at += n;
    }
    return cps;
}

/* Drop surrounding punctuation/footnote brackets (ported from the source). */
void trim_surrounding(std::vector<CodepointInfo>& cps) {
    if (cps.empty()) return;
    if (cps.size() >= 3) {
        int end = static_cast<int>(cps.size()) - 1;
        while (end >= 0 && is_punctuation(cps[static_cast<size_t>(end)].value)) --end;
        int pos = end;
        if (pos >= 0 && is_ascii_digit(cps[static_cast<size_t>(pos)].value)) {
            while (pos >= 0 && is_ascii_digit(cps[static_cast<size_t>(pos)].value)) --pos;
            if (pos >= 0 && cps[static_cast<size_t>(pos)].value == '[' && end - pos > 1) {
                cps.erase(cps.begin() + pos, cps.end());
            }
        }
    }
    while (!cps.empty() && is_punctuation(cps.front().value)) cps.erase(cps.begin());
    while (!cps.empty() && is_punctuation(cps.back().value)) cps.pop_back();
}

/* ------------------------------------------------------------------ */
/* Liang evaluator (ported from LiangHyphenation.cpp).                 */
/* ------------------------------------------------------------------ */

bool encode_utf8(uint32_t cp, std::vector<uint8_t>& out) {
    if ((cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) return false;
    uint8_t encoded[4];
    size_t len = 0;
    if (cp <= 0x7Fu) {
        encoded[len++] = static_cast<uint8_t>(cp);
    } else if (cp <= 0x7FFu) {
        encoded[len++] = static_cast<uint8_t>(0xC0u | ((cp >> 6) & 0x1Fu));
        encoded[len++] = static_cast<uint8_t>(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0xFFFFu) {
        encoded[len++] = static_cast<uint8_t>(0xE0u | ((cp >> 12) & 0x0Fu));
        encoded[len++] = static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3Fu));
        encoded[len++] = static_cast<uint8_t>(0x80u | (cp & 0x3Fu));
    } else {
        encoded[len++] = static_cast<uint8_t>(0xF0u | ((cp >> 18) & 0x07u));
        encoded[len++] = static_cast<uint8_t>(0x80u | ((cp >> 12) & 0x3Fu));
        encoded[len++] = static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3Fu));
        encoded[len++] = static_cast<uint8_t>(0x80u | (cp & 0x3Fu));
    }
    if (out.size() + len > kMaxWordBytes) return false;
    out.insert(out.end(), encoded, encoded + len);
    return true;
}

struct AugmentedWord {
    std::vector<uint8_t> bytes;
    std::vector<size_t> char_byte_offsets;
    std::vector<int32_t> byte_to_char;
    size_t char_count = 0;
};

struct AutomatonState {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t addr = 0;
    uint8_t stride = 1;
    size_t child_count = 0;
    const uint8_t* transitions = nullptr;
    const uint8_t* targets = nullptr;
    const uint8_t* levels = nullptr;
    size_t levels_len = 0;
    bool valid() const { return data != nullptr; }
};

AutomatonState decode_state(const SerializedHyphenationPatterns& automaton, size_t addr) {
    AutomatonState state;
    if (addr >= automaton.size) return state;
    const uint8_t* base = automaton.data + addr;
    size_t remaining = automaton.size - addr;
    size_t pos = 0;
    const uint8_t header = base[pos++];
    const bool has_levels = (header >> 7) != 0;
    uint8_t stride = static_cast<uint8_t>((header >> 5) & 0x03u);
    if (stride == 0) stride = 1;
    size_t child_count = static_cast<size_t>(header & 0x1Fu);
    if (child_count == 31u) {
        if (pos >= remaining) return AutomatonState{};
        child_count = base[pos++];
    }
    const uint8_t* levels_ptr = nullptr;
    size_t levels_len = 0;
    if (has_levels) {
        if (pos + 1 >= remaining) return AutomatonState{};
        const uint8_t offset_hi = base[pos++];
        const uint8_t offset_lo_len = base[pos++];
        const size_t offset =
            (static_cast<size_t>(offset_hi) << 4) | (offset_lo_len >> 4);
        levels_len = offset_lo_len & 0x0Fu;
        if (offset + levels_len > automaton.size) return AutomatonState{};
        levels_ptr = automaton.data + offset - 4u;
    }
    if (pos + child_count > remaining) return AutomatonState{};
    const uint8_t* transitions = base + pos;
    pos += child_count;
    const size_t targets_bytes = child_count * stride;
    if (pos + targets_bytes > remaining) return AutomatonState{};
    const uint8_t* targets = base + pos;
    state.data = automaton.data;
    state.size = automaton.size;
    state.addr = addr;
    state.stride = stride;
    state.child_count = child_count;
    state.transitions = transitions;
    state.targets = targets;
    state.levels = levels_ptr;
    state.levels_len = levels_len;
    return state;
}

int32_t decode_delta(const uint8_t* buf, uint8_t stride) {
    if (stride == 1) return static_cast<int8_t>(buf[0]);
    if (stride == 2)
        return static_cast<int16_t>((static_cast<uint16_t>(buf[0]) << 8) |
                                    static_cast<uint16_t>(buf[1]));
    const int32_t unsigned_val = (static_cast<int32_t>(buf[0]) << 16) |
                                 (static_cast<int32_t>(buf[1]) << 8) |
                                 static_cast<int32_t>(buf[2]);
    return unsigned_val - (1 << 23);
}

bool transition(const SerializedHyphenationPatterns& automaton, const AutomatonState& state,
                uint8_t letter, AutomatonState& out) {
    if (!state.valid()) return false;
    for (size_t idx = 0; idx < state.child_count; ++idx) {
        if (state.transitions[idx] != letter) continue;
        const uint8_t* delta_ptr = state.targets + idx * state.stride;
        const int32_t delta = decode_delta(delta_ptr, state.stride);
        const int64_t next_addr = static_cast<int64_t>(state.addr) + delta;
        if (next_addr < 0 || static_cast<size_t>(next_addr) >= automaton.size) return false;
        out = decode_state(automaton, static_cast<size_t>(next_addr));
        return out.valid();
    }
    return false;
}

struct WordConfig {
    bool (*is_letter)(uint32_t) = nullptr;
    uint32_t (*to_lower)(uint32_t) = nullptr;
    size_t min_prefix = 2;
    size_t min_suffix = 2;
};

bool build_augmented_word(AugmentedWord& word, const std::vector<CodepointInfo>& cps,
                          const WordConfig& config) {
    word.bytes.clear();
    word.char_byte_offsets.clear();
    word.byte_to_char.clear();
    word.char_count = 0;
    if (cps.empty()) return false;
    word.char_byte_offsets.push_back(0);
    word.bytes.push_back('.');
    word.char_count = 1;
    for (const CodepointInfo& info : cps) {
        if (!config.is_letter(info.value)) return false;
        if (word.char_count >= kMaxWordChars - 1) return false;
        word.char_byte_offsets.push_back(word.bytes.size());
        if (!encode_utf8(config.to_lower(info.value), word.bytes)) return false;
        ++word.char_count;
    }
    if (word.char_count >= kMaxWordChars || word.bytes.size() >= kMaxWordBytes) return false;
    word.char_byte_offsets.push_back(word.bytes.size());
    word.bytes.push_back('.');
    ++word.char_count;
    word.byte_to_char.assign(word.bytes.size(), -1);
    for (size_t i = 0; i < word.char_count; ++i) {
        size_t offset = word.char_byte_offsets[i];
        if (offset < word.bytes.size()) word.byte_to_char[offset] = static_cast<int32_t>(i);
    }
    return true;
}

std::vector<size_t> liang_break_indexes(const std::vector<CodepointInfo>& cps,
                                        const SerializedHyphenationPatterns& patterns,
                                        const WordConfig& config) {
    std::vector<size_t> indexes;
    AugmentedWord word;
    if (!build_augmented_word(word, cps, config)) return indexes;

    const AutomatonState root = decode_state(patterns, patterns.rootOffset);
    if (!root.valid()) return indexes;

    std::vector<uint8_t> scores(word.char_count, 0);
    for (size_t char_start = 0; char_start < word.char_count; ++char_start) {
        const size_t byte_start = word.char_byte_offsets[char_start];
        AutomatonState state = root;
        for (size_t cursor = byte_start; cursor < word.bytes.size(); ++cursor) {
            AutomatonState next;
            if (!transition(patterns, state, word.bytes[cursor], next)) break;
            state = next;
            if (!state.levels || !state.levels_len) continue;
            size_t offset = 0;
            for (size_t i = 0; i < state.levels_len; ++i) {
                const uint8_t packed = state.levels[i];
                const size_t dist = static_cast<size_t>(packed / 10);
                const uint8_t level = static_cast<uint8_t>(packed % 10);
                offset += dist;
                const size_t split_byte = byte_start + offset;
                if (split_byte >= word.bytes.size()) continue;
                const int32_t boundary = word.byte_to_char[split_byte];
                if (boundary < 0) continue;
                if (boundary < 2 || boundary + 2 > static_cast<int32_t>(word.char_count)) continue;
                const size_t idx = static_cast<size_t>(boundary);
                if (idx >= scores.size()) continue;
                if (level > scores[idx]) scores[idx] = level;
            }
        }
    }
    const size_t cp_count = cps.size();
    if (cp_count < 2) return indexes;
    for (size_t break_index = 1; break_index < cp_count; ++break_index) {
        if (break_index < config.min_prefix) continue;
        if (cp_count - break_index < config.min_suffix) continue;
        const size_t score_idx = break_index + 1;
        if (score_idx >= scores.size()) break;
        if ((scores[score_idx] & 1u) == 0) continue;
        indexes.push_back(break_index);
    }
    return indexes;
}

/* ------------------------------------------------------------------ */
/* Language registry.                                                  */
/* ------------------------------------------------------------------ */

struct LanguageEntry {
    const char* code;
    const SerializedHyphenationPatterns* patterns;
    bool cyrillic;
    size_t min_prefix;
    size_t min_suffix;
};

const LanguageEntry* language_entries(size_t* count) {
    static const LanguageEntry entries[] = {
#if REPUB_HYPH_EN
        {"en", &en_patterns, false, 3, 3},
#endif
#ifdef REPUB_HAVE_HYPH_DE
        {"de", &de_patterns, false, 2, 2},
#endif
#ifdef REPUB_HAVE_HYPH_FR
        {"fr", &fr_patterns, false, 2, 2},
#endif
#ifdef REPUB_HAVE_HYPH_ES
        {"es", &es_patterns, false, 2, 2},
#endif
#ifdef REPUB_HAVE_HYPH_IT
        {"it", &it_patterns, false, 2, 2},
#endif
#ifdef REPUB_HAVE_HYPH_RU
        {"ru", &ru_patterns, true, 2, 2},
#endif
    };
    *count = sizeof(entries) / sizeof(entries[0]);
    return entries;
}

std::string primary_lower(std::string_view tag) {
    std::string_view primary = primary_language(tag);
    std::string out;
    out.reserve(primary.size());
    for (char c : primary) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        out.push_back(c);
    }
    return out;
}

const LanguageEntry* find_language(std::string_view tag, WordConfig* config) {
    std::string primary = primary_lower(tag);
    if (primary.empty()) return nullptr;
    size_t count = 0;
    const LanguageEntry* entries = language_entries(&count);
    for (size_t i = 0; i < count; ++i) {
        if (primary == entries[i].code) {
            if (config) {
                config->is_letter = entries[i].cyrillic ? is_cyrillic_letter : is_latin_letter;
                config->to_lower = entries[i].cyrillic ? to_lower_cyrillic : to_lower_latin;
                config->min_prefix = entries[i].min_prefix;
                config->min_suffix = entries[i].min_suffix;
            }
            return &entries[i];
        }
    }
    return nullptr;
}

/* ------------------------------------------------------------------ */
/* Word break assembly.                                                */
/* ------------------------------------------------------------------ */

bool is_segment_separator(uint32_t cp) { return is_explicit_hyphen(cp) || is_apostrophe(cp); }

void append_segment_pattern_breaks(const std::vector<CodepointInfo>& cps,
                                   const SerializedHyphenationPatterns& patterns,
                                   const WordConfig& config, std::vector<HyphenBreak>& out) {
    size_t seg_start = 0;
    for (size_t i = 0; i <= cps.size(); ++i) {
        const bool at_end = i == cps.size();
        const bool at_sep = !at_end && is_segment_separator(cps[i].value);
        if (!at_end && !at_sep) continue;
        if (i > seg_start) {
            std::vector<CodepointInfo> segment(cps.begin() + static_cast<std::ptrdiff_t>(seg_start),
                                               cps.begin() + static_cast<std::ptrdiff_t>(i));
            std::vector<size_t> indexes = liang_break_indexes(segment, patterns, config);
            for (size_t idx : indexes) {
                if (idx == 0 || idx >= segment.size()) continue;
                const size_t cp_idx = seg_start + idx;
                if (cp_idx >= cps.size()) continue;
                HyphenBreak hb;
                hb.byte_offset = static_cast<uint16_t>(cps[cp_idx].byte_offset);
                hb.insert_hyphen = true;
                out.push_back(hb);
            }
        }
        seg_start = i + 1;
    }
}

void append_apostrophe_breaks(const std::vector<CodepointInfo>& cps,
                              std::vector<HyphenBreak>& out) {
    constexpr size_t kMinLeft = 3;
    constexpr size_t kMinRight = 3;
    size_t segment_start = 0;
    for (size_t i = 0; i < cps.size(); ++i) {
        if (!is_segment_separator(cps[i].value)) continue;
        if (is_apostrophe(cps[i].value) && i > 0 && i + 1 < cps.size() &&
            is_alphabetic(cps[i - 1].value) && is_alphabetic(cps[i + 1].value)) {
            size_t left = 0;
            for (size_t j = segment_start; j < i; ++j) {
                if (is_alphabetic(cps[j].value)) ++left;
            }
            size_t right = 0;
            for (size_t j = i + 1; j < cps.size() && !is_segment_separator(cps[j].value); ++j) {
                if (is_alphabetic(cps[j].value)) ++right;
            }
            if (left >= kMinLeft && right >= kMinRight) {
                HyphenBreak hb;
                hb.byte_offset = static_cast<uint16_t>(cps[i + 1].byte_offset);
                hb.insert_hyphen = false;
                out.push_back(hb);
            }
        }
        segment_start = i + 1;
    }
}

void append_explicit_breaks(const std::vector<CodepointInfo>& cps,
                            std::vector<HyphenBreak>& out) {
    for (size_t i = 1; i + 1 < cps.size(); ++i) {
        const uint32_t cp = cps[i].value;
        if (!is_explicit_hyphen(cp)) continue;
        if (!is_alphabetic(cps[i - 1].value) || !is_alphabetic(cps[i + 1].value)) continue;
        HyphenBreak hb;
        hb.byte_offset = static_cast<uint16_t>(cps[i + 1].byte_offset);
        /* A hard hyphen is already visible in the text; a soft hyphen is not. */
        hb.insert_hyphen = is_soft_hyphen(cp);
        out.push_back(hb);
    }
}

void sort_dedupe(std::vector<HyphenBreak>& breaks) {
    std::stable_sort(breaks.begin(), breaks.end(), [](const HyphenBreak& a, const HyphenBreak& b) {
        return a.byte_offset < b.byte_offset;
    });
    breaks.erase(std::unique(breaks.begin(), breaks.end(),
                             [](const HyphenBreak& a, const HyphenBreak& b) {
                                 return a.byte_offset == b.byte_offset;
                             }),
                 breaks.end());
}

}  // namespace

int hyphenate_word(std::string_view word, std::string_view language, HyphenBreak* breaks, int cap) {
    if (!breaks || cap <= 0 || word.empty()) return 0;
    WordConfig config;
    const LanguageEntry* entry = find_language(language, &config);
    if (!entry) return 0;

    std::vector<CodepointInfo> cps = collect_codepoints(word);
    if (cps.empty()) return 0;
    trim_surrounding(cps);
    if (cps.size() < entry->min_prefix + entry->min_suffix) return 0;
    if (cps.size() > kMaxWordChars) return 0;

    std::vector<HyphenBreak> out;
    out.reserve(16);

    bool has_explicit = false;
    bool has_apostrophe = false;
    for (const CodepointInfo& info : cps) {
        if (is_explicit_hyphen(info.value)) has_explicit = true;
        if (is_apostrophe(info.value)) has_apostrophe = true;
    }

    if (has_explicit) {
        append_explicit_breaks(cps, out);
        append_segment_pattern_breaks(cps, *entry->patterns, config, out);
        if (has_apostrophe) append_apostrophe_breaks(cps, out);
    } else if (has_apostrophe) {
        append_segment_pattern_breaks(cps, *entry->patterns, config, out);
        append_apostrophe_breaks(cps, out);
    } else {
        std::vector<size_t> indexes = liang_break_indexes(cps, *entry->patterns, config);
        for (size_t idx : indexes) {
            if (idx >= cps.size()) continue;
            HyphenBreak hb;
            hb.byte_offset = static_cast<uint16_t>(cps[idx].byte_offset);
            hb.insert_hyphen = true;
            out.push_back(hb);
        }
    }

    sort_dedupe(out);
    int written = 0;
    for (const HyphenBreak& hb : out) {
        if (written >= cap) break;
        breaks[written++] = hb;
    }
    return written;
}

bool hyphenation_available(std::string_view language) {
    return find_language(language, nullptr) != nullptr;
}

}  // namespace repub

#else /* !REPUB_ENABLE_HYPHENATION */

namespace repub {

int hyphenate_word(std::string_view, std::string_view, HyphenBreak*, int) { return 0; }
bool hyphenation_available(std::string_view) { return false; }

}  // namespace repub

#endif /* REPUB_ENABLE_HYPHENATION */

namespace repub {

std::string_view primary_language(std::string_view tag) {
    if (tag.empty()) return std::string_view();
    size_t end = 0;
    while (end < tag.size() && tag[end] != '-' && tag[end] != '_') ++end;
    std::string_view primary = tag.substr(0, end);
    if (primary.size() != 3) return primary;

    struct Mapping {
        const char* code3;
        const char* code2;
    };
    static const Mapping kMappings[] = {
        {"eng", "en"}, {"fra", "fr"}, {"fre", "fr"}, {"deu", "de"}, {"ger", "de"},
        {"rus", "ru"}, {"spa", "es"}, {"ita", "it"}, {"ukr", "uk"}, {"swe", "sv"},
        {"fin", "fi"}, {"pol", "pl"}, {"por", "pt"}, {"nld", "nl"}, {"dut", "nl"},
        {"dan", "da"}, {"nor", "no"}, {"cze", "cs"}, {"ces", "cs"}, {"gre", "el"},
        {"ell", "el"}, {"jpn", "ja"}, {"chi", "zh"}, {"zho", "zh"}, {"kor", "ko"},
    };
    auto lower = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    for (const Mapping& mapping : kMappings) {
        bool match = true;
        for (size_t i = 0; i < 3; ++i) {
            if (lower(primary[i]) != mapping.code3[i]) {
                match = false;
                break;
            }
        }
        if (match) return std::string_view(mapping.code2);
    }
    return primary;
}

}  // namespace repub
