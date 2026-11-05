/*
 * RollingEPUB text and path utilities: UTF-8, whitespace, HTML entity helpers,
 * ZIP/href path resolution.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_SRC_CORE_TEXT_UTIL_H
#define REPUB_SRC_CORE_TEXT_UTIL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace repub {

/* ---- Strings --------------------------------------------------------- */

std::string to_lower(std::string_view text);
std::string trim(std::string_view text);
std::string trim_left(std::string_view text);
std::string trim_right(std::string_view text);
bool starts_with(std::string_view text, std::string_view prefix);
bool starts_with_ci(std::string_view text, std::string_view prefix);
bool ends_with_ci(std::string_view text, std::string_view suffix);
bool contains_ci(std::string_view text, std::string_view needle);
std::vector<std::string> split(std::string_view text, char delimiter);
std::string join(const std::vector<std::string>& parts, std::string_view sep);
std::string collapse_whitespace(std::string_view text);
int parse_int(std::string_view text, int fallback = 0);
float parse_float(std::string_view text, float fallback = 0.0f);
bool parse_float_strict(std::string_view text, float* out);
/* Case-insensitive ASCII comparison. */
int compare_ci(std::string_view a, std::string_view b);

/* ---- UTF-8 ----------------------------------------------------------- */

/* Decode one codepoint. Returns bytes consumed (>=1); invalid input yields
 * U+FFFD and consumes 1 byte. */
size_t utf8_decode(const char* data, size_t size, uint32_t* cp);
/* Encode one codepoint. Returns bytes written (1..4). */
size_t utf8_encode(uint32_t cp, char out[4]);
size_t utf8_prev(const char* data, size_t offset);
size_t utf8_length(const char* data, size_t size);
bool utf8_valid(const char* data, size_t size);

bool is_space(uint32_t cp);
bool is_horizontal_space(uint32_t cp); /* space, tab, nbsp, ideographic space */
bool is_newline(uint32_t cp);
bool is_cjk(uint32_t cp);
bool is_cjk_punctuation(uint32_t cp);
bool is_wide(uint32_t cp);
bool is_latin_digit(uint32_t cp);
bool is_combining(uint32_t cp);
bool is_rtl(uint32_t cp);

/* ---- HTML entities --------------------------------------------------- */

/* Decode one entity starting at '&' (data[0] == '&'). Returns bytes consumed
 * including '&' and ';' when decoded, or 0 when not a valid entity.
 * Malformed numeric entities clamp to U+FFFD. */
size_t decode_entity(const char* data, size_t size, uint32_t* cp);
/* Decode all entities in a byte range into UTF-8. Also normalizes invalid
 * UTF-8 sequences to U+FFFD. */
std::string decode_entities(std::string_view text);

/* ---- Paths and hrefs ------------------------------------------------- */

/* Convert backslashes, remove duplicate slashes and resolve "." and "..". */
std::string normalize_path(std::string_view path);
/* Directory part of a package-relative path ("a/b/c.xhtml" -> "a/b"). */
std::string dirname_of(std::string_view path);
/* Join a base directory with a relative href; absolute hrefs are normalized
 * and returned without the leading slash. */
std::string resolve_path(std::string_view base_dir, std::string_view href);
/* Split "file.xhtml#frag" into file and fragment (either may be empty). */
void split_fragment(std::string_view href, std::string* file, std::string* fragment);
/* URL-decode %XX escapes in place (returns decoded string). */
std::string percent_decode(std::string_view text);
/* Strip query string and fragment. */
std::string strip_url_suffix(std::string_view href);
/* True for http(s):, mailto:, ftp:, tel:, javascript: links. */
bool is_external_href(std::string_view href);

}  // namespace repub

#endif  // REPUB_SRC_CORE_TEXT_UTIL_H
