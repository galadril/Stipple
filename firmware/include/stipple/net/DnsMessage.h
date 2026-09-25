// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace stipple {
namespace net {
namespace dns {

/// DNS over UDP, RFC 1035, enough of it to turn a hostname into an address.
///
/// Pure, like DhcpMessage and SntpMessage beside it - no sockets, no clock,
/// no allocation - because every byte parsed here came from somewhere else
/// and the parsing is the dangerous part.
///
/// **Why write one at all**, when the C library has `getaddrinfo`: because on
/// this device that function does not work. The resolver reads only
/// `/etc/resolv.conf`, that file is on the read-only rootfs, and it names
/// 114.114.114.114 first - a service that does not answer outside China, so
/// lookups stall rather than failing over. Measured: resolving a hostname
/// never returned, and the clock sat at 1970 until it was pointed at a
/// numeric address instead.
///
/// Asking the nameserver the DHCP lease already gave us sidesteps that file
/// completely.

/// A query and a reply both fit comfortably; anything larger than this is
/// either a different protocol or an attempt at something.
constexpr std::size_t kMaxMessageBytes = 512;

/// RFC 1035 caps a name at 255 bytes and a label at 63.
constexpr std::size_t kMaxNameBytes = 255;
constexpr std::size_t kMaxLabelBytes = 63;

enum class Result : std::uint8_t {
    Ok,
    /// Shorter than a header, or a field runs off the end.
    Malformed,
    /// The reply is for a different question than the one asked.
    WrongId,
    /// The server said the name does not exist.
    NoSuchName,
    /// The server answered, but with no address of the kind asked for -
    /// a name that exists with only IPv6 records, for instance.
    NoAddress,
    /// The server set the truncation bit. A larger answer would need TCP,
    /// which this does not do.
    Truncated,
    /// The server reported a failure of its own.
    ServerFailure,
};

const char* describe(Result result) noexcept;

/// Write a query for the A record of `hostname` into `out`.
///
/// Returns the number of bytes written, or 0 if the name cannot be encoded -
/// too long, an empty label, a label over 63 bytes. Rejecting here means the
/// socket layer never sends a malformed question.
///
/// `id` should be unpredictable rather than sequential: a resolver that asks
/// with 1, 2, 3 is trivially easy for an off-path attacker to answer before
/// the real server does.
std::size_t build(std::string_view hostname, std::uint16_t id, std::uint8_t* out,
                  std::size_t outSize) noexcept;

/// Read an A record out of a reply, following CNAMEs.
///
/// `address` is set to the address in network byte order on Ok.
///
/// **The compression pointers are the dangerous part.** A name may end with a
/// pointer back into the message, and nothing stops a hostile reply pointing
/// a name at itself - so every jump is counted and the total is capped. That
/// is not theoretical tidiness; it is the difference between a failed lookup
/// and a device that stops rendering.
Result parse(const std::uint8_t* data, std::size_t length, std::uint16_t id,
             std::uint32_t& address) noexcept;

}  // namespace dns
}  // namespace net
}  // namespace stipple
