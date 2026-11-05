/*
 * RollingEPUB hyphenation tests.
 *
 * SPDX-License-Identifier: MIT
 */

#include "layout/hyphen.h"

#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "test_harness.h"

namespace {

std::vector<uint16_t> offsets_for(const std::string& word, const char* language,
                                  std::vector<bool>* insert = nullptr) {
    repub::HyphenBreak breaks[32];
    int count = repub::hyphenate_word(word, language, breaks, 32);
    std::vector<uint16_t> offsets;
    for (int i = 0; i < count; ++i) {
        offsets.push_back(breaks[i].byte_offset);
        if (insert) insert->push_back(breaks[i].insert_hyphen);
    }
    return offsets;
}

bool is_utf8_lead(unsigned char c) { return (c & 0xC0) != 0x80; }

bool offsets_are_utf8_boundaries(const std::string& word, const std::vector<uint16_t>& offsets) {
    for (uint16_t offset : offsets) {
        if (offset >= word.size()) return false;
        if (!is_utf8_lead(static_cast<unsigned char>(word[offset]))) return false;
    }
    return true;
}

}  // namespace

REPUB_TEST(hyphen_english_known_words) {
    REPUB_CHECK_EQ(offsets_for("computer", "en"), (std::vector<uint16_t>{3}));
    REPUB_CHECK_EQ(offsets_for("extraordinary", "en"), (std::vector<uint16_t>{5, 7, 9}));
    REPUB_CHECK_EQ(offsets_for("representation", "en"), (std::vector<uint16_t>{3, 5, 8, 10}));
    /* "hyphenation" is a good sanity check that patterns load at all. */
    std::vector<uint16_t> hyphenation = offsets_for("hyphenation", "en");
    REPUB_CHECK(!hyphenation.empty());
    for (uint16_t offset : hyphenation) REPUB_CHECK(offset >= 3 && offset < 11);
    /* All pattern breaks need a visible hyphen. */
    std::vector<bool> insert;
    offsets_for("computer", "en", &insert);
    REPUB_CHECK(!insert.empty());
    for (bool value : insert) REPUB_CHECK(value);
}

REPUB_TEST(hyphen_short_words) {
    REPUB_CHECK(offsets_for("a", "en").empty());
    REPUB_CHECK(offsets_for("an", "en").empty());
    REPUB_CHECK(offsets_for("cat", "en").empty());
    REPUB_CHECK(offsets_for("the", "en").empty());
    REPUB_CHECK(offsets_for("", "en").empty());
}

REPUB_TEST(hyphen_explicit_hyphen_and_soft_hyphen) {
    /* "well-known": a hard hyphen is already visible -> no inserted hyphen. */
    std::vector<bool> insert;
    std::vector<uint16_t> offsets = offsets_for("well-known", "en", &insert);
    bool found = false;
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (offsets[i] == 5) {
            found = true;
            REPUB_CHECK(!insert[i]);
        }
    }
    REPUB_CHECK(found);

    /* "hy\xC2\xADphen": a soft hyphen is invisible -> break inserts a hyphen. */
    std::string soft = "hy\xC2\xADphen";
    insert.clear();
    offsets = offsets_for(soft, "en", &insert);
    found = false;
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (offsets[i] == 4) {
            found = true;
            REPUB_CHECK(insert[i]);
        }
    }
    REPUB_CHECK(found);
    REPUB_CHECK(offsets_are_utf8_boundaries(soft, offsets));
}

REPUB_TEST(hyphen_utf8_safe) {
    /* Non-ASCII Latin words must not crash and must report UTF-8 boundaries. */
    const char* words[] = {"caf\xC3\xA9", "\xC3\xBC" "berraschung", "na\xC3\xAFve", "co\xC3\xB6perate"};
    for (const char* word : words) {
        std::vector<uint16_t> offsets = offsets_for(word, "en");
        REPUB_CHECK(offsets_are_utf8_boundaries(word, offsets));
        for (size_t i = 1; i < offsets.size(); ++i) REPUB_CHECK(offsets[i] > offsets[i - 1]);
    }
    /* Unknown/disabled languages report no breaks. */
    REPUB_CHECK(offsets_for("computer", "xx").empty());
    REPUB_CHECK(offsets_for("computer", "zz-ZZ").empty());
    REPUB_CHECK(offsets_for("computer", "").empty());
    REPUB_CHECK(repub::hyphenation_available("en"));
}

REPUB_TEST(hyphen_availability_and_primary_language) {
    REPUB_CHECK(repub::hyphenation_available("en"));
    REPUB_CHECK(repub::hyphenation_available("en-US"));
    REPUB_CHECK(repub::hyphenation_available("EN"));
    REPUB_CHECK(!repub::hyphenation_available("xx"));
    REPUB_CHECK(!repub::hyphenation_available(""));

    auto check_primary = [](const char* tag, const char* expected) {
        std::string_view primary = repub::primary_language(tag);
        return std::string(primary) == expected;
    };
    REPUB_CHECK(check_primary("en-US", "en"));
    REPUB_CHECK(check_primary("zh-CN", "zh"));
    REPUB_CHECK(check_primary("eng", "en"));
    REPUB_CHECK(check_primary("fra", "fr"));
    REPUB_CHECK(check_primary("deu", "de"));
    REPUB_CHECK(check_primary("en", "en"));
    REPUB_CHECK(repub::primary_language("").empty());
}

REPUB_TEST(hyphen_cap_and_determinism) {
    std::string word = "internationalization";
    repub::HyphenBreak small[2];
    int count = repub::hyphenate_word(word, "en", small, 2);
    REPUB_CHECK_EQ(count, 2);
    REPUB_CHECK(small[0].byte_offset < small[1].byte_offset);

    repub::HyphenBreak a[32];
    repub::HyphenBreak b[32];
    int na = repub::hyphenate_word(word, "en", a, 32);
    int nb = repub::hyphenate_word(word, "en", b, 32);
    REPUB_CHECK_EQ(na, nb);
    for (int i = 0; i < na; ++i) {
        REPUB_CHECK_EQ(a[i].byte_offset, b[i].byte_offset);
        REPUB_CHECK_EQ(a[i].insert_hyphen, b[i].insert_hyphen);
    }
    /* offset 0 is never a valid break */
    for (int i = 0; i < na; ++i) REPUB_CHECK(a[i].byte_offset > 0);
}
