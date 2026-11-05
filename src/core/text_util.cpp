/*
 * RollingEPUB text and path utilities.
 *
 * SPDX-License-Identifier: MIT
 *
 * Provenance:
 *  - UTF-8 decoding, entity handling and package path resolution are adapted
 *    from Read Pico main/book/book_epub.c (SPDX-License-Identifier:
 *    Apache-2.0), rewritten in C++ with the same tolerant behaviour.
 *  - The named HTML entity table follows the common HTML 4 / XHTML set used by
 *    CrossMux lib/Epub/Epub/htmlEntities.cpp (MIT).
 */
#include "core/text_util.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace repub {

namespace {

bool ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct NamedEntity {
    const char* name;
    uint32_t cp;
};

/* Common EPUB/XHTML named entities; small enough for a linear scan (only
 * entered after a '&'). */
const NamedEntity kEntities[] = {
    {"AElig", 0x00C6},    {"Aacute", 0x00C1},   {"Acirc", 0x00C2},    {"Agrave", 0x00C0},
    {"Alpha", 0x0391},    {"Aring", 0x00C5},    {"Atilde", 0x00C3},   {"Auml", 0x00C4},
    {"Beta", 0x0392},     {"Ccedil", 0x00C7},   {"Chi", 0x03A7},      {"Dagger", 0x2021},
    {"Delta", 0x0394},    {"ETH", 0x00D0},      {"Eacute", 0x00C9},   {"Ecirc", 0x00CA},
    {"Egrave", 0x00C8},   {"Epsilon", 0x0395},  {"Eta", 0x0397},      {"Euml", 0x00CB},
    {"Gamma", 0x0393},    {"Iacute", 0x00CD},   {"Icirc", 0x00CE},    {"Igrave", 0x00CC},
    {"Iota", 0x0399},     {"Iuml", 0x00CF},     {"Kappa", 0x039A},    {"Lambda", 0x039B},
    {"Mu", 0x039C},       {"Ntilde", 0x00D1},   {"Nu", 0x039D},       {"OElig", 0x0152},
    {"Oacute", 0x00D3},   {"Ocirc", 0x00D4},    {"Ograve", 0x00D2},   {"Omega", 0x03A9},
    {"Omicron", 0x039F},  {"Oslash", 0x00D8},   {"Otilde", 0x00D5},   {"Ouml", 0x00D6},
    {"Phi", 0x03A6},      {"Pi", 0x03A0},       {"Prime", 0x2033},    {"Psi", 0x03A8},
    {"Rho", 0x03A1},      {"Scaron", 0x0160},   {"Sigma", 0x03A3},    {"THORN", 0x00DE},
    {"Tau", 0x03A4},      {"Theta", 0x0398},    {"Uacute", 0x00DA},   {"Ucirc", 0x00DB},
    {"Ugrave", 0x00D9},   {"Upsilon", 0x03A5},  {"Uuml", 0x00DC},     {"Xi", 0x039E},
    {"Yacute", 0x00DD},   {"Yuml", 0x0178},     {"Zeta", 0x0396},     {"aacute", 0x00E1},
    {"acirc", 0x00E2},    {"acute", 0x00B4},    {"aelig", 0x00E6},    {"agrave", 0x00E0},
    {"alefsym", 0x2135},  {"alpha", 0x03B1},    {"amp", 0x0026},      {"and", 0x2227},
    {"ang", 0x2220},      {"apos", 0x0027},     {"aring", 0x00E5},    {"asymp", 0x2248},
    {"atilde", 0x00E3},   {"auml", 0x00E4},     {"bdquo", 0x201E},    {"beta", 0x03B2},
    {"brvbar", 0x00A6},   {"bull", 0x2022},     {"cap", 0x2229},      {"ccedil", 0x00E7},
    {"cedil", 0x00B8},    {"cent", 0x00A2},     {"chi", 0x03C7},      {"circ", 0x02C6},
    {"clubs", 0x2663},    {"cong", 0x2245},     {"copy", 0x00A9},     {"crarr", 0x21B5},
    {"cup", 0x222A},      {"curren", 0x00A4},   {"dArr", 0x21D3},     {"dagger", 0x2020},
    {"darr", 0x2193},     {"deg", 0x00B0},      {"delta", 0x03B4},    {"diams", 0x2666},
    {"divide", 0x00F7},   {"eacute", 0x00E9},   {"ecirc", 0x00EA},    {"egrave", 0x00E8},
    {"empty", 0x2205},    {"emsp", 0x2003},     {"ensp", 0x2002},     {"epsilon", 0x03B5},
    {"equiv", 0x2261},    {"eta", 0x03B7},      {"eth", 0x00F0},      {"euml", 0x00EB},
    {"euro", 0x20AC},     {"exist", 0x2203},    {"fnof", 0x0192},     {"forall", 0x2200},
    {"frac12", 0x00BD},   {"frac14", 0x00BC},   {"frac34", 0x00BE},   {"frasl", 0x2044},
    {"gamma", 0x03B3},    {"ge", 0x2265},       {"gt", 0x003E},       {"hArr", 0x21D4},
    {"harr", 0x2194},     {"hearts", 0x2665},   {"hellip", 0x2026},   {"iacute", 0x00ED},
    {"icirc", 0x00EE},    {"iexcl", 0x00A1},    {"igrave", 0x00EC},   {"image", 0x2111},
    {"infin", 0x221E},    {"int", 0x222B},      {"iota", 0x03B9},     {"iquest", 0x00BF},
    {"isin", 0x2208},     {"iuml", 0x00EF},     {"kappa", 0x03BA},    {"lArr", 0x21D0},
    {"lambda", 0x03BB},   {"lang", 0x27E8},     {"laquo", 0x00AB},    {"larr", 0x2190},
    {"lceil", 0x2308},    {"ldquo", 0x201C},    {"le", 0x2264},       {"lfloor", 0x230A},
    {"lowast", 0x2217},   {"loz", 0x25CA},      {"lrm", 0x200E},      {"lsaquo", 0x2039},
    {"lsquo", 0x2018},    {"lt", 0x003C},       {"macr", 0x00AF},     {"mdash", 0x2014},
    {"micro", 0x00B5},    {"middot", 0x00B7},   {"minus", 0x2212},    {"mu", 0x03BC},
    {"nabla", 0x2207},    {"nbsp", 0x00A0},     {"ndash", 0x2013},    {"ne", 0x2260},
    {"ni", 0x220B},       {"not", 0x00AC},      {"notin", 0x2209},    {"nsub", 0x2284},
    {"ntilde", 0x00F1},   {"nu", 0x03BD},       {"oacute", 0x00F3},   {"ocirc", 0x00F4},
    {"oelig", 0x0153},    {"ograve", 0x00F2},   {"oline", 0x203E},    {"omega", 0x03C9},
    {"omicron", 0x03BF},  {"oplus", 0x2295},    {"or", 0x2228},       {"ordf", 0x00AA},
    {"ordm", 0x00BA},     {"oslash", 0x00F8},   {"otilde", 0x00F5},   {"otimes", 0x2297},
    {"ouml", 0x00F6},     {"para", 0x00B6},     {"part", 0x2202},     {"permil", 0x2030},
    {"perp", 0x22A5},     {"phi", 0x03C6},      {"pi", 0x03C0},       {"piv", 0x03D6},
    {"plusmn", 0x00B1},   {"pound", 0x00A3},    {"prime", 0x2032},    {"prod", 0x220F},
    {"prop", 0x221D},     {"psi", 0x03C8},      {"quot", 0x0022},     {"rArr", 0x21D2},
    {"radic", 0x221A},    {"rang", 0x27E9},     {"raquo", 0x00BB},    {"rarr", 0x2192},
    {"rceil", 0x2309},    {"rdquo", 0x201D},    {"real", 0x211C},     {"reg", 0x00AE},
    {"rfloor", 0x230B},   {"rho", 0x03C1},      {"rlm", 0x200F},      {"rsaquo", 0x203A},
    {"rsquo", 0x2019},    {"sbquo", 0x201A},    {"scaron", 0x0161},   {"sdot", 0x22C5},
    {"sect", 0x00A7},     {"shy", 0x00AD},      {"sigma", 0x03C3},    {"sigmaf", 0x03C2},
    {"sim", 0x223C},      {"spades", 0x2660},   {"sub", 0x2282},      {"sube", 0x2286},
    {"sum", 0x2211},      {"sup", 0x2283},      {"sup1", 0x00B9},     {"sup2", 0x00B2},
    {"sup3", 0x00B3},     {"supe", 0x2287},     {"szlig", 0x00DF},    {"tau", 0x03C4},
    {"there4", 0x2234},   {"theta", 0x03B8},    {"thetasym", 0x03D1}, {"thinsp", 0x2009},
    {"thorn", 0x00FE},    {"tilde", 0x02DC},    {"times", 0x00D7},    {"trade", 0x2122},
    {"uArr", 0x21D1},     {"uacute", 0x00FA},   {"uarr", 0x2191},     {"ucirc", 0x00FB},
    {"ugrave", 0x00F9},   {"uml", 0x00A8},      {"upsih", 0x03D2},    {"upsilon", 0x03C5},
    {"uuml", 0x00FC},     {"weierp", 0x2118},   {"xi", 0x03BE},       {"yacute", 0x00FD},
    {"yen", 0x00A5},      {"yuml", 0x00FF},     {"zeta", 0x03B6},     {"zwj", 0x200D},
    {"zwnj", 0x200C},
};

bool lookup_entity(std::string_view name, uint32_t* cp) {
    for (const NamedEntity& entity : kEntities) {
        if (name == entity.name) {
            *cp = entity.cp;
            return true;
        }
    }
    return false;
}

/* Decode one scalar; returns bytes consumed or 0 when invalid. */
size_t decode_scalar(const char* data, size_t size, uint32_t* cp) {
    const unsigned char c0 = static_cast<unsigned char>(data[0]);
    if (c0 < 0x80) {
        *cp = c0;
        return 1;
    }
    size_t count = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;
    if (c0 >= 0xC2 && c0 <= 0xDF) {
        count = 2;
        value = c0 & 0x1F;
        minimum = 0x80;
    } else if (c0 >= 0xE0 && c0 <= 0xEF) {
        count = 3;
        value = c0 & 0x0F;
        minimum = 0x800;
    } else if (c0 >= 0xF0 && c0 <= 0xF4) {
        count = 4;
        value = c0 & 0x07;
        minimum = 0x10000;
    } else {
        return 0;
    }
    if (size < count) return 0;
    for (size_t i = 1; i < count; ++i) {
        const unsigned char b = static_cast<unsigned char>(data[i]);
        if ((b & 0xC0) != 0x80) return 0;
        value = (value << 6) | (b & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return 0;
    *cp = value;
    return count;
}

bool has_ascii_prefix_ci(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (lower_ascii(text[i]) != lower_ascii(prefix[i])) return false;
    }
    return true;
}

}  // namespace

/* ------------------------------------------------------------------------- */
/* Strings                                                                   */
/* ------------------------------------------------------------------------- */

std::string to_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = lower_ascii(c);
    return out;
}

std::string trim_left(std::string_view text) {
    size_t start = 0;
    while (start < text.size() && ascii_space(text[start])) ++start;
    return std::string(text.substr(start));
}

std::string trim_right(std::string_view text) {
    size_t end = text.size();
    while (end > 0 && ascii_space(text[end - 1])) --end;
    return std::string(text.substr(0, end));
}

std::string trim(std::string_view text) { return trim_left(trim_right(text)); }

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool starts_with_ci(std::string_view text, std::string_view prefix) {
    return has_ascii_prefix_ci(text, prefix);
}

bool ends_with_ci(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    const size_t offset = text.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (lower_ascii(text[offset + i]) != lower_ascii(suffix[i])) return false;
    }
    return true;
}

bool contains_ci(std::string_view text, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > text.size()) return false;
    for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (lower_ascii(text[i + j]) != lower_ascii(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    if (text.empty()) return parts;
    size_t start = 0;
    for (;;) {
        const size_t pos = text.find(delimiter, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    size_t total = sep.size() * (parts.empty() ? 0 : parts.size() - 1);
    for (const std::string& part : parts) total += part.size();
    out.reserve(total);
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out.append(sep.data(), sep.size());
        out.append(parts[i]);
    }
    return out;
}

std::string collapse_whitespace(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pending_space = false;
    for (char c : text) {
        if (ascii_space(c)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(c);
    }
    return out;
}

int parse_int(std::string_view text, int fallback) {
    const std::string value = trim(text);
    if (value.empty()) return fallback;
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) return fallback;
    while (end && *end && ascii_space(*end)) ++end;
    if (end == nullptr || *end != '\0') return fallback;
    if (errno == ERANGE) {
        return parsed < 0 ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    }
    if (parsed < std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
    if (parsed > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
    return static_cast<int>(parsed);
}

bool parse_float_strict(std::string_view text, float* out) {
    if (!out) return false;
    const std::string value = trim(text);
    if (value.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
    *out = parsed;
    return true;
}

float parse_float(std::string_view text, float fallback) {
    float out = 0.0f;
    if (!parse_float_strict(text, &out)) return fallback;
    return out;
}

int compare_ci(std::string_view a, std::string_view b) {
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ca = static_cast<unsigned char>(lower_ascii(a[i]));
        const unsigned char cb = static_cast<unsigned char>(lower_ascii(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

/* ------------------------------------------------------------------------- */
/* UTF-8                                                                     */
/* ------------------------------------------------------------------------- */

size_t utf8_decode(const char* data, size_t size, uint32_t* cp) {
    if (!cp) return 1;
    if (!data || size == 0) {
        *cp = 0xFFFD;
        return 1;
    }
    const size_t consumed = decode_scalar(data, size, cp);
    if (consumed) return consumed;
    *cp = 0xFFFD;
    return 1;
}

size_t utf8_encode(uint32_t cp, char out[4]) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}

size_t utf8_prev(const char* data, size_t offset) {
    if (!data || offset == 0) return 0;
    size_t index = offset - 1;
    while (index > 0 && (static_cast<unsigned char>(data[index]) & 0xC0) == 0x80) --index;
    return index;
}

size_t utf8_length(const char* data, size_t size) {
    if (!data) return 0;
    size_t count = 0;
    size_t offset = 0;
    while (offset < size) {
        uint32_t cp = 0;
        offset += utf8_decode(data + offset, size - offset, &cp);
        ++count;
    }
    return count;
}

bool utf8_valid(const char* data, size_t size) {
    if (!data) return size == 0;
    size_t offset = 0;
    while (offset < size) {
        uint32_t cp = 0;
        const size_t consumed = decode_scalar(data + offset, size - offset, &cp);
        if (!consumed) return false;
        offset += consumed;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* Character properties                                                      */
/* ------------------------------------------------------------------------- */

bool is_space(uint32_t cp) {
    if (cp == 0x09 || cp == 0x0A || cp == 0x0B || cp == 0x0C || cp == 0x0D || cp == 0x20) return true;
    if (cp == 0x00A0 || cp == 0x1680 || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
        cp == 0x205F || cp == 0x3000) {
        return true;
    }
    return cp >= 0x2000 && cp <= 0x200A;
}

bool is_horizontal_space(uint32_t cp) {
    if (cp == 0x0A || cp == 0x0D || cp == 0x2028 || cp == 0x2029) return false;
    return is_space(cp);
}

bool is_newline(uint32_t cp) {
    return cp == 0x0A || cp == 0x0D || cp == 0x2028 || cp == 0x2029;
}

bool is_cjk(uint32_t cp) {
    if (cp >= 0x1100 && cp <= 0x11FF) return true;   /* Hangul Jamo */
    if (cp >= 0x2E80 && cp <= 0x2FFF) return true;   /* radicals / Kangxi */
    if (cp >= 0x3000 && cp <= 0x303F) return true;   /* CJK symbols and punctuation */
    if (cp >= 0x3040 && cp <= 0x30FF) return true;   /* Hiragana / Katakana */
    if (cp >= 0x3100 && cp <= 0x312F) return true;   /* Bopomofo */
    if (cp >= 0x3130 && cp <= 0x318F) return true;   /* Hangul compatibility Jamo */
    if (cp >= 0x3190 && cp <= 0x31FF) return true;   /* Kanbun / strokes / Katakana ext */
    if (cp >= 0x3200 && cp <= 0x33FF) return true;   /* enclosed / compatibility CJK */
    if (cp >= 0x3400 && cp <= 0x4DBF) return true;   /* CJK ext A */
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;   /* CJK unified */
    if (cp >= 0xA960 && cp <= 0xA97F) return true;   /* Hangul Jamo ext A */
    if (cp >= 0xAC00 && cp <= 0xD7AF) return true;   /* Hangul syllables */
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;   /* CJK compatibility ideographs */
    if (cp >= 0xFE10 && cp <= 0xFE1F) return true;   /* vertical forms */
    if (cp >= 0xFE30 && cp <= 0xFE6F) return true;   /* CJK compatibility forms */
    if (cp >= 0xFF00 && cp <= 0xFFEF) return true;   /* half/fullwidth forms */
    if (cp >= 0x1AFF0 && cp <= 0x1B16F) return true; /* Kana extended/supplement */
    if (cp >= 0x20000 && cp <= 0x3FFFD) return true; /* CJK ext B..G */
    return false;
}

bool is_cjk_punctuation(uint32_t cp) {
    switch (cp) {
        case 0x3001:
        case 0x3002:
        case 0x3030:
        case 0x30FB:
        case 0xFF01:
        case 0xFF0C:
        case 0xFF0E:
        case 0xFF1A:
        case 0xFF1B:
        case 0xFF1F:
        case 0xFF3B:
        case 0xFF3D:
        case 0xFF5B:
        case 0xFF5D:
        case 0xFF5E:
        case 0xFF61:
        case 0xFF64:
            return true;
        default:
            break;
    }
    if (cp >= 0x3008 && cp <= 0x3011) return true;
    if (cp >= 0x3014 && cp <= 0x301F) return true;
    if (cp >= 0xFE50 && cp <= 0xFE6F) return true;
    return false;
}

bool is_wide(uint32_t cp) {
    if (cp >= 0x1100 && cp <= 0x115F) return true; /* Hangul Jamo initial */
    if (cp >= 0x2E80 && cp <= 0xA4CF) return true; /* CJK .. Yi */
    if (cp >= 0xAC00 && cp <= 0xD7A3) return true; /* Hangul syllables */
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;
    if (cp >= 0xFE10 && cp <= 0xFE19) return true;
    if (cp >= 0xFE30 && cp <= 0xFE6F) return true;
    if (cp >= 0xFF00 && cp <= 0xFF60) return true;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return true;
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return true; /* emoji / pictographs */
    if (cp >= 0x20000 && cp <= 0x3FFFD) return true;
    return false;
}

bool is_latin_digit(uint32_t cp) { return cp >= '0' && cp <= '9'; }

bool is_combining(uint32_t cp) {
    if (cp >= 0x0300 && cp <= 0x036F) return true;
    if (cp >= 0x0483 && cp <= 0x0489) return true;
    if (cp >= 0x0591 && cp <= 0x05BD) return true;
    if (cp >= 0x0610 && cp <= 0x061A) return true;
    if (cp >= 0x064B && cp <= 0x065F) return true;
    if (cp == 0x0670) return true;
    if (cp >= 0x06D6 && cp <= 0x06DC) return true;
    if (cp == 0x0E31) return true;
    if (cp >= 0x0E34 && cp <= 0x0E3A) return true;
    if (cp >= 0x0E47 && cp <= 0x0E4E) return true;
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return true;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return true;
    if (cp >= 0x20D0 && cp <= 0x20FF) return true;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;   /* variation selectors */
    if (cp >= 0xFE20 && cp <= 0xFE2F) return true;
    if (cp >= 0xE0100 && cp <= 0xE01EF) return true; /* variation selectors supplement */
    return false;
}

bool is_rtl(uint32_t cp) {
    if (cp >= 0x0590 && cp <= 0x05FF) return true; /* Hebrew */
    if (cp >= 0x0600 && cp <= 0x06FF) return true; /* Arabic */
    if (cp >= 0x0700 && cp <= 0x074F) return true; /* Syriac */
    if (cp >= 0x0750 && cp <= 0x077F) return true; /* Arabic supplement */
    if (cp >= 0x0780 && cp <= 0x07BF) return true; /* Thaana */
    if (cp >= 0x07C0 && cp <= 0x07FF) return true; /* NKo */
    if (cp >= 0x0800 && cp <= 0x083F) return true; /* Samaritan */
    if (cp >= 0x0840 && cp <= 0x085F) return true; /* Mandaic */
    if (cp >= 0x08A0 && cp <= 0x08FF) return true; /* Arabic ext A */
    if (cp >= 0xFB1D && cp <= 0xFDFF) return true; /* Hebrew/Arabic presentation */
    if (cp >= 0xFE70 && cp <= 0xFEFF) return true; /* Arabic presentation B */
    if (cp >= 0x10800 && cp <= 0x10FFF) return true;
    if (cp >= 0x1E800 && cp <= 0x1EFFF) return true;
    return false;
}

/* ------------------------------------------------------------------------- */
/* HTML entities                                                             */
/* ------------------------------------------------------------------------- */

size_t decode_entity(const char* data, size_t size, uint32_t* cp) {
    if (!data || !cp || size < 3 || data[0] != '&') return 0;

    size_t index = 1;
    if (data[index] == '#') {
        ++index;
        bool hex = false;
        if (index < size && (data[index] == 'x' || data[index] == 'X')) {
            hex = true;
            ++index;
        }
        const uint32_t base = hex ? 16u : 10u;
        uint64_t value = 0;
        size_t digits = 0;
        while (index < size && data[index] != ';') {
            const int digit = hex_digit(data[index]);
            if (digit < 0 || static_cast<uint32_t>(digit) >= base) return 0;
            ++digits;
            if (value <= 0x10FFFF) value = value * base + static_cast<uint64_t>(digit);
            ++index;
        }
        if (index >= size || data[index] != ';' || digits == 0) return 0;
        if (value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) value = 0xFFFD;
        *cp = static_cast<uint32_t>(value);
        return index + 1;
    }

    const size_t start = index;
    while (index < size && data[index] != ';' && index - start < 32) ++index;
    if (index >= size || data[index] != ';') return 0;
    const std::string_view name(data + start, index - start);
    if (name.empty()) return 0;
    uint32_t value = 0;
    if (!lookup_entity(name, &value)) return 0;
    *cp = value;
    return index + 1;
}

std::string decode_entities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    size_t offset = 0;
    while (offset < text.size()) {
        if (text[offset] == '&') {
            uint32_t cp = 0;
            const size_t consumed = decode_entity(text.data() + offset, text.size() - offset, &cp);
            if (consumed) {
                char bytes[4];
                const size_t written = utf8_encode(cp, bytes);
                out.append(bytes, written);
                offset += consumed;
                continue;
            }
        }
        uint32_t cp = 0;
        const size_t consumed = utf8_decode(text.data() + offset, text.size() - offset, &cp);
        char bytes[4];
        const size_t written = utf8_encode(cp, bytes);
        out.append(bytes, written);
        offset += consumed;
    }
    return out;
}

/* ------------------------------------------------------------------------- */
/* Paths and hrefs                                                           */
/* ------------------------------------------------------------------------- */

std::string normalize_path(std::string_view path) {
    std::vector<std::string_view> segments;
    size_t index = 0;
    while (index < path.size()) {
        while (index < path.size() && (path[index] == '/' || path[index] == '\\')) ++index;
        if (index >= path.size()) break;
        const size_t start = index;
        while (index < path.size() && path[index] != '/' && path[index] != '\\') ++index;
        const std::string_view segment = path.substr(start, index - start);
        if (segment == ".") continue;
        if (segment == "..") {
            if (!segments.empty()) segments.pop_back();
            continue;
        }
        segments.push_back(segment);
    }
    std::string out;
    size_t total = segments.empty() ? 0 : segments.size() - 1;
    for (const std::string_view segment : segments) total += segment.size();
    out.reserve(total);
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i) out.push_back('/');
        out.append(segments[i].data(), segments[i].size());
    }
    return out;
}

std::string dirname_of(std::string_view path) {
    const size_t pos = path.find_last_of("/\\");
    if (pos == std::string_view::npos) return std::string();
    return normalize_path(path.substr(0, pos));
}

std::string resolve_path(std::string_view base_dir, std::string_view href) {
    const std::string stripped = strip_url_suffix(href);
    const std::string file = percent_decode(stripped);
    if (file.empty()) return normalize_path(base_dir);
    if (is_external_href(file)) return file;
    if (file[0] == '/' || file[0] == '\\') return normalize_path(file);
    std::string joined = normalize_path(base_dir);
    if (!joined.empty()) joined.push_back('/');
    joined.append(file);
    return normalize_path(joined);
}

void split_fragment(std::string_view href, std::string* file, std::string* fragment) {
    const size_t pos = href.find('#');
    if (pos == std::string_view::npos) {
        if (file) file->assign(href.data(), href.size());
        if (fragment) fragment->clear();
        return;
    }
    if (file) file->assign(href.data(), pos);
    if (fragment) fragment->assign(href.data() + pos + 1, href.size() - pos - 1);
}

std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hex_digit(text[i + 1]);
            const int lo = hex_digit(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string strip_url_suffix(std::string_view href) {
    const size_t pos = href.find_first_of("?#");
    if (pos == std::string_view::npos) return std::string(href);
    return std::string(href.substr(0, pos));
}

bool is_external_href(std::string_view href) {
    size_t start = 0;
    while (start < href.size() && ascii_space(href[start])) ++start;
    const std::string_view text = href.substr(start);
    static const char* const kSchemes[] = {
        "http:", "https:", "mailto:", "ftp:", "ftps:", "tel:", "javascript:", "data:", "file:"};
    for (const char* scheme : kSchemes) {
        if (has_ascii_prefix_ci(text, scheme)) return true;
    }
    return false;
}

}  // namespace repub
