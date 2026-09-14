// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/core/Checksum.h"

#include <array>

namespace notrix {
namespace {

const std::array<std::uint32_t, 256>& table() {
    static const std::array<std::uint32_t, 256> computed = [] {
        std::array<std::uint32_t, 256> entries{};
        for (std::uint32_t n = 0; n < 256u; ++n) {
            std::uint32_t value = n;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) != 0u ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            }
            entries[n] = value;
        }
        return entries;
    }();
    return computed;
}

}  // namespace

std::uint32_t crc32(const void* data, std::size_t length) noexcept {
    const std::array<std::uint32_t, 256>& lookup = table();
    const auto* bytes = static_cast<const std::uint8_t*>(data);

    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc = lookup[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t crc32(std::string_view text) noexcept {
    return crc32(text.data(), text.size());
}

void crc32ToHex(std::uint32_t value, char out[9]) noexcept {
    static const char kDigits[] = "0123456789abcdef";
    for (int i = 0; i < 8; ++i) {
        const int shift = (7 - i) * 4;
        out[i] = kDigits[(value >> shift) & 0xFu];
    }
    out[8] = '\0';
}

}  // namespace notrix
