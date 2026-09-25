// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace stipple {
namespace text {

/// U+FFFD REPLACEMENT CHARACTER. Substituted for anything malformed, so text
/// always renders something rather than vanishing or desynchronising.
inline constexpr char32_t kReplacementChar = 0xFFFDu;

struct DecodedChar {
    char32_t codepoint = kReplacementChar;
    std::size_t size = 1;  ///< bytes consumed; never 0, so loops always advance
};

/// Decode one UTF-8 sequence starting at `offset`.
///
/// This will eventually see text straight off the network (blueprint §19, §23),
/// so it is strict by construction: overlong encodings, UTF-16 surrogate halves
/// and anything above U+10FFFF are all rejected. A rejected sequence consumes
/// exactly one byte and yields the replacement character, which guarantees
/// forward progress and keeps a malformed byte from swallowing the rest of a
/// string.
inline DecodedChar decodeUtf8(std::string_view text, std::size_t offset) noexcept {
    if (offset >= text.size()) {
        return DecodedChar{kReplacementChar, 1};
    }

    const auto byteAt = [&text](std::size_t index) noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(text[index]));
    };

    const std::uint32_t lead = byteAt(offset);
    const std::size_t available = text.size() - offset;

    // Continuation bytes must be 10xxxxxx.
    const auto isContinuation = [&byteAt](std::size_t index) noexcept -> bool {
        return (byteAt(index) & 0xC0u) == 0x80u;
    };

    if (lead < 0x80u) {
        return DecodedChar{static_cast<char32_t>(lead), 1};
    }

    if ((lead & 0xE0u) == 0xC0u) {
        if (available < 2 || !isContinuation(offset + 1)) {
            return DecodedChar{kReplacementChar, 1};
        }
        const std::uint32_t value = ((lead & 0x1Fu) << 6) | (byteAt(offset + 1) & 0x3Fu);
        if (value < 0x80u) {
            return DecodedChar{kReplacementChar, 1};  // overlong
        }
        return DecodedChar{static_cast<char32_t>(value), 2};
    }

    if ((lead & 0xF0u) == 0xE0u) {
        if (available < 3 || !isContinuation(offset + 1) || !isContinuation(offset + 2)) {
            return DecodedChar{kReplacementChar, 1};
        }
        const std::uint32_t value = ((lead & 0x0Fu) << 12) | ((byteAt(offset + 1) & 0x3Fu) << 6) |
                                    (byteAt(offset + 2) & 0x3Fu);
        if (value < 0x800u) {
            return DecodedChar{kReplacementChar, 1};  // overlong
        }
        if (value >= 0xD800u && value <= 0xDFFFu) {
            return DecodedChar{kReplacementChar, 1};  // lone surrogate
        }
        return DecodedChar{static_cast<char32_t>(value), 3};
    }

    if ((lead & 0xF8u) == 0xF0u) {
        if (available < 4 || !isContinuation(offset + 1) || !isContinuation(offset + 2) ||
            !isContinuation(offset + 3)) {
            return DecodedChar{kReplacementChar, 1};
        }
        const std::uint32_t value = ((lead & 0x07u) << 18) | ((byteAt(offset + 1) & 0x3Fu) << 12) |
                                    ((byteAt(offset + 2) & 0x3Fu) << 6) |
                                    (byteAt(offset + 3) & 0x3Fu);
        if (value < 0x10000u) {
            return DecodedChar{kReplacementChar, 1};  // overlong
        }
        if (value > 0x10FFFFu) {
            return DecodedChar{kReplacementChar, 1};  // outside Unicode
        }
        return DecodedChar{static_cast<char32_t>(value), 4};
    }

    // Stray continuation byte, or a 5/6-byte form that UTF-8 no longer permits.
    return DecodedChar{kReplacementChar, 1};
}

/// Number of codepoints, counting each malformed byte as one.
inline std::size_t countCodepoints(std::string_view text) noexcept {
    std::size_t count = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        offset += decodeUtf8(text, offset).size;
        ++count;
    }
    return count;
}

/// Visit each codepoint in order. `fn` takes a char32_t.
template <typename Fn>
void forEachCodepoint(std::string_view text, Fn&& fn) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const DecodedChar decoded = decodeUtf8(text, offset);
        fn(decoded.codepoint);
        offset += decoded.size;
    }
}

}  // namespace text
}  // namespace stipple
