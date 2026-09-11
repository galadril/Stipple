// SPDX-License-Identifier: GPL-3.0-or-later
#include "Golden.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "notrix/imageio/Png.h"

#ifndef NOTRIX_TESTDATA_DIR
#    error "NOTRIX_TESTDATA_DIR must be defined by the build (see firmware/tests/CMakeLists.txt)"
#endif

namespace notrix {
namespace test {
namespace {

namespace fs = std::filesystem;

constexpr int kSnapshotScale = 8;

bool envFlag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

/// Rewrite fixtures even when they already exist — an explicit "I changed the
/// renderer on purpose" switch.
bool updateRequested() {
    return envFlag("NOTRIX_UPDATE_GOLDEN");
}

/// In CI a fixture that is absent means someone forgot to commit it. Locally it
/// usually just means the test is new, so it is auto-created there instead.
bool strictMode() {
    return envFlag("NOTRIX_STRICT_GOLDEN");
}

fs::path testDataDir() {
    return fs::path(NOTRIX_TESTDATA_DIR);
}

std::vector<std::uint8_t> readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                     std::istreambuf_iterator<char>());
}

bool writeFixture(const fs::path& path, const Framebuffer& framebuffer) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(framebuffer.bytes()),
               static_cast<std::streamsize>(Framebuffer::kByteSize));
    return static_cast<bool>(file);
}

/// Rebuild a Framebuffer from raw fixture bytes so the expected image can also
/// be written out as a PNG for side-by-side comparison.
Framebuffer framebufferFromBytes(const std::vector<std::uint8_t>& bytes) {
    Framebuffer framebuffer;
    if (bytes.size() < Framebuffer::kByteSize) {
        return framebuffer;
    }
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(Framebuffer::kWidth) +
                 static_cast<std::size_t>(x)) *
                3u;
            framebuffer.set(x, y, Rgb{bytes[offset], bytes[offset + 1u], bytes[offset + 2u]});
        }
    }
    return framebuffer;
}

int countDifferingPixels(const Framebuffer& actual, const std::vector<std::uint8_t>& expected) {
    const std::uint8_t* actualBytes = actual.bytes();
    int differing = 0;
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(Framebuffer::kPixelCount); ++pixel) {
        const std::size_t offset = pixel * 3u;
        if (actualBytes[offset] != expected[offset] ||
            actualBytes[offset + 1u] != expected[offset + 1u] ||
            actualBytes[offset + 2u] != expected[offset + 2u]) {
            ++differing;
        }
    }
    return differing;
}

}  // namespace

void checkGolden(const char* name, const Framebuffer& framebuffer, const char* file, int line) {
    const fs::path fixture = testDataDir() / (std::string(name) + ".rgb");
    const std::vector<std::uint8_t> expected = readFile(fixture);

    const bool missing = expected.size() < Framebuffer::kByteSize;

    if (missing) {
        if (strictMode()) {
            Registry::instance().fail(file, line,
                                      "golden fixture missing in strict mode: " + fixture.string() +
                                          "\n           it was most likely never committed");
            return;
        }

        if (!writeFixture(fixture, framebuffer)) {
            Registry::instance().fail(file, line,
                                      "could not write golden fixture: " + fixture.string());
            return;
        }

        // Loud on purpose: a fixture created from unreviewed output records
        // whatever the renderer did, bug included.
        const fs::path preview = testDataDir() / (std::string(name) + ".png");
        imageio::writePng(preview.string(), framebuffer, kSnapshotScale);
        std::cout << "        [golden] CREATED " << fixture.string()
                  << "\n                 review " << preview.string()
                  << " before committing it\n";
        return;
    }

    const int differing = countDifferingPixels(framebuffer, expected);
    if (differing == 0) {
        return;
    }

    if (updateRequested()) {
        if (writeFixture(fixture, framebuffer)) {
            const fs::path preview = testDataDir() / (std::string(name) + ".png");
            imageio::writePng(preview.string(), framebuffer, kSnapshotScale);
            std::cout << "        [golden] UPDATED " << fixture.string() << " (" << differing
                      << " pixels changed)\n";
            return;
        }
    }

    const fs::path failedDir = testDataDir() / "_failed";
    std::error_code ec;
    fs::create_directories(failedDir, ec);

    const fs::path actualPng = failedDir / (std::string(name) + ".actual.png");
    const fs::path expectedPng = failedDir / (std::string(name) + ".expected.png");

    imageio::writePng(actualPng.string(), framebuffer, kSnapshotScale);
    imageio::writePng(expectedPng.string(), framebufferFromBytes(expected), kSnapshotScale);

    Registry::instance().fail(file, line,
                              "golden mismatch for '" + std::string(name) + "': " +
                                  std::to_string(differing) + " of " +
                                  std::to_string(Framebuffer::kPixelCount) +
                                  " pixels differ\n           actual:   " + actualPng.string() +
                                  "\n           expected: " + expectedPng.string());
}

}  // namespace test
}  // namespace notrix
