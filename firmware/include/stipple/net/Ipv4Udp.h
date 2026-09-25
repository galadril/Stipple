// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace stipple {
namespace net {
namespace ipv4 {

/// Putting an IP and UDP header on a datagram, and taking one off again.
///
/// **Why this exists at all**, given the kernel will happily do it: a client
/// asking for its first address has no address. A normal UDP socket has
/// nothing to put in the source field and no route to 255.255.255.255, so the
/// send fails or the reply never arrives. Every DHCP client ends up building
/// its own headers and handing them to a packet socket, and this is that.
///
/// It lives in the core, away from the socket, because the part most likely
/// to be silently wrong is the two checksums - and a wrong checksum is not an
/// error, it is a packet that is dropped by something else on the network,
/// which looks exactly like a server that did not answer.

inline constexpr std::size_t kIpHeaderBytes = 20;
inline constexpr std::size_t kUdpHeaderBytes = 8;
inline constexpr std::size_t kHeaderBytes = kIpHeaderBytes + kUdpHeaderBytes;
inline constexpr std::uint8_t kProtocolUdp = 17;

namespace detail {

inline std::uint16_t readU16(const std::uint8_t* at) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(at[0]) << 8) |
                                      static_cast<std::uint16_t>(at[1]));
}

inline void writeU16(std::uint8_t* at, std::uint16_t value) noexcept {
    at[0] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    at[1] = static_cast<std::uint8_t>(value & 0xFFu);
}

inline void writeU32(std::uint8_t* at, std::uint32_t value) noexcept {
    at[0] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    at[1] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    at[2] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    at[3] = static_cast<std::uint8_t>(value & 0xFFu);
}

inline std::uint32_t readU32(const std::uint8_t* at) noexcept {
    return (static_cast<std::uint32_t>(at[0]) << 24) |
           (static_cast<std::uint32_t>(at[1]) << 16) |
           (static_cast<std::uint32_t>(at[2]) << 8) | static_cast<std::uint32_t>(at[3]);
}

/// The one's-complement sum everything here is built on. Accumulated wide and
/// folded once at the end, which is both faster and harder to get wrong than
/// folding as it goes.
inline std::uint32_t sum(const std::uint8_t* data, std::size_t size) noexcept {
    std::uint32_t total = 0;
    std::size_t at = 0;
    for (; at + 1 < size; at += 2) {
        total += readU16(&data[at]);
    }
    if (at < size) {
        // An odd trailing byte is padded on the right, not the left.
        total += static_cast<std::uint32_t>(data[at]) << 8;
    }
    return total;
}

inline std::uint16_t fold(std::uint32_t total) noexcept {
    while ((total >> 16) != 0) {
        total = (total & 0xFFFFu) + (total >> 16);
    }
    return static_cast<std::uint16_t>(~total & 0xFFFFu);
}

}  // namespace detail

/// Write `payload` into `out` wrapped in an IP and a UDP header. Returns the
/// total size, or 0 if the buffer was too small.
///
/// `ident` only has to vary between packets. Nothing here fragments, so it is
/// never used to reassemble anything - it exists so a capture of two
/// identical retransmissions can be told apart.
inline std::size_t encapsulate(const std::uint8_t* payload, std::size_t payloadSize,
                               std::uint32_t source, std::uint32_t destination,
                               std::uint16_t sourcePort, std::uint16_t destinationPort,
                               std::uint16_t ident, std::uint8_t* out, std::size_t capacity) {
    const std::size_t total = kHeaderBytes + payloadSize;
    if (out == nullptr || payload == nullptr || total > capacity || total > 65535u) {
        return 0;
    }

    out[0] = 0x45;  // IPv4, 20-byte header
    out[1] = 0;     // no differentiated services
    detail::writeU16(&out[2], static_cast<std::uint16_t>(total));
    detail::writeU16(&out[4], ident);
    detail::writeU16(&out[6], 0);  // no flags, no fragment offset
    out[8] = 64;                   // ttl; this never leaves the link but 64 is what everything expects
    out[9] = kProtocolUdp;
    detail::writeU16(&out[10], 0);  // checksum, filled in below
    detail::writeU32(&out[12], source);
    detail::writeU32(&out[16], destination);
    detail::writeU16(&out[10], detail::fold(detail::sum(out, kIpHeaderBytes)));

    std::uint8_t* udp = &out[kIpHeaderBytes];
    detail::writeU16(&udp[0], sourcePort);
    detail::writeU16(&udp[2], destinationPort);
    detail::writeU16(&udp[4], static_cast<std::uint16_t>(kUdpHeaderBytes + payloadSize));
    detail::writeU16(&udp[6], 0);
    for (std::size_t i = 0; i < payloadSize; ++i) {
        udp[kUdpHeaderBytes + i] = payload[i];
    }

    // UDP's checksum covers a pseudo-header of the addresses, the protocol
    // and the length, as well as the datagram itself. It is optional over
    // IPv4 and plenty of clients send zero; it is computed here because
    // "optional" stops being true the moment anything on the path is doing
    // hardware offload or bridging, and debugging that from a clock is not a
    // afternoon anybody wants.
    std::uint32_t total32 = 0;
    total32 += static_cast<std::uint32_t>((source >> 16) & 0xFFFFu);
    total32 += static_cast<std::uint32_t>(source & 0xFFFFu);
    total32 += static_cast<std::uint32_t>((destination >> 16) & 0xFFFFu);
    total32 += static_cast<std::uint32_t>(destination & 0xFFFFu);
    total32 += kProtocolUdp;
    total32 += static_cast<std::uint32_t>(kUdpHeaderBytes + payloadSize);
    total32 += detail::sum(udp, kUdpHeaderBytes + payloadSize);

    const std::uint16_t checksum = detail::fold(total32);
    // Zero means "not computed", so a checksum that lands on zero is sent as
    // all ones - which is the same value in one's complement arithmetic.
    detail::writeU16(&udp[6], checksum == 0 ? 0xFFFFu : checksum);

    return total;
}

/// What came off the wire.
struct Datagram {
    std::uint32_t source = 0;
    std::uint32_t destination = 0;
    std::uint16_t sourcePort = 0;
    std::uint16_t destinationPort = 0;
    const std::uint8_t* payload = nullptr;
    std::size_t payloadSize = 0;
};

/// Unwrap a frame that starts at the IP header. Returns false for anything
/// that is not an intact, unfragmented UDP datagram.
///
/// A packet socket sees everything on the interface, so most of what arrives
/// here is not for us. Every rejection below is a normal event, not an error.
inline bool extract(const std::uint8_t* frame, std::size_t size, Datagram& out) {
    out = Datagram();

    if (frame == nullptr || size < kIpHeaderBytes) {
        return false;
    }
    if ((frame[0] >> 4) != 4) {
        return false;  // IPv6, or not IP at all
    }

    const std::size_t headerBytes = static_cast<std::size_t>(frame[0] & 0x0Fu) * 4u;
    if (headerBytes < kIpHeaderBytes || headerBytes > size) {
        return false;
    }
    if (frame[9] != kProtocolUdp) {
        return false;
    }

    // Fragments. Nothing this code cares about is ever fragmented, and
    // reassembling one would mean holding state for something that arrived
    // unasked-for from the network (§38).
    const std::uint16_t fragment = detail::readU16(&frame[6]);
    if ((fragment & 0x1FFFu) != 0 || (fragment & 0x2000u) != 0) {
        return false;
    }

    if (detail::fold(detail::sum(frame, headerBytes)) != 0) {
        return false;  // corrupt header
    }

    std::size_t totalLength = detail::readU16(&frame[2]);
    if (totalLength < headerBytes) {
        return false;
    }
    if (totalLength > size) {
        // Some drivers hand up a buffer with trailing padding and some
        // truncate; trusting the larger of the two is how a parser reads off
        // the end.
        return false;
    }

    const std::uint8_t* udp = &frame[headerBytes];
    const std::size_t udpBytes = totalLength - headerBytes;
    if (udpBytes < kUdpHeaderBytes) {
        return false;
    }

    const std::size_t udpLength = detail::readU16(&udp[4]);
    if (udpLength < kUdpHeaderBytes || udpLength > udpBytes) {
        return false;
    }

    const std::uint16_t checksum = detail::readU16(&udp[6]);
    if (checksum != 0) {
        std::uint32_t total32 = 0;
        const std::uint32_t source = detail::readU32(&frame[12]);
        const std::uint32_t destination = detail::readU32(&frame[16]);
        total32 += (source >> 16) & 0xFFFFu;
        total32 += source & 0xFFFFu;
        total32 += (destination >> 16) & 0xFFFFu;
        total32 += destination & 0xFFFFu;
        total32 += kProtocolUdp;
        total32 += static_cast<std::uint32_t>(udpLength);
        total32 += detail::sum(udp, udpLength);
        if (detail::fold(total32) != 0) {
            return false;
        }
    }

    out.source = detail::readU32(&frame[12]);
    out.destination = detail::readU32(&frame[16]);
    out.sourcePort = detail::readU16(&udp[0]);
    out.destinationPort = detail::readU16(&udp[2]);
    out.payload = udp + kUdpHeaderBytes;
    out.payloadSize = udpLength - kUdpHeaderBytes;
    return true;
}

}  // namespace ipv4
}  // namespace net
}  // namespace stipple
