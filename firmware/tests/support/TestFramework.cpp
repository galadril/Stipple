// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestFramework.h"

#include <cstring>
#include <iostream>
#include <vector>

namespace notrix {
namespace test {

std::string describe(const Rgb& color) {
    std::ostringstream stream;
    stream << "rgb(" << static_cast<int>(color.r) << ", " << static_cast<int>(color.g) << ", "
           << static_cast<int>(color.b) << ")";
    return stream.str();
}

std::string describe(const Rect& rect) {
    std::ostringstream stream;
    stream << "rect(x=" << rect.x << ", y=" << rect.y << ", w=" << rect.w << ", h=" << rect.h << ")";
    return stream.str();
}

std::string describe(const Point& point) {
    std::ostringstream stream;
    stream << "point(" << point.x << ", " << point.y << ")";
    return stream.str();
}

std::string describe(bool value) {
    return value ? "true" : "false";
}

namespace {

struct TestCase {
    const char* suite = nullptr;
    const char* name = nullptr;
    TestFunction function = nullptr;
};

}  // namespace

struct Registry::Impl {
    std::vector<TestCase> tests;
    std::vector<std::string> currentFailures;
};

Registry& Registry::instance() {
    static Registry registry;
    return registry;
}

Registry::Impl& Registry::impl() {
    static Impl storage;
    return storage;
}

void Registry::add(const char* suite, const char* name, TestFunction function) {
    impl().tests.push_back(TestCase{suite, name, function});
}

void Registry::fail(const char* file, int line, const std::string& message) {
    std::ostringstream stream;
    stream << file << ":" << line << "\n           " << message;
    impl().currentFailures.push_back(stream.str());
}

int Registry::runAll(const char* filter) {
    Impl& state = impl();

    int passed = 0;
    int failed = 0;
    int skipped = 0;

    for (const TestCase& test : state.tests) {
        const std::string fullName = std::string(test.suite) + "." + test.name;
        if (filter != nullptr && std::strlen(filter) > 0 && fullName.find(filter) == std::string::npos) {
            ++skipped;
            continue;
        }

        state.currentFailures.clear();
        test.function();

        if (state.currentFailures.empty()) {
            ++passed;
            std::cout << "  PASS  " << fullName << "\n";
        } else {
            ++failed;
            std::cout << "  FAIL  " << fullName << "\n";
            for (const std::string& failure : state.currentFailures) {
                std::cout << "        " << failure << "\n";
            }
        }
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed";
    if (skipped > 0) {
        std::cout << ", " << skipped << " filtered out";
    }
    std::cout << std::endl;

    return failed == 0 ? 0 : 1;
}

}  // namespace test
}  // namespace notrix

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    return notrix::test::Registry::instance().runAll(filter);
}
