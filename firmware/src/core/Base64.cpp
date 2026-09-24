// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/core/Base64.h"

namespace notrix {
namespace base64 {
namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::string encode(const std::uint8_t* data, std::size_t length) {
    std::string out;
    if (data == nullptr || length == 0) {
        return out;
    }

    // Reserved exactly, so encoding a frame every poll does not reallocate.
    out.reserve(encodedSize(length));

    std::size_t i = 0;
    for (; i + 3 <= length; i += 3) {
        const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16) |
                                     (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                     static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3Fu]);
        out.push_back(kAlphabet[triple & 0x3Fu]);
    }

    const std::size_t remaining = length - i;
    if (remaining == 1) {
        const std::uint32_t triple = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16) |
                                     (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3Fu]);
        out.push_back('=');
    }

    return out;
}

namespace {

/// -1 for anything that is not a base64 digit. Deliberately a table rather
/// than a chain of range checks: the chain is where a stray character slips
/// through as a valid value.
constexpr signed char kReverse[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

}  // namespace

bool decode(std::string_view text, std::string& out, std::size_t maxBytes) {
    out.clear();

    // Length is checked before a single character is looked at, so an
    // oversized header costs a comparison rather than a decode.
    if (text.size() % 4u != 0u) {
        return false;
    }
    const std::size_t groups = text.size() / 4u;
    if (groups * 3u > maxBytes) {
        return false;
    }
    if (text.empty()) {
        return true;
    }

    // Padding is only ever the last one or two characters. Anywhere else it
    // is a malformed input pretending to be a short one.
    std::size_t padding = 0;
    if (text[text.size() - 1] == '=') {
        ++padding;
        if (text.size() >= 2 && text[text.size() - 2] == '=') {
            ++padding;
        }
    }

    out.reserve(groups * 3u - padding);

    for (std::size_t group = 0; group < groups; ++group) {
        const std::size_t at = group * 4u;
        const bool last = group + 1u == groups;

        signed char digit[4];
        for (std::size_t i = 0; i < 4; ++i) {
            const auto c = static_cast<unsigned char>(text[at + i]);
            if (c == '=') {
                // Only on the last group, and only in the last two places.
                if (!last || i < 2) {
                    return false;
                }
                digit[i] = 0;
                continue;
            }
            digit[i] = kReverse[c];
            if (digit[i] < 0) {
                return false;
            }
        }

        const std::uint32_t bits = (static_cast<std::uint32_t>(digit[0]) << 18) |
                                   (static_cast<std::uint32_t>(digit[1]) << 12) |
                                   (static_cast<std::uint32_t>(digit[2]) << 6) |
                                   static_cast<std::uint32_t>(digit[3]);

        const std::size_t produce = last ? 3u - padding : 3u;
        if (produce >= 1) {
            out.push_back(static_cast<char>((bits >> 16) & 0xFFu));
        }
        if (produce >= 2) {
            out.push_back(static_cast<char>((bits >> 8) & 0xFFu));
        }
        if (produce >= 3) {
            out.push_back(static_cast<char>(bits & 0xFFu));
        }
    }
    return true;
}

}  // namespace base64
}  // namespace notrix
