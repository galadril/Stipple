// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace notrix {

/// A single 24-bit colour. Deliberately a plain aggregate: the framebuffer holds
/// these by value and must stay trivially copyable for cheap bulk operations.
struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

constexpr bool operator==(Rgb lhs, Rgb rhs) noexcept {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

constexpr bool operator!=(Rgb lhs, Rgb rhs) noexcept {
    return !(lhs == rhs);
}

/// Build a colour from int components. Braced aggregate init rejects narrowing
/// from non-constant ints, so callers working with computed values use this.
constexpr Rgb rgb(int red, int green, int blue) noexcept {
    const auto clamp8 = [](int v) constexpr noexcept -> std::uint8_t {
        if (v < 0) {
            return 0;
        }
        if (v > 255) {
            return 255;
        }
        return static_cast<std::uint8_t>(v);
    };
    return Rgb{clamp8(red), clamp8(green), clamp8(blue)};
}

/// Pack to 0xRRGGBB, the form the web emulator and JSON APIs use.
constexpr std::uint32_t toPacked(Rgb c) noexcept {
    return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) |
           static_cast<std::uint32_t>(c.b);
}

constexpr Rgb fromPacked(std::uint32_t packed) noexcept {
    return Rgb{static_cast<std::uint8_t>((packed >> 16) & 0xFFu),
               static_cast<std::uint8_t>((packed >> 8) & 0xFFu),
               static_cast<std::uint8_t>(packed & 0xFFu)};
}

/// Scale a colour by 0..255. Used for brightness and fade transitions; integer
/// maths only, since the device has no FPU worth relying on in the render path.
constexpr Rgb scale(Rgb c, std::uint8_t factor) noexcept {
    const auto mul = [factor](std::uint8_t v) constexpr noexcept -> std::uint8_t {
        const unsigned product = static_cast<unsigned>(v) * static_cast<unsigned>(factor);
        return static_cast<std::uint8_t>(product / 255u);
    };
    return Rgb{mul(c.r), mul(c.g), mul(c.b)};
}

namespace colors {
inline constexpr Rgb kBlack{0, 0, 0};
inline constexpr Rgb kWhite{255, 255, 255};
inline constexpr Rgb kRed{255, 0, 0};
inline constexpr Rgb kGreen{0, 255, 0};
inline constexpr Rgb kBlue{0, 0, 255};
inline constexpr Rgb kYellow{255, 255, 0};
inline constexpr Rgb kCyan{0, 255, 255};
inline constexpr Rgb kMagenta{255, 0, 255};
inline constexpr Rgb kOrange{255, 128, 0};
}  // namespace colors

}  // namespace notrix
