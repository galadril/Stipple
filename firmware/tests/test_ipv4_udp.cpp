// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <string>
#include <vector>

#include "notrix/net/Ipv4Udp.h"
#include "support/TestFramework.h"

using notrix::net::ipv4::Datagram;
using notrix::net::ipv4::encapsulate;
using notrix::net::ipv4::extract;
using notrix::net::ipv4::kHeaderBytes;
using notrix::net::ipv4::kIpHeaderBytes;

namespace {

constexpr std::uint32_t ip(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(c) << 8) | static_cast<std::uint32_t>(d);
}

const std::uint8_t kPayload[] = {'n', 'o', 't', 'r', 'i', 'x', '!', 0x00, 0xFF, 0x7F};

std::vector<std::uint8_t> wrap(std::uint32_t source, std::uint32_t destination,
                               const std::uint8_t* payload, std::size_t size) {
    std::vector<std::uint8_t> frame(kHeaderBytes + size + 8, 0);
    const std::size_t wrote =
        encapsulate(payload, size, source, destination, 68, 67, 0x1234, frame.data(), frame.size());
    frame.resize(wrote);
    return frame;
}

/// Recompute the IP header checksum after a test has changed a field, so the
/// test is exercising the thing it means to and not just a broken header.
void refreshIpChecksum(std::vector<std::uint8_t>& frame) {
    frame[10] = 0;
    frame[11] = 0;
    std::uint32_t total = 0;
    for (std::size_t at = 0; at + 1 < kIpHeaderBytes; at += 2) {
        total += static_cast<std::uint32_t>((frame[at] << 8) | frame[at + 1]);
    }
    while ((total >> 16) != 0) {
        total = (total & 0xFFFFu) + (total >> 16);
    }
    const std::uint16_t checksum = static_cast<std::uint16_t>(~total & 0xFFFFu);
    frame[10] = static_cast<std::uint8_t>((checksum >> 8) & 0xFFu);
    frame[11] = static_cast<std::uint8_t>(checksum & 0xFFu);
}

}  // namespace

NOTRIX_TEST(Ipv4Udp, WrapsAndUnwraps) {
    const std::vector<std::uint8_t> frame =
        wrap(ip(0, 0, 0, 0), ip(255, 255, 255, 255), kPayload, sizeof(kPayload));
    NOTRIX_CHECK_EQ(frame.size(), kHeaderBytes + sizeof(kPayload));

    Datagram out;
    NOTRIX_REQUIRE(extract(frame.data(), frame.size(), out));
    NOTRIX_CHECK_EQ(out.source, 0u);
    NOTRIX_CHECK_EQ(out.destination, ip(255, 255, 255, 255));
    NOTRIX_CHECK_EQ(int(out.sourcePort), 68);
    NOTRIX_CHECK_EQ(int(out.destinationPort), 67);
    NOTRIX_CHECK_EQ(out.payloadSize, sizeof(kPayload));
    for (std::size_t i = 0; i < sizeof(kPayload); ++i) {
        NOTRIX_CHECK_EQ(int(out.payload[i]), int(kPayload[i]));
    }
}

NOTRIX_TEST(Ipv4Udp, WritesAHeaderThatLooksRight) {
    const std::vector<std::uint8_t> frame =
        wrap(ip(192, 168, 1, 238), ip(192, 168, 1, 1), kPayload, sizeof(kPayload));

    NOTRIX_CHECK_EQ(int(frame[0]), 0x45);  // IPv4, no options
    NOTRIX_CHECK_EQ(int(frame[9]), 17);    // UDP
    NOTRIX_CHECK_EQ(int(frame[8]), 64);    // ttl

    const std::size_t declared = static_cast<std::size_t>((frame[2] << 8) | frame[3]);
    NOTRIX_CHECK_EQ(declared, frame.size());

    // Never zero: a zero UDP checksum means "not computed", and this one is.
    const std::uint16_t udpChecksum =
        static_cast<std::uint16_t>((frame[26] << 8) | frame[27]);
    NOTRIX_CHECK(udpChecksum != 0);
}

NOTRIX_TEST(Ipv4Udp, ChecksumsAreRightAccordingToTheOtherDirection) {
    // The real check on a checksum: summing a correct packet, checksum field
    // and all, comes to zero. A test that recomputes it the same way the
    // encoder did would pass just as happily with both of them wrong.
    const std::vector<std::uint8_t> frame =
        wrap(ip(10, 0, 0, 7), ip(10, 0, 0, 1), kPayload, sizeof(kPayload));

    std::uint32_t total = 0;
    for (std::size_t at = 0; at + 1 < kIpHeaderBytes; at += 2) {
        total += static_cast<std::uint32_t>((frame[at] << 8) | frame[at + 1]);
    }
    while ((total >> 16) != 0) {
        total = (total & 0xFFFFu) + (total >> 16);
    }
    NOTRIX_CHECK_EQ(total & 0xFFFFu, 0xFFFFu);
}

NOTRIX_TEST(Ipv4Udp, RejectsACorruptHeader) {
    std::vector<std::uint8_t> frame =
        wrap(ip(0, 0, 0, 0), ip(255, 255, 255, 255), kPayload, sizeof(kPayload));
    frame[12] ^= 0xFFu;  // change a source octet, leave the checksum alone

    Datagram out;
    NOTRIX_CHECK_FALSE(extract(frame.data(), frame.size(), out));
}

NOTRIX_TEST(Ipv4Udp, RejectsACorruptPayload) {
    std::vector<std::uint8_t> frame =
        wrap(ip(0, 0, 0, 0), ip(255, 255, 255, 255), kPayload, sizeof(kPayload));
    frame[kHeaderBytes] ^= 0xFFu;

    Datagram out;
    NOTRIX_CHECK_FALSE(extract(frame.data(), frame.size(), out));
}

NOTRIX_TEST(Ipv4Udp, IgnoresEverythingThatIsNotUdp) {
    // A packet socket sees all of it. None of these are errors, they are
    // Tuesday.
    std::vector<std::uint8_t> frame =
        wrap(ip(0, 0, 0, 0), ip(255, 255, 255, 255), kPayload, sizeof(kPayload));
    Datagram out;

    std::vector<std::uint8_t> tcp = frame;
    tcp[9] = 6;
    refreshIpChecksum(tcp);
    NOTRIX_CHECK_FALSE(extract(tcp.data(), tcp.size(), out));

    std::vector<std::uint8_t> six = frame;
    six[0] = 0x60;
    NOTRIX_CHECK_FALSE(extract(six.data(), six.size(), out));

    NOTRIX_CHECK_FALSE(extract(frame.data(), 4, out));
    NOTRIX_CHECK_FALSE(extract(nullptr, 100, out));
}

NOTRIX_TEST(Ipv4Udp, IgnoresFragments) {
    // Nothing here is ever fragmented, and reassembling one would mean
    // holding state for something that arrived unasked-for.
    std::vector<std::uint8_t> frame =
        wrap(ip(0, 0, 0, 0), ip(255, 255, 255, 255), kPayload, sizeof(kPayload));
    Datagram out;

    std::vector<std::uint8_t> later = frame;
    later[7] = 0x10;  // a non-zero fragment offset
    refreshIpChecksum(later);
    NOTRIX_CHECK_FALSE(extract(later.data(), later.size(), out));

    std::vector<std::uint8_t> more = frame;
    more[6] = 0x20;  // more-fragments
    refreshIpChecksum(more);
    NOTRIX_CHECK_FALSE(extract(more.data(), more.size(), out));
}

NOTRIX_TEST(Ipv4Udp, RefusesLengthsThatWouldReadPastTheBuffer) {
    // The one that matters. A driver that hands up a short buffer with a
    // header claiming more is how this reads somebody else's memory.
    std::vector<std::uint8_t> frame =
        wrap(ip(0, 0, 0, 0), ip(255, 255, 255, 255), kPayload, sizeof(kPayload));
    Datagram out;

    std::vector<std::uint8_t> lying = frame;
    lying[2] = 0x0F;
    lying[3] = 0xFF;  // claims 4095 bytes
    refreshIpChecksum(lying);
    NOTRIX_CHECK_FALSE(extract(lying.data(), lying.size(), out));

    // A UDP length longer than the IP total length says is available.
    std::vector<std::uint8_t> udpLying = frame;
    udpLying[24] = 0x0F;
    udpLying[25] = 0xFF;
    NOTRIX_CHECK_FALSE(extract(udpLying.data(), udpLying.size(), out));

    // And a header length field pointing outside the buffer.
    std::vector<std::uint8_t> ihl = frame;
    ihl[0] = 0x4F;  // 60-byte header on a 38-byte frame
    refreshIpChecksum(ihl);
    NOTRIX_CHECK_FALSE(extract(ihl.data(), ihl.size(), out));
}

NOTRIX_TEST(Ipv4Udp, AcceptsADatagramWithNoChecksum) {
    // Legal over IPv4, and plenty of servers do it.
    std::vector<std::uint8_t> frame =
        wrap(ip(0, 0, 0, 0), ip(255, 255, 255, 255), kPayload, sizeof(kPayload));
    frame[26] = 0;
    frame[27] = 0;

    Datagram out;
    NOTRIX_REQUIRE(extract(frame.data(), frame.size(), out));
    NOTRIX_CHECK_EQ(out.payloadSize, sizeof(kPayload));
}

NOTRIX_TEST(Ipv4Udp, HandlesAnOddNumberOfPayloadBytes) {
    // The odd trailing byte is padded on the right. Getting that backwards
    // produces a checksum that is wrong only for odd-length packets, which is
    // the sort of bug that survives a year.
    const std::uint8_t odd[] = {0xAB, 0xCD, 0xEF};
    const std::vector<std::uint8_t> frame =
        wrap(ip(10, 0, 0, 7), ip(10, 0, 0, 1), odd, sizeof(odd));

    Datagram out;
    NOTRIX_REQUIRE(extract(frame.data(), frame.size(), out));
    NOTRIX_CHECK_EQ(out.payloadSize, sizeof(odd));
    NOTRIX_CHECK_EQ(int(out.payload[2]), 0xEF);
}

NOTRIX_TEST(Ipv4Udp, RefusesToWriteIntoABufferTooSmall) {
    std::uint8_t small[16];
    NOTRIX_CHECK_EQ(encapsulate(kPayload, sizeof(kPayload), 0, 0xFFFFFFFFu, 68, 67, 1, small,
                                sizeof(small)),
                    std::size_t(0));
}
