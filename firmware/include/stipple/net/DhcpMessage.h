// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace stipple {
namespace net {
namespace dhcp {

/// Building and reading DHCP packets, and nothing else.
///
/// This lives in the core rather than in the TC002 adapter because it is the
/// same on every machine that has ever spoken DHCP, and because it is the part
/// most worth testing: a hand-written binary protocol is where the bugs are,
/// and a bug here is a device that cannot get on the network - which is the
/// one bug that cannot be diagnosed remotely.
///
/// **Why STIPPLE has to speak this at all.** There is no DHCP client on the
/// TC002. Not in /bin, not as a busybox applet, because that busybox has no
/// applets. The vendor application obtains the lease in-process, which is why
/// every STIPPLE session so far has had an address: each one began by stopping
/// a program that already had one. Nothing renews it, so a STIPPLE device left
/// running long enough loses its network. See
/// docs/research/tc002-platform-findings.md.
///
/// Addresses are held as a host-order uint32 with the first octet in the top
/// byte - 192.168.1.5 is 0xC0A80105. That keeps byte-order conversion at the
/// two edges where bytes are actually read and written, instead of scattering
/// htonl through code that is supposed to be portable and testable.

enum class MessageType : std::uint8_t {
    kNone = 0,
    kDiscover = 1,
    kOffer = 2,
    kRequest = 3,
    kDecline = 4,
    kAck = 5,
    kNak = 6,
    kRelease = 7,
};

/// 236 bytes of BOOTP plus the four-byte cookie. Anything shorter is not a
/// DHCP packet however friendly it looks.
inline constexpr std::size_t kFixedBytes = 240;

/// Bound, because this arrives from whatever answered a broadcast (§38). The
/// RFC's own floor for what a client must accept is 576; a kilobyte is
/// generous for a reply carrying an address and a handful of options, and
/// anything larger is not a server being helpful.
inline constexpr std::size_t kMaxMessageBytes = 1024;

inline constexpr std::uint32_t kMagicCookie = 0x63825363u;
inline constexpr std::size_t kHardwareBytes = 6;

/// Ports, from the client's point of view.
inline constexpr std::uint16_t kClientPort = 68;
inline constexpr std::uint16_t kServerPort = 67;

/// A lease that never expires. Servers do issue these, and the client must
/// not treat one as "expired already", which is what a naive comparison does.
inline constexpr std::uint32_t kInfiniteLease = 0xFFFFFFFFu;

inline constexpr std::uint32_t kBroadcastAddress = 0xFFFFFFFFu;

/// What a server granted. Everything but the address is optional: a reply
/// without a mask is unusual rather than invalid, and guessing one from the
/// address class is 1990s behaviour that gets /8 wrong on every home network.
struct Lease {
    std::uint32_t address = 0;
    std::uint32_t mask = 0;
    std::uint32_t router = 0;
    std::uint32_t dns = 0;

    /// Which server said so. Needed to renew, and needed to tell two servers
    /// apart when both answer.
    std::uint32_t server = 0;

    std::uint32_t leaseSeconds = 0;

    /// T1 and T2. Defaulted to half and seven-eighths of the lease when the
    /// server does not say, which is what the RFC asks for.
    std::uint32_t renewSeconds = 0;
    std::uint32_t rebindSeconds = 0;

    bool valid() const noexcept { return address != 0 && leaseSeconds != 0; }
};

/// A parsed reply. `valid` false means the bytes were not a DHCP message we
/// can use, and every other field is meaningless.
struct Reply {
    bool valid = false;
    MessageType type = MessageType::kNone;
    std::uint32_t xid = 0;
    Lease lease;
};

/// What goes into a packet we send.
struct Request {
    std::uint32_t xid = 0;

    /// Seconds since the client started trying. Servers use it to decide
    /// whether to answer a client that is clearly struggling.
    std::uint16_t secs = 0;

    std::uint8_t mac[kHardwareBytes] = {0, 0, 0, 0, 0, 0};

    /// Option 50. Set in a REQUEST that follows an OFFER; left zero when
    /// renewing, where the address goes in ciaddr instead.
    std::uint32_t requestedAddress = 0;

    /// Option 54. Set when answering one specific server's offer.
    std::uint32_t serverAddress = 0;

    /// The address already held, for renewal.
    std::uint32_t clientAddress = 0;

    /// Option 12. Worth having: it is what shows up in a router's client
    /// list, and a row saying "stipple" beats a row saying nothing.
    std::string_view hostname;

    /// Ask the server to broadcast its reply. Required while the interface
    /// has no address, because a unicast to an address the kernel does not
    /// own yet is dropped before this code ever sees it.
    bool broadcast = true;
};

namespace detail {

inline std::uint32_t readU32(const std::uint8_t* at) noexcept {
    return (static_cast<std::uint32_t>(at[0]) << 24) |
           (static_cast<std::uint32_t>(at[1]) << 16) |
           (static_cast<std::uint32_t>(at[2]) << 8) | static_cast<std::uint32_t>(at[3]);
}

inline void writeU32(std::uint8_t* at, std::uint32_t value) noexcept {
    at[0] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    at[1] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    at[2] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    at[3] = static_cast<std::uint8_t>(value & 0xFFu);
}

}  // namespace detail

/// Dotted quad, for the log and the web UI.
inline std::string formatIpv4(std::uint32_t address) {
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u",
                  static_cast<unsigned>((address >> 24) & 0xFFu),
                  static_cast<unsigned>((address >> 16) & 0xFFu),
                  static_cast<unsigned>((address >> 8) & 0xFFu),
                  static_cast<unsigned>(address & 0xFFu));
    return std::string(text);
}

/// The other direction. Returns false on anything that is not four octets in
/// range - this parses what a person typed, so "192.168.1" and
/// "192.168.1.999" both have to be refused rather than quietly rounded.
inline bool parseIpv4(std::string_view text, std::uint32_t& out) noexcept {
    std::uint32_t value = 0;
    std::size_t at = 0;
    for (int octet = 0; octet < 4; ++octet) {
        if (at >= text.size() || text[at] < '0' || text[at] > '9') {
            return false;
        }
        std::uint32_t part = 0;
        std::size_t digits = 0;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            part = part * 10u + static_cast<std::uint32_t>(text[at] - '0');
            ++at;
            if (++digits > 3 || part > 255u) {
                return false;
            }
        }
        value = (value << 8) | part;
        if (octet < 3) {
            if (at >= text.size() || text[at] != '.') {
                return false;
            }
            ++at;
        }
    }
    if (at != text.size()) {
        return false;
    }
    out = value;
    return true;
}

/// How many leading one-bits a mask has, for showing 192.168.1.5/24.
inline int prefixLength(std::uint32_t mask) noexcept {
    int bits = 0;
    while (bits < 32 && (mask & (0x80000000u >> bits)) != 0u) {
        ++bits;
    }
    return bits;
}

/// Write a client packet into `out`. Returns the number of bytes, or 0 if the
/// buffer was too small.
///
/// Padded to the BOOTP minimum: some servers and rather more relays quietly
/// drop anything shorter, and the padding costs nothing.
inline std::size_t build(MessageType type, const Request& request, std::uint8_t* out,
                         std::size_t capacity) {
    constexpr std::size_t kMinimumSend = 300;
    if (out == nullptr || capacity < kMinimumSend) {
        return 0;
    }

    for (std::size_t i = 0; i < kMinimumSend; ++i) {
        out[i] = 0;
    }

    out[0] = 1;  // BOOTREQUEST
    out[1] = 1;  // ethernet
    out[2] = static_cast<std::uint8_t>(kHardwareBytes);
    out[3] = 0;  // hops
    detail::writeU32(&out[4], request.xid);
    out[8] = static_cast<std::uint8_t>((request.secs >> 8) & 0xFFu);
    out[9] = static_cast<std::uint8_t>(request.secs & 0xFFu);
    if (request.broadcast) {
        out[10] = 0x80;
    }
    detail::writeU32(&out[12], request.clientAddress);  // ciaddr
    for (std::size_t i = 0; i < kHardwareBytes; ++i) {
        out[28 + i] = request.mac[i];
    }
    detail::writeU32(&out[236], kMagicCookie);

    std::size_t at = kFixedBytes;

    const auto put = [&](std::uint8_t code, const std::uint8_t* value, std::size_t length) {
        if (at + 2 + length > capacity) {
            return false;
        }
        out[at++] = code;
        out[at++] = static_cast<std::uint8_t>(length);
        for (std::size_t i = 0; i < length; ++i) {
            out[at++] = value[i];
        }
        return true;
    };

    const std::uint8_t messageType = static_cast<std::uint8_t>(type);
    if (!put(53, &messageType, 1)) {
        return 0;
    }

    // Option 61, client identifier: hardware type then the MAC. Without it a
    // server is free to key the lease on something else, and the address
    // changes for no reason anybody can see.
    std::uint8_t identifier[1 + kHardwareBytes];
    identifier[0] = 1;
    for (std::size_t i = 0; i < kHardwareBytes; ++i) {
        identifier[1 + i] = request.mac[i];
    }
    if (!put(61, identifier, sizeof(identifier))) {
        return 0;
    }

    if (request.requestedAddress != 0) {
        std::uint8_t value[4];
        detail::writeU32(value, request.requestedAddress);
        if (!put(50, value, sizeof(value))) {
            return 0;
        }
    }
    if (request.serverAddress != 0) {
        std::uint8_t value[4];
        detail::writeU32(value, request.serverAddress);
        if (!put(54, value, sizeof(value))) {
            return 0;
        }
    }

    if (!request.hostname.empty()) {
        // Bounded. The hostname is the one field here a person can set, so it
        // is the one that has to be treated as untrusted.
        const std::size_t length = request.hostname.size() < 63u ? request.hostname.size() : 63u;
        if (!put(12, reinterpret_cast<const std::uint8_t*>(request.hostname.data()), length)) {
            return 0;
        }
    }

    // Option 55: what we would like back. Asking for less than this and then
    // wondering why the reply had no router is a common way to spend an
    // evening.
    const std::uint8_t wanted[] = {1, 3, 6, 15, 28, 51, 58, 59};
    if (!put(55, wanted, sizeof(wanted))) {
        return 0;
    }

    if (at >= capacity) {
        return 0;
    }
    out[at++] = 255;  // end

    return at > kMinimumSend ? at : kMinimumSend;
}

/// Read a server reply. Returns false for anything that is not one.
inline bool parse(const std::uint8_t* data, std::size_t size, Reply& out) {
    out = Reply();

    if (data == nullptr || size < kFixedBytes || size > kMaxMessageBytes) {
        return false;
    }
    if (data[0] != 2) {  // BOOTREPLY
        return false;
    }
    if (detail::readU32(&data[236]) != kMagicCookie) {
        return false;
    }

    out.xid = detail::readU32(&data[4]);
    out.lease.address = detail::readU32(&data[16]);  // yiaddr

    std::size_t at = kFixedBytes;
    while (at < size) {
        const std::uint8_t code = data[at++];
        if (code == 255) {
            break;  // end
        }
        if (code == 0) {
            continue;  // pad
        }
        if (at >= size) {
            return false;  // a length that ran off the end
        }
        const std::size_t length = data[at++];
        if (at + length > size) {
            return false;
        }
        const std::uint8_t* value = &data[at];
        at += length;

        switch (code) {
            case 53:
                if (length >= 1 && value[0] <= 7) {
                    out.type = static_cast<MessageType>(value[0]);
                }
                break;
            case 1:
                if (length >= 4) {
                    out.lease.mask = detail::readU32(value);
                }
                break;
            case 3:
                // A list of routers; the first is the one to use.
                if (length >= 4) {
                    out.lease.router = detail::readU32(value);
                }
                break;
            case 6:
                if (length >= 4) {
                    out.lease.dns = detail::readU32(value);
                }
                break;
            case 51:
                if (length >= 4) {
                    out.lease.leaseSeconds = detail::readU32(value);
                }
                break;
            case 54:
                if (length >= 4) {
                    out.lease.server = detail::readU32(value);
                }
                break;
            case 58:
                if (length >= 4) {
                    out.lease.renewSeconds = detail::readU32(value);
                }
                break;
            case 59:
                if (length >= 4) {
                    out.lease.rebindSeconds = detail::readU32(value);
                }
                break;
            default:
                break;
        }
    }

    if (out.type == MessageType::kNone) {
        return false;  // BOOTP, or a reply with nothing to say
    }

    // Fill in whichever timers the server left out. Halving and seven-eighths
    // are the RFC's numbers, and the point of both is that a client starts
    // asking well before it has to stop using the address.
    if (out.lease.leaseSeconds != 0 && out.lease.leaseSeconds != kInfiniteLease) {
        if (out.lease.renewSeconds == 0 || out.lease.renewSeconds >= out.lease.leaseSeconds) {
            out.lease.renewSeconds = out.lease.leaseSeconds / 2u;
        }
        if (out.lease.rebindSeconds == 0 || out.lease.rebindSeconds >= out.lease.leaseSeconds ||
            out.lease.rebindSeconds <= out.lease.renewSeconds) {
            out.lease.rebindSeconds = out.lease.leaseSeconds / 8u * 7u;
        }
    }

    out.valid = true;
    return true;
}

}  // namespace dhcp
}  // namespace net
}  // namespace stipple
