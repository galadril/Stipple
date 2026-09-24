// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/net/SntpMessage.h"

#include <cstring>

#include "support/TestFramework.h"

using notrix::net::sntp::kEpochOffsetSeconds;
using notrix::net::sntp::kPacketBytes;
using notrix::net::sntp::Reply;

namespace {

/// A well-formed server reply carrying `unixSeconds`.
void makeReply(std::uint8_t* packet, std::uint32_t unixSeconds, std::uint8_t stratum = 2,
               std::uint8_t leap = 0, std::uint8_t mode = 4) {
    std::memset(packet, 0, kPacketBytes);
    packet[0] = static_cast<std::uint8_t>((leap << 6) | (4 << 3) | mode);
    packet[1] = stratum;
    const std::uint32_t ntp = unixSeconds + kEpochOffsetSeconds;
    packet[40] = static_cast<std::uint8_t>((ntp >> 24) & 0xFFu);
    packet[41] = static_cast<std::uint8_t>((ntp >> 16) & 0xFFu);
    packet[42] = static_cast<std::uint8_t>((ntp >> 8) & 0xFFu);
    packet[43] = static_cast<std::uint8_t>(ntp & 0xFFu);
}

}  // namespace

NOTRIX_TEST(Sntp, RequestIsAClientPacket) {
    std::uint8_t packet[kPacketBytes];
    std::memset(packet, 0xAA, sizeof(packet));
    notrix::net::sntp::build(packet);

    NOTRIX_CHECK_EQ(static_cast<int>((packet[0] >> 3) & 0x7), 4);  // version 4
    NOTRIX_CHECK_EQ(static_cast<int>(packet[0] & 0x7), 3);         // mode client

    // Everything else zeroed - a request that carried this device's idea of
    // the time would be broadcasting that it thinks it is 1970.
    for (std::size_t i = 1; i < kPacketBytes; ++i) {
        NOTRIX_CHECK_EQ(static_cast<int>(packet[i]), 0);
    }
}

NOTRIX_TEST(Sntp, ReadsATimeBack) {
    std::uint8_t packet[kPacketBytes];
    // 2026-09-24T19:00:00Z, chosen because a fixture with a real timestamp
    // beats one with a round number nobody can sanity-check.
    const std::uint32_t when = 1790708400u;
    makeReply(packet, when);

    Reply reply;
    NOTRIX_CHECK(notrix::net::sntp::parse(packet, sizeof(packet), reply));
    NOTRIX_CHECK_EQ(reply.unixSeconds, when);
    NOTRIX_CHECK_EQ(static_cast<int>(reply.stratum), 2);
}

NOTRIX_TEST(Sntp, RefusesAShortPacket) {
    std::uint8_t packet[kPacketBytes];
    makeReply(packet, 1790708400u);
    Reply reply;
    NOTRIX_CHECK(!notrix::net::sntp::parse(packet, kPacketBytes - 1, reply));
    NOTRIX_CHECK(!notrix::net::sntp::parse(nullptr, kPacketBytes, reply));
}

NOTRIX_TEST(Sntp, RefusesAnUnsynchronisedServer) {
    std::uint8_t packet[kPacketBytes];
    // Leap indicator 3: the server is saying its own clock is not to be
    // trusted. A device with no clock at all must not take it anyway.
    makeReply(packet, 1790708400u, 2, 3);
    Reply reply;
    NOTRIX_CHECK(!notrix::net::sntp::parse(packet, sizeof(packet), reply));
}

NOTRIX_TEST(Sntp, RefusesKissOfDeath) {
    std::uint8_t packet[kPacketBytes];
    makeReply(packet, 1790708400u, 0);
    // Stratum 0 puts four ASCII bytes where the timestamp goes; reading it as
    // a time gives a date in 2036 rather than an error.
    std::memcpy(packet + 40, "RATE", 4);
    Reply reply;
    NOTRIX_CHECK(!notrix::net::sntp::parse(packet, sizeof(packet), reply));
}

NOTRIX_TEST(Sntp, RefusesAnImpossibleStratum) {
    std::uint8_t packet[kPacketBytes];
    makeReply(packet, 1790708400u, 16);
    Reply reply;
    NOTRIX_CHECK(!notrix::net::sntp::parse(packet, sizeof(packet), reply));
}

NOTRIX_TEST(Sntp, RefusesSomethingThatIsNotAServerReply) {
    std::uint8_t packet[kPacketBytes];
    // Mode 3 is a client request. Answering our own broadcast would set the
    // clock from a device that does not know the time either.
    makeReply(packet, 1790708400u, 2, 0, 3);
    Reply reply;
    NOTRIX_CHECK(!notrix::net::sntp::parse(packet, sizeof(packet), reply));
}

NOTRIX_TEST(Sntp, RefusesATimestampBeforeTheUnixEpoch) {
    std::uint8_t packet[kPacketBytes];
    makeReply(packet, 0);
    // A zero transmit timestamp means the server did not fill it in.
    NOTRIX_CHECK_EQ(static_cast<int>(packet[40]), 0x83);
    std::memset(packet + 40, 0, 4);
    Reply reply;
    NOTRIX_CHECK(!notrix::net::sntp::parse(packet, sizeof(packet), reply));

    // And anything between the NTP epoch and the Unix one would wrap.
    packet[40] = 0x00;
    packet[41] = 0x00;
    packet[42] = 0x00;
    packet[43] = 0x01;
    NOTRIX_CHECK(!notrix::net::sntp::parse(packet, sizeof(packet), reply));
}

NOTRIX_TEST(Sntp, AcceptsABroadcastReply) {
    std::uint8_t packet[kPacketBytes];
    makeReply(packet, 1790708400u, 3, 0, 5);
    Reply reply;
    NOTRIX_CHECK(notrix::net::sntp::parse(packet, sizeof(packet), reply));
    NOTRIX_CHECK_EQ(reply.unixSeconds, 1790708400u);
}
