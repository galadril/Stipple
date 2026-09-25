// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace stipple {

/// MD5, because the TC002 loader uses it and we do not get to choose.
///
/// **This is not here as a security primitive and must never be used as
/// one.** MD5 has been broken for collision resistance since 2004, and
/// anybody who can write to the file this validates can also write a
/// matching digest. It is here for exactly one reason: the device's own
/// firmware loader checks an MD5 over the payload before it will flash
/// anything, so an image STIPPLE prepares has to carry one the loader agrees
/// with. Detecting accidental corruption is the whole job.
///
/// Incremental, so a multi-megabyte image can be checked without a second
/// copy of it in RAM — which on a device with 36 MB is not a stylistic
/// preference.
class Md5 {
public:
    static constexpr std::size_t kDigestBytes = 16;

    Md5() noexcept { reset(); }

    void reset() noexcept;
    void update(const void* data, std::size_t length) noexcept;

    /// Finish and write the digest. The object must not be updated again
    /// without `reset()`.
    void finish(std::uint8_t out[kDigestBytes]) noexcept;

    /// Lowercase hex, for logs and comparisons a person will read.
    std::string finishHex();

    /// One-shot, for the common case.
    static std::string hex(const void* data, std::size_t length);

private:
    void processBlock(const std::uint8_t block[64]) noexcept;

    std::uint32_t state_[4] = {};
    std::uint64_t bits_ = 0;
    std::uint8_t buffer_[64] = {};
    std::size_t buffered_ = 0;
    bool finished_ = false;
};

}  // namespace stipple
