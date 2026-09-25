// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/core/Rgb.h"

namespace stipple {
namespace {

/// -1 for anything that is not a hex digit, so callers test one value rather
/// than bracketing three ranges at every use.
constexpr int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

constexpr char kHexDigits[] = "0123456789ABCDEF";

}  // namespace

bool parseHexColor(std::string_view text, Rgb& out) noexcept {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        return false;
    }

    int channels[3] = {0, 0, 0};
    for (std::size_t i = 0; i < 3; ++i) {
        const int high = hexValue(text[i * 2u]);
        const int low = hexValue(text[i * 2u + 1u]);
        if (high < 0 || low < 0) {
            return false;
        }
        channels[i] = high * 16 + low;
    }

    // Written only once every channel has been validated, so a rejected value
    // cannot leave the caller holding a half-parsed colour.
    out = rgb(channels[0], channels[1], channels[2]);
    return true;
}

void formatHexColor(Rgb color, char out[8]) noexcept {
    const std::uint8_t channels[3] = {color.r, color.g, color.b};
    out[0] = '#';
    for (std::size_t i = 0; i < 3; ++i) {
        out[1 + i * 2u] = kHexDigits[(channels[i] >> 4) & 0x0Fu];
        out[2 + i * 2u] = kHexDigits[channels[i] & 0x0Fu];
    }
    out[7] = '\0';
}

}  // namespace stipple
