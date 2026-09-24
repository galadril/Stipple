// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstddef>

namespace notrix {
namespace net {
namespace sntp {

/// SNTP, RFC 4330: one 48-byte packet out, one back.
///
/// Pure, like DhcpMessage beside it - no sockets, no clock, no allocation -
/// because the parsing happens on untrusted network input and that is worth
/// being able to test exhaustively on a host.
///
/// **Why this exists at all.** The TC002 has no RTC battery, so its clock
/// reads 1970 on every cold boot. The stock application sets the time; NOTRIX
/// replaces that application, so without this the panel shows `__:__`
/// forever - which is what ClockApp correctly does when
/// `wallClockValid()` is false, and which looks exactly like a broken device.

/// Every SNTP packet is exactly this long. Anything else is not one.
constexpr std::size_t kPacketBytes = 48;

/// Seconds between 1900-01-01 (the NTP epoch) and 1970-01-01 (the Unix one).
///
/// Named rather than inlined because getting it wrong yields a time that is
/// plausible and seventy years out, which is the hardest kind of wrong to
/// notice.
constexpr std::uint32_t kEpochOffsetSeconds = 2208988800u;

/// The server's answer, as much of it as anybody here needs.
struct Reply {
    /// Seconds since the Unix epoch, UTC.
    std::uint32_t unixSeconds = 0;

    /// Leap indicator 3 means "clock not synchronised" - the server is
    /// telling us its own time is not to be trusted, and a device with no
    /// clock at all must not take it anyway.
    std::uint8_t leap = 0;

    /// 0 is reserved and means the server is unsynchronised or kiss-o'-death;
    /// 1..15 are real. Checked, because a stratum-0 reply carries an ASCII
    /// kiss code where the timestamp should be.
    std::uint8_t stratum = 0;

    std::uint8_t mode = 0;
    std::uint8_t versionNumber = 0;
};

/// Fill `out` with a client request. `out` must hold at least kPacketBytes.
///
/// Everything but the first byte stays zero, which is legal and deliberate:
/// a request carrying no timestamps cannot leak when the device thinks it is,
/// and this device's idea of "now" is wrong anyway.
void build(std::uint8_t* out) noexcept;

/// Parse a server reply. False means "do not set a clock from this".
///
/// Rejects: wrong length, a mode that is not server(4) or broadcast(5), an
/// unsynchronised leap indicator, stratum 0 or above 15, and a transmit
/// timestamp of zero or one that predates the Unix epoch. Each of those has
/// produced a wrong clock somewhere, and a wrong clock shown confidently is
/// worse than `__:__` (ADR 0013).
bool parse(const std::uint8_t* data, std::size_t length, Reply& out) noexcept;

}  // namespace sntp
}  // namespace net
}  // namespace notrix
