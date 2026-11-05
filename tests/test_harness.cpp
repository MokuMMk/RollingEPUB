/* SPDX-License-Identifier: MIT */
#include "test_harness.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace repubtest {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int run_all(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int failed = 0;
    int ran = 0;
    for (const TestCase& test : registry()) {
        if (filter && std::strstr(test.name, filter) == nullptr) continue;
        ++ran;
        std::fprintf(stderr, "[ RUN  ] %s\n", test.name);
        test.fn();
        std::fprintf(stderr, "[  OK  ] %s\n", test.name);
    }
    if (failed == 0) {
        std::fprintf(stderr, "[ PASS ] %d test(s)\n", ran);
        return ran > 0 ? 0 : 1;
    }
    std::fprintf(stderr, "[ FAIL ] %d test(s)\n", failed);
    return 1;
}

}  // namespace repubtest

int main(int argc, char** argv) { return repubtest::run_all(argc, argv); }

const char* fixture_dir() { return REPUB_TEST_FIXTURES; }

std::string fixture_path(const std::string& name) {
    return std::string(fixture_dir()) + "/" + name;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
