// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sstream>
#include <string>

#include "notrix/core/Geometry.h"
#include "notrix/core/Rgb.h"

// A ~120-line test runner rather than a vendored framework.
//
// Blueprint §32 requires reproducible builds with no silent downloads, and the
// core has zero dependencies by design. Catch2 or GoogleTest would each mean a
// FetchContent pull, a pinned revision, a licence entry and a CI network hop —
// for assertions and a main(). If the suite ever outgrows this, swapping it is
// an isolated change behind these macros.

namespace notrix {
namespace test {

using TestFunction = void (*)();

/// Generic description via operator<<, with overloads below for the core value
/// types so assertion failures print something readable instead of an address.
template <typename T>
std::string describe(const T& value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

std::string describe(const Rgb& color);
std::string describe(const Rect& rect);
std::string describe(const Point& point);
std::string describe(bool value);

class Registry {
public:
    static Registry& instance();

    void add(const char* suite, const char* name, TestFunction function);
    void fail(const char* file, int line, const std::string& message);

    /// Runs every registered test, or only those whose "suite.name" contains
    /// `filter`. Returns a process exit code.
    int runAll(const char* filter);

private:
    Registry() = default;
    struct Impl;
    Impl& impl();
};

struct Registrar {
    Registrar(const char* suite, const char* name, TestFunction function) {
        Registry::instance().add(suite, name, function);
    }
};

}  // namespace test
}  // namespace notrix

#define NOTRIX_TEST(suite, name)                                                                   \
    static void notrix_test_##suite##_##name();                                                    \
    static const ::notrix::test::Registrar notrix_registrar_##suite##_##name(                      \
        #suite, #name, &notrix_test_##suite##_##name);                                             \
    static void notrix_test_##suite##_##name()

#define NOTRIX_CHECK(expr)                                                                         \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::notrix::test::Registry::instance().fail(__FILE__, __LINE__,                          \
                                                      std::string("expected true: ") + #expr);     \
        }                                                                                          \
    } while (false)

#define NOTRIX_CHECK_FALSE(expr)                                                                   \
    do {                                                                                           \
        if ((expr)) {                                                                              \
            ::notrix::test::Registry::instance().fail(__FILE__, __LINE__,                          \
                                                      std::string("expected false: ") + #expr);    \
        }                                                                                          \
    } while (false)

#define NOTRIX_CHECK_EQ(actual, expected)                                                          \
    do {                                                                                           \
        const auto& notrix_actual = (actual);                                                      \
        const auto& notrix_expected = (expected);                                                  \
        if (!(notrix_actual == notrix_expected)) {                                                 \
            std::ostringstream notrix_stream;                                                      \
            notrix_stream << #actual << " == " << #expected << "\n           actual:   "           \
                          << ::notrix::test::describe(notrix_actual) << "\n           expected: "  \
                          << ::notrix::test::describe(notrix_expected);                            \
            ::notrix::test::Registry::instance().fail(__FILE__, __LINE__, notrix_stream.str());    \
        }                                                                                          \
    } while (false)
