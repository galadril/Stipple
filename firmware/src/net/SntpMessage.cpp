// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/net/SntpMessage.h"

namespace stipple {
namespace net {
namespace sntp {
namespace {

/// Byte offsets into the packet, from RFC 4330 figure 1.
constexpr std::size_t kFlags = 0;
constexpr std::size_t kStratum = 1;
constexpr std::size_t kTransmitTimestamp = 40;

constexpr std::uint8_t kModeClient = 3;
constexpr std::uint8_t kModeServer = 4;
constexpr std::uint8_t kModeBroadcast = 5;
constexpr std::uint8_t kVersion4 = 4;

constexpr std::uint8_t kLeapUnsynchronised = 3;
constexpr std::uint8_t kMaxStratum = 15;

std::uint32_t readBe32(const std::uint8_t* at) noexcept {
    return (static_cast<std::uint32_t>(at[0]) << 24) |
           (static_cast<std::uint32_t>(at[1]) << 16) |
           (static_cast<std::uint32_t>(at[2]) << 8) |
           static_cast<std::uint32_t>(at[3]);
}

}  // namespace

void build(std::uint8_t* out) noexcept {
    for (std::size_t i = 0; i < kPacketBytes; ++i) {
        out[i] = 0;
    }
    // LI = 0, VN = 4, Mode = 3 (client).
    out[kFlags] = static_cast<std::uint8_t>((kVersion4 << 3) | kModeClient);
}

bool parse(const std::uint8_t* data, std::size_t length, Reply& out) noexcept {
    if (data == nullptr || length < kPacketBytes) {
        return false;
    }

    const std::uint8_t flags = data[kFlags];
    out.leap = static_cast<std::uint8_t>((flags >> 6) & 0x3u);
    out.versionNumber = static_cast<std::uint8_t>((flags >> 3) & 0x7u);
    out.mode = static_cast<std::uint8_t>(flags & 0x7u);
    out.stratum = data[kStratum];

    if (out.mode != kModeServer && out.mode != kModeBroadcast) {
        return false;
    }
    if (out.leap == kLeapUnsynchronised) {
        return false;
    }
    // Stratum 0 is a kiss-o'-death: the "timestamp" is four ASCII bytes of
    // reason code, and reading it as a time gives a date in 2036.
    if (out.stratum == 0 || out.stratum > kMaxStratum) {
        return false;
    }

    const std::uint32_t ntpSeconds = readBe32(data + kTransmitTimestamp);
    if (ntpSeconds == 0) {
        return false;
    }
    // Before 1970 cannot be a real answer for this device, and would wrap.
    if (ntpSeconds < kEpochOffsetSeconds) {
        return false;
    }

    out.unixSeconds = ntpSeconds - kEpochOffsetSeconds;
    return true;
}

}  // namespace sntp
}  // namespace net
}  // namespace stipple
