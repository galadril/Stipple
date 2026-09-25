// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace stipple {
namespace base64 {

/// Standard base64, with padding.
///
/// Exists because the live view needs to put a 52x16 framebuffer into a JSON
/// response. PNG would be the obvious answer and is deliberately unavailable:
/// stipple_imageio is kept out of the device build on purpose (see
/// firmware/CMakeLists.txt), so encoding an image on the clock is not an option
/// and should not become one. Raw RGB plus base64 is 3328 characters for a
/// whole frame, which a browser turns into pixels in four lines of JavaScript.
///
std::string encode(const std::uint8_t* data, std::size_t length);

/// Decode, strictly, into `out`. Returns false for anything malformed.
///
/// This header used to say encoding only, on the grounds that an unused
/// decoder parsing untrusted input is a liability rather than a convenience.
/// That was right, and it stopped being true the moment HTTP Basic arrived
/// (ADR 0018): the credential on every request to this device is base64, and
/// it comes from whoever is on the network.
///
/// So it is strict rather than forgiving. Whitespace, missing padding, stray
/// characters and over-long input are all refused rather than skipped -
/// there is exactly one caller, it is an authentication path, and a decoder
/// that accepts near-misses is one that turns a malformed header into a
/// credential somebody did not send.
///
/// `maxBytes` bounds the output (§38). A header claiming a megabyte of
/// credential is not a credential.
bool decode(std::string_view text, std::string& out, std::size_t maxBytes);

/// Characters an encoding of `length` bytes will occupy.
constexpr std::size_t encodedSize(std::size_t length) noexcept {
    return ((length + 2u) / 3u) * 4u;
}

}  // namespace base64
}  // namespace stipple
