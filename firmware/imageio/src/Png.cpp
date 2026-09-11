// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/imageio/Png.h"

#include <array>
#include <cstddef>
#include <fstream>

namespace notrix {
namespace imageio {
namespace {

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256u; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[n] = c;
        }
        return t;
    }();
    return table;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t length) {
    const std::array<std::uint32_t, 256>& table = crcTable();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t length) {
    constexpr std::uint32_t kModulus = 65521u;
    std::uint32_t a = 1u;
    std::uint32_t b = 0u;
    for (std::size_t i = 0; i < length; ++i) {
        a = (a + data[i]) % kModulus;
        b = (b + a) % kModulus;
    }
    return (b << 16) | a;
}

void pushBigEndian32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void writeChunk(std::vector<std::uint8_t>& out,
                const char (&type)[5],
                const std::vector<std::uint8_t>& data) {
    pushBigEndian32(out, static_cast<std::uint32_t>(data.size()));

    const std::size_t crcStart = out.size();
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(type[i]));
    }
    out.insert(out.end(), data.begin(), data.end());

    const std::uint32_t crc = crc32(out.data() + crcStart, out.size() - crcStart);
    pushBigEndian32(out, crc);
}

/// Wrap raw bytes in a zlib stream made of stored deflate blocks.
std::vector<std::uint8_t> zlibStored(const std::vector<std::uint8_t>& raw) {
    constexpr std::size_t kMaxBlock = 65535u;

    std::vector<std::uint8_t> out;
    out.reserve(raw.size() + (raw.size() / kMaxBlock + 1u) * 5u + 6u);

    // CMF=0x78 (deflate, 32K window), FLG=0x01 -> (0x7801 % 31) == 0.
    out.push_back(0x78u);
    out.push_back(0x01u);

    std::size_t offset = 0;
    for (;;) {
        const std::size_t remaining = raw.size() - offset;
        const std::size_t blockLength = remaining < kMaxBlock ? remaining : kMaxBlock;
        const bool isFinal = (offset + blockLength) >= raw.size();

        out.push_back(static_cast<std::uint8_t>(isFinal ? 1u : 0u));

        const std::uint16_t len = static_cast<std::uint16_t>(blockLength);
        const std::uint16_t nlen = static_cast<std::uint16_t>(~len);
        out.push_back(static_cast<std::uint8_t>(len & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>(nlen & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFFu));

        const auto begin = raw.begin() + static_cast<std::ptrdiff_t>(offset);
        out.insert(out.end(), begin, begin + static_cast<std::ptrdiff_t>(blockLength));

        offset += blockLength;
        if (isFinal) {
            break;
        }
    }

    pushBigEndian32(out, adler32(raw.data(), raw.size()));
    return out;
}

}  // namespace

std::vector<std::uint8_t> encodePng(const Rgb* pixels, int width, int height, int scale) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return {};
    }
    if (scale < 1) {
        scale = 1;
    }

    const int outWidth = width * scale;
    const int outHeight = height * scale;

    // Scanlines, each prefixed with filter type 0 (None).
    std::vector<std::uint8_t> raw;
    raw.reserve((static_cast<std::size_t>(outWidth) * 3u + 1u) * static_cast<std::size_t>(outHeight));

    for (int y = 0; y < outHeight; ++y) {
        raw.push_back(0u);
        const std::size_t sourceRow = static_cast<std::size_t>(y / scale) * static_cast<std::size_t>(width);
        for (int x = 0; x < outWidth; ++x) {
            const Rgb color = pixels[sourceRow + static_cast<std::size_t>(x / scale)];
            raw.push_back(color.r);
            raw.push_back(color.g);
            raw.push_back(color.b);
        }
    }

    std::vector<std::uint8_t> png{137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};

    std::vector<std::uint8_t> ihdr;
    pushBigEndian32(ihdr, static_cast<std::uint32_t>(outWidth));
    pushBigEndian32(ihdr, static_cast<std::uint32_t>(outHeight));
    ihdr.push_back(8u);  // bit depth
    ihdr.push_back(2u);  // colour type: truecolour RGB
    ihdr.push_back(0u);  // compression: deflate
    ihdr.push_back(0u);  // filter method: adaptive
    ihdr.push_back(0u);  // interlace: none
    writeChunk(png, "IHDR", ihdr);

    writeChunk(png, "IDAT", zlibStored(raw));
    writeChunk(png, "IEND", std::vector<std::uint8_t>{});

    return png;
}

std::vector<std::uint8_t> encodePng(const Framebuffer& framebuffer, int scale) {
    return encodePng(framebuffer.data(), Framebuffer::kWidth, Framebuffer::kHeight, scale);
}

bool writePng(const std::string& path, const Framebuffer& framebuffer, int scale) {
    const std::vector<std::uint8_t> png = encodePng(framebuffer, scale);
    if (png.empty()) {
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(file);
}

}  // namespace imageio
}  // namespace notrix
