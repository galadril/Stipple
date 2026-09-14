// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace notrix {

/// Standard CRC-32 (IEEE 802.3, polynomial 0xEDB88320).
///
/// Used for configuration integrity (blueprint §21) and PNG chunks. This is an
/// error-detection code, not a security primitive: it catches a truncated or
/// corrupted flash write, and is not meant to resist deliberate tampering.
/// Signed update manifests (§28) are a separate mechanism.
std::uint32_t crc32(const void* data, std::size_t length) noexcept;

std::uint32_t crc32(std::string_view text) noexcept;

/// Lower-case, zero-padded, eight hex digits.
void crc32ToHex(std::uint32_t value, char out[9]) noexcept;

}  // namespace notrix
