// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace notrix {
namespace base64 {

/// Standard base64, with padding.
///
/// Exists because the live view needs to put a 52x16 framebuffer into a JSON
/// response. PNG would be the obvious answer and is deliberately unavailable:
/// notrix_imageio is kept out of the device build on purpose (see
/// firmware/CMakeLists.txt), so encoding an image on the clock is not an option
/// and should not become one. Raw RGB plus base64 is 3328 characters for a
/// whole frame, which a browser turns into pixels in four lines of JavaScript.
///
/// Encoding only. Nothing on the device decodes base64 yet, and an unused
/// decoder parsing untrusted input is a liability rather than a convenience.
std::string encode(const std::uint8_t* data, std::size_t length);

/// Characters an encoding of `length` bytes will occupy.
constexpr std::size_t encodedSize(std::size_t length) noexcept {
    return ((length + 2u) / 3u) * 4u;
}

}  // namespace base64
}  // namespace notrix
