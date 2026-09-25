// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sstream>
#include <string>

#include "stipple/core/Geometry.h"
#include "stipple/core/Rgb.h"

// A ~120-line test runner rather than a vendored framework.
//
// Blueprint §32 requires reproducible builds with no silent downloads, and the
// core has zero dependencies by design. Catch2 or GoogleTest would each mean a
// FetchContent pull, a pinned revision, a licence entry and a CI network hop —
// for assertions and a main(). If the suite ever outgrows this, swapping it is
// an isolated change behind these macros.

namespace stipple {
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
}  // namespace stipple

#define STIPPLE_TEST(suite, name)                                                                   \
    static void stipple_test_##suite##_##name();                                                    \
    static const ::stipple::test::Registrar stipple_registrar_##suite##_##name(                      \
        #suite, #name, &stipple_test_##suite##_##name);                                             \
    static void stipple_test_##suite##_##name()

#define STIPPLE_CHECK(expr)                                                                         \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::stipple::test::Registry::instance().fail(__FILE__, __LINE__,                          \
                                                      std::string("expected true: ") + #expr);     \
        }                                                                                          \
    } while (false)

/// Like STIPPLE_CHECK, but abandons the rest of the test when it fails.
///
/// STIPPLE_CHECK deliberately records and carries on, so one test can report
/// several problems at once. That is the right default — until the thing being
/// checked is a pointer the following lines dereference, at which point carrying
/// on turns a readable failure into a segfault with no output at all.
///
/// Use this for preconditions: "the message exists", "the app was found".
#define STIPPLE_REQUIRE(expr)                                                                       \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::stipple::test::Registry::instance().fail(__FILE__, __LINE__,                          \
                                                      std::string("required: ") + #expr);          \
            return;                                                                                \
        }                                                                                          \
    } while (false)

#define STIPPLE_CHECK_FALSE(expr)                                                                   \
    do {                                                                                           \
        if ((expr)) {                                                                              \
            ::stipple::test::Registry::instance().fail(__FILE__, __LINE__,                          \
                                                      std::string("expected false: ") + #expr);    \
        }                                                                                          \
    } while (false)

#define STIPPLE_CHECK_EQ(actual, expected)                                                          \
    do {                                                                                           \
        const auto& stipple_actual = (actual);                                                      \
        const auto& stipple_expected = (expected);                                                  \
        if (!(stipple_actual == stipple_expected)) {                                                 \
            std::ostringstream stipple_stream;                                                      \
            stipple_stream << #actual << " == " << #expected << "\n           actual:   "           \
                          << ::stipple::test::describe(stipple_actual) << "\n           expected: "  \
                          << ::stipple::test::describe(stipple_expected);                            \
            ::stipple::test::Registry::instance().fail(__FILE__, __LINE__, stipple_stream.str());    \
        }                                                                                          \
    } while (false)
