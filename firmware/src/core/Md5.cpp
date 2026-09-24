// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/core/Md5.h"

namespace notrix {
namespace {

/// The per-round shift amounts, from RFC 1321.
constexpr std::uint32_t kShift[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

/// floor(abs(sin(i + 1)) * 2^32), spelled out rather than computed: a table
/// somebody can check against the RFC beats four lines that need trusting.
constexpr std::uint32_t kSine[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

inline std::uint32_t rotateLeft(std::uint32_t value, std::uint32_t by) noexcept {
    return (value << by) | (value >> (32u - by));
}

inline std::uint32_t readLittle(const std::uint8_t* at) noexcept {
    return static_cast<std::uint32_t>(at[0]) |
           (static_cast<std::uint32_t>(at[1]) << 8) |
           (static_cast<std::uint32_t>(at[2]) << 16) |
           (static_cast<std::uint32_t>(at[3]) << 24);
}

}  // namespace

void Md5::reset() noexcept {
    state_[0] = 0x67452301;
    state_[1] = 0xefcdab89;
    state_[2] = 0x98badcfe;
    state_[3] = 0x10325476;
    bits_ = 0;
    buffered_ = 0;
    finished_ = false;
}

void Md5::processBlock(const std::uint8_t block[64]) noexcept {
    std::uint32_t word[16];
    for (std::size_t i = 0; i < 16; ++i) {
        word[i] = readLittle(&block[i * 4]);
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];

    for (std::uint32_t i = 0; i < 64; ++i) {
        std::uint32_t mixed;
        std::uint32_t index;
        if (i < 16) {
            mixed = (b & c) | (~b & d);
            index = i;
        } else if (i < 32) {
            mixed = (d & b) | (~d & c);
            index = (5u * i + 1u) % 16u;
        } else if (i < 48) {
            mixed = b ^ c ^ d;
            index = (3u * i + 5u) % 16u;
        } else {
            mixed = c ^ (b | ~d);
            index = (7u * i) % 16u;
        }

        const std::uint32_t rotated = a + mixed + kSine[i] + word[index];
        a = d;
        d = c;
        c = b;
        b = b + rotateLeft(rotated, kShift[i]);
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
}

void Md5::update(const void* data, std::size_t length) noexcept {
    if (finished_ || data == nullptr) {
        return;
    }
    const auto* at = static_cast<const std::uint8_t*>(data);
    bits_ += static_cast<std::uint64_t>(length) * 8u;

    // Top up a partial block first, then take whole blocks straight from the
    // caller's buffer. That second part is the point of doing this
    // incrementally at all: a multi-megabyte image is never copied.
    if (buffered_ > 0) {
        const std::size_t want = 64u - buffered_;
        const std::size_t take = length < want ? length : want;
        for (std::size_t i = 0; i < take; ++i) {
            buffer_[buffered_ + i] = at[i];
        }
        buffered_ += take;
        at += take;
        length -= take;
        if (buffered_ < 64u) {
            return;
        }
        processBlock(buffer_);
        buffered_ = 0;
    }

    while (length >= 64u) {
        processBlock(at);
        at += 64;
        length -= 64;
    }

    for (std::size_t i = 0; i < length; ++i) {
        buffer_[i] = at[i];
    }
    buffered_ = length;
}

void Md5::finish(std::uint8_t out[kDigestBytes]) noexcept {
    if (!finished_) {
        const std::uint64_t bits = bits_;

        // A single 0x80 byte, zeros, then the length in bits. The padding is
        // fed through update(), which keeps bits_ moving - so the length is
        // captured above, before any of it.
        const std::uint8_t one = 0x80;
        update(&one, 1);
        const std::uint8_t zero = 0;
        while (buffered_ != 56u) {
            update(&zero, 1);
        }

        std::uint8_t tail[8];
        for (std::size_t i = 0; i < 8; ++i) {
            tail[i] = static_cast<std::uint8_t>((bits >> (8u * i)) & 0xFFu);
        }
        update(tail, sizeof(tail));

        finished_ = true;
    }

    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t b = 0; b < 4; ++b) {
            out[i * 4 + b] = static_cast<std::uint8_t>((state_[i] >> (8u * b)) & 0xFFu);
        }
    }
}

std::string Md5::finishHex() {
    std::uint8_t digest[kDigestBytes];
    finish(digest);

    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(kDigestBytes * 2);
    for (const std::uint8_t byte : digest) {
        out.push_back(kDigits[(byte >> 4) & 0x0Fu]);
        out.push_back(kDigits[byte & 0x0Fu]);
    }
    return out;
}

std::string Md5::hex(const void* data, std::size_t length) {
    Md5 md5;
    md5.update(data, length);
    return md5.finishHex();
}

}  // namespace notrix
