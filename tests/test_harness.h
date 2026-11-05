/*
 * RollingEPUB test harness: dependency-free unit test registry.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef REPUB_TESTS_TEST_HARNESS_H
#define REPUB_TESTS_TEST_HARNESS_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace repubtest {

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();
int run_all(int argc, char** argv);

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back(TestCase{name, fn}); }
};

}  // namespace repubtest

#define REPUB_TEST(name)                                              \
    static void repub_test_##name();                                  \
    static ::repubtest::Registrar repub_test_reg_##name(#name,        \
                                                        repub_test_##name); \
    static void repub_test_##name()

#define REPUB_CHECK(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__,        \
                         __LINE__, #cond);                                    \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

#define REPUB_CHECK_EQ(a, b)                                                        \
    do {                                                                            \
        const auto repub_a = (a);                                                   \
        const auto repub_b = (b);                                                   \
        if (!(repub_a == repub_b)) {                                                \
            std::fprintf(stderr, "CHECK_EQ FAILED %s:%d: %s == %s\n", __FILE__,     \
                         __LINE__, #a, #b);                                         \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)

#define REPUB_CHECK_STR_EQ(a, b)                                                     \
    do {                                                                             \
        const std::string repub_a = (a);                                             \
        const std::string repub_b = (b);                                             \
        if (repub_a != repub_b) {                                                    \
            std::fprintf(stderr, "CHECK_STR_EQ FAILED %s:%d\n  actual:   [%s]\n  expected: [%s]\n", \
                         __FILE__, __LINE__, repub_a.c_str(), repub_b.c_str());      \
            std::exit(1);                                                            \
        }                                                                            \
    } while (0)

/* Fixture directory (tests/fixtures). */
const char* fixture_dir();
std::string fixture_path(const std::string& name);
/* Read a whole file, empty on failure. */
std::string read_file(const std::string& path);

#endif  // REPUB_TESTS_TEST_HARNESS_H
