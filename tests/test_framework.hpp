// SPDX-License-Identifier: MIT
// Shadow SE - minimal self-contained unit test framework.
#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

inline int g_checks = 0;
inline int g_failures = 0;

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back(TestCase{name, std::move(fn)});
    }
};

template <typename A, typename B>
void checkEq(const char* file, int line, const A& a, const B& b, const char* aStr,
             const char* bStr) {
    ++g_checks;
    if (!(a == b)) {
        ++g_failures;
        std::ostringstream os;
        os << file << ":" << line << "  FAIL  " << aStr << " == " << bStr << "\n"
           << "        left:  " << a << "\n"
           << "        right: " << b << "\n";
        std::fputs(os.str().c_str(), stderr);
    }
}

inline void checkTrue(const char* file, int line, bool cond, const char* expr) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::fprintf(stderr, "%s:%d  FAIL  %s\n", file, line, expr);
    }
}

inline int runAll() {
    int passed = 0;
    for (const TestCase& tc : registry()) {
        const int before = g_failures;
        try {
            tc.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::fprintf(stderr, "  EXCEPTION in %s: %s\n", tc.name.c_str(), e.what());
        } catch (...) {
            ++g_failures;
            std::fprintf(stderr, "  UNKNOWN EXCEPTION in %s\n", tc.name.c_str());
        }
        if (g_failures == before) {
            ++passed;
            std::printf("  [PASS] %s\n", tc.name.c_str());
        } else {
            std::printf("  [FAIL] %s\n", tc.name.c_str());
        }
    }
    std::printf("\n%d/%d tests passed, %d checks, %d failures\n", passed,
                static_cast<int>(registry().size()), g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

} // namespace testfw

#define TEST(name)                                                                  \
    static void test_##name();                                                      \
    static ::testfw::Registrar reg_##name(#name, test_##name);                      \
    static void test_##name()

#define CHECK(expr) ::testfw::checkTrue(__FILE__, __LINE__, (expr), #expr)
#define CHECK_EQ(a, b) ::testfw::checkEq(__FILE__, __LINE__, (a), (b), #a, #b)
