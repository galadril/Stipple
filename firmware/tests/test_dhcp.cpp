// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <string>
#include <vector>

#include "stipple/net/DhcpClient.h"
#include "stipple/net/DhcpMessage.h"
#include "support/TestFramework.h"

using stipple::net::dhcp::build;
using stipple::net::dhcp::DhcpClient;
using stipple::net::dhcp::formatIpv4;
using stipple::net::dhcp::kBroadcastAddress;
using stipple::net::dhcp::kFixedBytes;
using stipple::net::dhcp::kInfiniteLease;
using stipple::net::dhcp::kMagicCookie;
using stipple::net::dhcp::MessageType;
using stipple::net::dhcp::parse;
using stipple::net::dhcp::parseIpv4;
using stipple::net::dhcp::prefixLength;
using stipple::net::dhcp::Reply;
using stipple::net::dhcp::Request;

namespace {

constexpr std::uint32_t ip(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(c) << 8) | static_cast<std::uint32_t>(d);
}

/// Not a real MAC. A test fixture should not carry a device's hardware
/// address into a public repository.
const std::uint8_t kMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

constexpr std::uint32_t kOurAddress = ip(192, 168, 1, 238);
constexpr std::uint32_t kServer = ip(192, 168, 1, 1);
constexpr std::uint32_t kMask = ip(255, 255, 255, 0);
constexpr std::uint32_t kRouter = ip(192, 168, 1, 1);
constexpr std::uint32_t kDns = ip(192, 168, 1, 1);

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void appendOption(std::vector<std::uint8_t>& out, std::uint8_t code, std::uint32_t value) {
    out.push_back(code);
    out.push_back(4);
    appendU32(out, value);
}

/// What a server would send. Written here rather than reusing build(),
/// because a test that encodes with the same code it decodes with proves
/// only that the code agrees with itself.
struct ServerReply {
    MessageType type = MessageType::kOffer;
    std::uint32_t xid = 0;
    std::uint32_t address = kOurAddress;
    std::uint32_t server = kServer;
    std::uint32_t mask = kMask;
    std::uint32_t router = kRouter;
    std::uint32_t dns = kDns;
    std::uint32_t leaseSeconds = 3600;
    std::uint32_t t1 = 0;
    std::uint32_t t2 = 0;
};

std::vector<std::uint8_t> encode(const ServerReply& reply) {
    std::vector<std::uint8_t> out(kFixedBytes, 0);
    out[0] = 2;  // BOOTREPLY
    out[1] = 1;
    out[2] = 6;
    out[4] = static_cast<std::uint8_t>((reply.xid >> 24) & 0xFFu);
    out[5] = static_cast<std::uint8_t>((reply.xid >> 16) & 0xFFu);
    out[6] = static_cast<std::uint8_t>((reply.xid >> 8) & 0xFFu);
    out[7] = static_cast<std::uint8_t>(reply.xid & 0xFFu);
    out[16] = static_cast<std::uint8_t>((reply.address >> 24) & 0xFFu);
    out[17] = static_cast<std::uint8_t>((reply.address >> 16) & 0xFFu);
    out[18] = static_cast<std::uint8_t>((reply.address >> 8) & 0xFFu);
    out[19] = static_cast<std::uint8_t>(reply.address & 0xFFu);
    for (std::size_t i = 0; i < 6; ++i) {
        out[28 + i] = kMac[i];
    }
    out[236] = 0x63;
    out[237] = 0x82;
    out[238] = 0x53;
    out[239] = 0x63;

    out.push_back(53);
    out.push_back(1);
    out.push_back(static_cast<std::uint8_t>(reply.type));
    if (reply.mask != 0) {
        appendOption(out, 1, reply.mask);
    }
    if (reply.router != 0) {
        appendOption(out, 3, reply.router);
    }
    if (reply.dns != 0) {
        appendOption(out, 6, reply.dns);
    }
    if (reply.leaseSeconds != 0) {
        appendOption(out, 51, reply.leaseSeconds);
    }
    if (reply.server != 0) {
        appendOption(out, 54, reply.server);
    }
    if (reply.t1 != 0) {
        appendOption(out, 58, reply.t1);
    }
    if (reply.t2 != 0) {
        appendOption(out, 59, reply.t2);
    }
    out.push_back(255);
    return out;
}

/// Pull one option back out of a packet we built, so a test can check what
/// actually went on the wire rather than what the struct said.
bool findOption(const std::uint8_t* data, std::size_t size, std::uint8_t wanted,
                std::vector<std::uint8_t>& value) {
    value.clear();
    if (size < kFixedBytes) {
        return false;
    }
    std::size_t at = kFixedBytes;
    while (at < size) {
        const std::uint8_t code = data[at++];
        if (code == 255) {
            return false;
        }
        if (code == 0) {
            continue;
        }
        if (at >= size) {
            return false;
        }
        const std::size_t length = data[at++];
        if (at + length > size) {
            return false;
        }
        if (code == wanted) {
            value.assign(&data[at], &data[at + length]);
            return true;
        }
        at += length;
    }
    return false;
}

std::uint32_t xidOf(const DhcpClient::Packet& packet) {
    return (static_cast<std::uint32_t>(packet.data[4]) << 24) |
           (static_cast<std::uint32_t>(packet.data[5]) << 16) |
           (static_cast<std::uint32_t>(packet.data[6]) << 8) |
           static_cast<std::uint32_t>(packet.data[7]);
}

std::uint32_t ciaddrOf(const DhcpClient::Packet& packet) {
    return (static_cast<std::uint32_t>(packet.data[12]) << 24) |
           (static_cast<std::uint32_t>(packet.data[13]) << 16) |
           (static_cast<std::uint32_t>(packet.data[14]) << 8) |
           static_cast<std::uint32_t>(packet.data[15]);
}

std::uint8_t typeOf(const DhcpClient::Packet& packet) {
    std::vector<std::uint8_t> value;
    if (!findOption(packet.data, packet.size, 53, value) || value.empty()) {
        return 0;
    }
    return value[0];
}

/// Drive a client all the way to bound, and report the time it happened.
///
/// Plain returns rather than STIPPLE_REQUIRE: the macro returns from the
/// enclosing function, which a helper with a value to give back cannot do.
/// A failure here shows up as the caller's own assertions failing, which is
/// the right place to read it anyway.
std::uint64_t handshake(DhcpClient& client, std::uint64_t startMillis,
                        std::uint32_t leaseSeconds = 3600) {
    client.start(kMac, 0x1234u, startMillis);

    DhcpClient::Packet packet;
    if (!client.tick(startMillis, packet)) {
        return startMillis;
    }
    const std::uint32_t xid = xidOf(packet);

    ServerReply offer;
    offer.type = MessageType::kOffer;
    offer.xid = xid;
    offer.leaseSeconds = leaseSeconds;
    const std::vector<std::uint8_t> offerBytes = encode(offer);
    client.receive(offerBytes.data(), offerBytes.size(), startMillis + 100);

    if (!client.tick(startMillis + 100, packet)) {
        return startMillis;
    }

    ServerReply ack = offer;
    ack.type = MessageType::kAck;
    const std::vector<std::uint8_t> ackBytes = encode(ack);
    client.receive(ackBytes.data(), ackBytes.size(), startMillis + 200);

    return startMillis + 200;
}

}  // namespace

// --- the wire format ------------------------------------------------------

STIPPLE_TEST(Dhcp, BuildsADiscoverThatLooksLikeOne) {
    Request request;
    request.xid = 0xDEADBEEFu;
    for (std::size_t i = 0; i < 6; ++i) {
        request.mac[i] = kMac[i];
    }
    request.hostname = "stipple";

    std::uint8_t out[600];
    const std::size_t size = build(MessageType::kDiscover, request, out, sizeof(out));
    STIPPLE_CHECK(size >= 300);

    STIPPLE_CHECK_EQ(int(out[0]), 1);  // BOOTREQUEST
    STIPPLE_CHECK_EQ(int(out[1]), 1);  // ethernet
    STIPPLE_CHECK_EQ(int(out[2]), 6);
    STIPPLE_CHECK_EQ(int(out[10]) & 0x80, 0x80);  // broadcast, and it must be
    for (std::size_t i = 0; i < 6; ++i) {
        STIPPLE_CHECK_EQ(int(out[28 + i]), int(kMac[i]));
    }

    const std::uint32_t cookie = (static_cast<std::uint32_t>(out[236]) << 24) |
                                 (static_cast<std::uint32_t>(out[237]) << 16) |
                                 (static_cast<std::uint32_t>(out[238]) << 8) |
                                 static_cast<std::uint32_t>(out[239]);
    STIPPLE_CHECK_EQ(cookie, kMagicCookie);

    std::vector<std::uint8_t> value;
    STIPPLE_REQUIRE(findOption(out, size, 53, value));
    STIPPLE_CHECK_EQ(int(value[0]), 1);

    STIPPLE_REQUIRE(findOption(out, size, 12, value));
    STIPPLE_CHECK_EQ(std::string(value.begin(), value.end()), std::string("stipple"));

    // The client identifier is what stops the address wandering between
    // reboots, so it is worth asserting rather than assuming.
    STIPPLE_REQUIRE(findOption(out, size, 61, value));
    STIPPLE_CHECK_EQ(value.size(), std::size_t(7));
    STIPPLE_CHECK_EQ(int(value[0]), 1);
    STIPPLE_CHECK_EQ(int(value[6]), int(kMac[5]));
}

STIPPLE_TEST(Dhcp, ReadsAnOffer) {
    ServerReply offer;
    offer.xid = 0x11223344u;
    const std::vector<std::uint8_t> bytes = encode(offer);

    Reply reply;
    STIPPLE_REQUIRE(parse(bytes.data(), bytes.size(), reply));
    STIPPLE_CHECK(reply.valid);
    STIPPLE_CHECK(reply.type == MessageType::kOffer);
    STIPPLE_CHECK_EQ(reply.xid, 0x11223344u);
    STIPPLE_CHECK_EQ(reply.lease.address, kOurAddress);
    STIPPLE_CHECK_EQ(reply.lease.mask, kMask);
    STIPPLE_CHECK_EQ(reply.lease.router, kRouter);
    STIPPLE_CHECK_EQ(reply.lease.server, kServer);
    STIPPLE_CHECK_EQ(reply.lease.leaseSeconds, 3600u);
}

STIPPLE_TEST(Dhcp, FillsInTheTimersAServerLeftOut) {
    ServerReply offer;
    offer.xid = 1;
    offer.leaseSeconds = 3600;
    const std::vector<std::uint8_t> bytes = encode(offer);

    Reply reply;
    STIPPLE_REQUIRE(parse(bytes.data(), bytes.size(), reply));
    STIPPLE_CHECK_EQ(reply.lease.renewSeconds, 1800u);   // half
    STIPPLE_CHECK_EQ(reply.lease.rebindSeconds, 3150u);  // seven eighths
}

STIPPLE_TEST(Dhcp, KeepsTheTimersAServerDidSend) {
    ServerReply offer;
    offer.xid = 1;
    offer.leaseSeconds = 3600;
    offer.t1 = 900;
    offer.t2 = 2700;
    const std::vector<std::uint8_t> bytes = encode(offer);

    Reply reply;
    STIPPLE_REQUIRE(parse(bytes.data(), bytes.size(), reply));
    STIPPLE_CHECK_EQ(reply.lease.renewSeconds, 900u);
    STIPPLE_CHECK_EQ(reply.lease.rebindSeconds, 2700u);
}

STIPPLE_TEST(Dhcp, RefusesRubbish) {
    Reply reply;

    // Too short to be a DHCP packet.
    std::vector<std::uint8_t> bytes(100, 0);
    STIPPLE_CHECK_FALSE(parse(bytes.data(), bytes.size(), reply));

    // A request, not a reply - our own broadcast coming back to us.
    ServerReply offer;
    offer.xid = 1;
    bytes = encode(offer);
    bytes[0] = 1;
    STIPPLE_CHECK_FALSE(parse(bytes.data(), bytes.size(), reply));

    // Wrong cookie: BOOTP, or something else entirely on port 68.
    bytes = encode(offer);
    bytes[238] = 0x00;
    STIPPLE_CHECK_FALSE(parse(bytes.data(), bytes.size(), reply));

    // No message type. A BOOTP reply is not a lease.
    bytes.assign(kFixedBytes, 0);
    bytes[0] = 2;
    bytes[236] = 0x63;
    bytes[237] = 0x82;
    bytes[238] = 0x53;
    bytes[239] = 0x63;
    bytes.push_back(255);
    STIPPLE_CHECK_FALSE(parse(bytes.data(), bytes.size(), reply));
}

STIPPLE_TEST(Dhcp, RefusesAnOptionThatRunsOffTheEnd) {
    // The one that matters: a length byte claiming more bytes than arrived is
    // how a hand-written parser reads past its buffer, and this parser is fed
    // by anything that can reach port 68.
    ServerReply offer;
    offer.xid = 1;
    std::vector<std::uint8_t> bytes = encode(offer);
    bytes.pop_back();  // drop the end marker
    bytes.push_back(6);
    bytes.push_back(64);  // says 64 bytes follow
    bytes.push_back(1);   // one does

    Reply reply;
    STIPPLE_CHECK_FALSE(parse(bytes.data(), bytes.size(), reply));
}

STIPPLE_TEST(Dhcp, RefusesAPacketLargerThanAnyServerWouldSend) {
    ServerReply offer;
    offer.xid = 1;
    std::vector<std::uint8_t> bytes = encode(offer);
    bytes.resize(4096, 0);

    Reply reply;
    STIPPLE_CHECK_FALSE(parse(bytes.data(), bytes.size(), reply));
}

STIPPLE_TEST(Dhcp, FormatsAndParsesAddresses) {
    STIPPLE_CHECK_EQ(formatIpv4(kOurAddress), std::string("192.168.1.238"));
    STIPPLE_CHECK_EQ(formatIpv4(0), std::string("0.0.0.0"));
    STIPPLE_CHECK_EQ(formatIpv4(kBroadcastAddress), std::string("255.255.255.255"));

    std::uint32_t value = 0;
    STIPPLE_REQUIRE(parseIpv4("192.168.1.238", value));
    STIPPLE_CHECK_EQ(value, kOurAddress);

    // Everything a person might type that is not an address.
    STIPPLE_CHECK_FALSE(parseIpv4("192.168.1", value));
    STIPPLE_CHECK_FALSE(parseIpv4("192.168.1.999", value));
    STIPPLE_CHECK_FALSE(parseIpv4("192.168.1.2.3", value));
    STIPPLE_CHECK_FALSE(parseIpv4("192.168.1.", value));
    STIPPLE_CHECK_FALSE(parseIpv4("", value));
    STIPPLE_CHECK_FALSE(parseIpv4("hello", value));
    STIPPLE_CHECK_FALSE(parseIpv4(" 192.168.1.1", value));
    STIPPLE_CHECK_FALSE(parseIpv4("192.168.1.1 ", value));
}

STIPPLE_TEST(Dhcp, CountsPrefixBits) {
    STIPPLE_CHECK_EQ(prefixLength(kMask), 24);
    STIPPLE_CHECK_EQ(prefixLength(ip(255, 255, 0, 0)), 16);
    STIPPLE_CHECK_EQ(prefixLength(0xFFFFFFFFu), 32);
    STIPPLE_CHECK_EQ(prefixLength(0), 0);
}

// --- the state machine ----------------------------------------------------

STIPPLE_TEST(DhcpClient, DoesNothingUntilStarted) {
    DhcpClient client;
    DhcpClient::Packet packet;
    STIPPLE_CHECK(client.state() == DhcpClient::State::kIdle);
    STIPPLE_CHECK_FALSE(client.tick(1000, packet));
    STIPPLE_CHECK_FALSE(client.bound());
}

STIPPLE_TEST(DhcpClient, WalksDiscoverOfferRequestAck) {
    DhcpClient client;
    client.start(kMac, 0x1234u, 1000);
    STIPPLE_CHECK(client.state() == DhcpClient::State::kSelecting);

    DhcpClient::Packet packet;
    STIPPLE_REQUIRE(client.tick(1000, packet));
    STIPPLE_CHECK_EQ(int(typeOf(packet)), 1);  // DISCOVER
    STIPPLE_CHECK_EQ(packet.destination, kBroadcastAddress);
    const std::uint32_t xid = xidOf(packet);

    ServerReply offer;
    offer.xid = xid;
    const std::vector<std::uint8_t> offerBytes = encode(offer);
    client.receive(offerBytes.data(), offerBytes.size(), 1100);
    STIPPLE_CHECK(client.state() == DhcpClient::State::kRequesting);

    // The REQUEST goes out at once rather than waiting for the retry timer:
    // an offer is only held for a moment.
    STIPPLE_REQUIRE(client.tick(1100, packet));
    STIPPLE_CHECK_EQ(int(typeOf(packet)), 3);  // REQUEST
    STIPPLE_CHECK_EQ(xidOf(packet), xid);      // same conversation

    std::vector<std::uint8_t> value;
    STIPPLE_REQUIRE(findOption(packet.data, packet.size, 50, value));
    STIPPLE_CHECK_EQ(value.size(), std::size_t(4));
    STIPPLE_REQUIRE(findOption(packet.data, packet.size, 54, value));
    STIPPLE_CHECK_EQ(int(value[3]), 1);  // the server that offered

    ServerReply ack = offer;
    ack.type = MessageType::kAck;
    const std::vector<std::uint8_t> ackBytes = encode(ack);
    client.receive(ackBytes.data(), ackBytes.size(), 1200);

    STIPPLE_CHECK(client.state() == DhcpClient::State::kBound);
    STIPPLE_CHECK(client.bound());
    STIPPLE_CHECK_EQ(client.lease().address, kOurAddress);
    STIPPLE_CHECK_EQ(client.lease().mask, kMask);
    STIPPLE_CHECK_EQ(client.lease().router, kRouter);
    STIPPLE_CHECK(client.takeAcquired());
    STIPPLE_CHECK_FALSE(client.takeAcquired());  // reading it clears it
    STIPPLE_CHECK_FALSE(client.takeLost());
}

STIPPLE_TEST(DhcpClient, IgnoresSomebodyElsesConversation) {
    DhcpClient client;
    client.start(kMac, 0x1234u, 1000);

    DhcpClient::Packet packet;
    STIPPLE_REQUIRE(client.tick(1000, packet));

    ServerReply offer;
    offer.xid = xidOf(packet) ^ 0xFFFFFFFFu;  // not ours
    const std::vector<std::uint8_t> bytes = encode(offer);
    client.receive(bytes.data(), bytes.size(), 1100);

    STIPPLE_CHECK(client.state() == DhcpClient::State::kSelecting);
}

STIPPLE_TEST(DhcpClient, BacksOffBetweenRetries) {
    DhcpClient client;
    client.start(kMac, 0x1234u, 1000);

    DhcpClient::Packet packet;
    STIPPLE_REQUIRE(client.tick(1000, packet));

    // Four seconds, and not a tick before it.
    STIPPLE_CHECK_FALSE(client.tick(4999, packet));
    STIPPLE_REQUIRE(client.tick(5000, packet));

    // Then eight.
    STIPPLE_CHECK_FALSE(client.tick(12999, packet));
    STIPPLE_REQUIRE(client.tick(13000, packet));

    // Then sixteen.
    STIPPLE_CHECK_FALSE(client.tick(28999, packet));
    STIPPLE_REQUIRE(client.tick(29000, packet));
}

STIPPLE_TEST(DhcpClient, KeepsAskingForeverAndNeverFasterThanAMinute) {
    // A client that gives up is a device nobody can reach, and nobody is
    // standing next to a clock waiting to restart it. It has to keep trying -
    // and it has to stop getting further apart, or by the next morning the
    // retries are hours apart and a router that came back is not noticed.
    DhcpClient client;
    client.start(kMac, 0x1234u, 0);

    DhcpClient::Packet packet;
    std::uint64_t now = 0;
    std::uint64_t lastSend = 0;
    std::uint64_t longestGap = 0;
    int sends = 0;

    for (; now < 24ull * 60ull * 60ull * 1000ull; now += 500) {
        if (client.tick(now, packet)) {
            if (sends > 0) {
                const std::uint64_t gap = now - lastSend;
                if (gap > longestGap) {
                    longestGap = gap;
                }
            }
            lastSend = now;
            ++sends;
        }
    }

    STIPPLE_CHECK(sends > 1400);            // still asking a day later
    STIPPLE_CHECK(longestGap <= 60000u);    // never more than a minute apart
    STIPPLE_CHECK(client.state() == DhcpClient::State::kSelecting);
}

STIPPLE_TEST(DhcpClient, StartsOverWhenAnOfferIsFollowedBySilence) {
    DhcpClient client;
    client.start(kMac, 0x1234u, 1000);

    DhcpClient::Packet packet;
    STIPPLE_REQUIRE(client.tick(1000, packet));

    ServerReply offer;
    offer.xid = xidOf(packet);
    const std::vector<std::uint8_t> bytes = encode(offer);
    client.receive(bytes.data(), bytes.size(), 1100);
    STIPPLE_CHECK(client.state() == DhcpClient::State::kRequesting);

    // Four REQUESTs, no answer. The address has gone to somebody else.
    std::uint64_t now = 1100;
    for (int i = 0; i < 8 && client.state() == DhcpClient::State::kRequesting; ++i) {
        client.tick(now, packet);
        now += 70000;
    }
    STIPPLE_CHECK(client.state() == DhcpClient::State::kSelecting);
}

STIPPLE_TEST(DhcpClient, RenewsAtHalfTheLeaseAndUnicastsToTheServer) {
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 1000, 3600);
    STIPPLE_CHECK(client.takeAcquired());  // the first bind, cleared before asking about the renewal

    DhcpClient::Packet packet;
    STIPPLE_CHECK_FALSE(client.tick(bound + 1799999, packet));
    STIPPLE_CHECK(client.state() == DhcpClient::State::kBound);

    STIPPLE_REQUIRE(client.tick(bound + 1800000, packet));
    STIPPLE_CHECK(client.state() == DhcpClient::State::kRenewing);
    STIPPLE_CHECK_EQ(int(typeOf(packet)), 3);
    STIPPLE_CHECK_EQ(packet.destination, kServer);  // unicast, not broadcast

    // A renewal says "I have this", so the address is in ciaddr and there is
    // no option 50 asking for it.
    STIPPLE_CHECK_EQ(ciaddrOf(packet), kOurAddress);
    std::vector<std::uint8_t> value;
    STIPPLE_CHECK_FALSE(findOption(packet.data, packet.size, 50, value));
    STIPPLE_CHECK_EQ(int(packet.data[10]) & 0x80, 0);  // and no broadcast flag

    ServerReply ack;
    ack.type = MessageType::kAck;
    ack.xid = xidOf(packet);
    const std::vector<std::uint8_t> bytes = encode(ack);
    client.receive(bytes.data(), bytes.size(), bound + 1800100);

    STIPPLE_CHECK(client.state() == DhcpClient::State::kBound);
    // Same address, same everything: nothing for the caller to reconfigure.
    STIPPLE_CHECK_FALSE(client.takeAcquired());
}

STIPPLE_TEST(DhcpClient, RebindsByBroadcastWhenTheServerGoesQuiet) {
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 1000, 3600);

    DhcpClient::Packet packet;
    std::uint64_t now = bound + 1800000;
    // Renew, and get nothing back, until T2 at seven eighths.
    while (now < bound + 3150000) {
        client.tick(now, packet);
        now += 1000;
    }
    STIPPLE_REQUIRE(client.tick(bound + 3150000, packet));
    STIPPLE_CHECK(client.state() == DhcpClient::State::kRebinding);
    STIPPLE_CHECK_EQ(packet.destination, kBroadcastAddress);
    STIPPLE_CHECK_EQ(ciaddrOf(packet), kOurAddress);
}

STIPPLE_TEST(DhcpClient, GivesTheAddressUpWhenTheLeaseRunsOut) {
    // Keeping an expired lease is how two devices end up with one address,
    // and the second one to notice is the one that stops working.
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 1000, 3600);
    STIPPLE_CHECK(client.takeAcquired());

    DhcpClient::Packet packet;
    std::uint64_t now = bound + 1800000;
    while (now < bound + 3600000) {
        client.tick(now, packet);
        now += 1000;
    }
    STIPPLE_CHECK(client.bound());

    client.tick(bound + 3600000, packet);
    STIPPLE_CHECK_FALSE(client.bound());
    STIPPLE_CHECK(client.state() == DhcpClient::State::kSelecting);
    STIPPLE_CHECK(client.takeLost());
    STIPPLE_CHECK_EQ(client.lease().address, 0u);
}

STIPPLE_TEST(DhcpClient, TakesANakAsAnAnswer) {
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 1000, 3600);
    STIPPLE_CHECK(client.takeAcquired());

    DhcpClient::Packet packet;
    STIPPLE_REQUIRE(client.tick(bound + 1800000, packet));

    ServerReply nak;
    nak.type = MessageType::kNak;
    nak.xid = xidOf(packet);
    const std::vector<std::uint8_t> bytes = encode(nak);
    client.receive(bytes.data(), bytes.size(), bound + 1800100);

    STIPPLE_CHECK(client.state() == DhcpClient::State::kSelecting);
    STIPPLE_CHECK(client.takeLost());
    STIPPLE_CHECK_EQ(client.lease().address, 0u);
}

STIPPLE_TEST(DhcpClient, CarriesTheMaskAndRouterThroughARenewalThatOmitsThem) {
    // A renewal usually answers with the address and nothing else. Treating
    // the missing fields as zero loses the default route every half hour,
    // which looks exactly like flaky Wi-Fi.
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 1000, 3600);
    STIPPLE_CHECK(client.takeAcquired());

    DhcpClient::Packet packet;
    STIPPLE_REQUIRE(client.tick(bound + 1800000, packet));

    ServerReply ack;
    ack.type = MessageType::kAck;
    ack.xid = xidOf(packet);
    ack.mask = 0;
    ack.router = 0;
    ack.dns = 0;
    ack.server = 0;
    const std::vector<std::uint8_t> bytes = encode(ack);
    client.receive(bytes.data(), bytes.size(), bound + 1800100);

    STIPPLE_CHECK(client.state() == DhcpClient::State::kBound);
    STIPPLE_CHECK_EQ(client.lease().mask, kMask);
    STIPPLE_CHECK_EQ(client.lease().router, kRouter);
    STIPPLE_CHECK_EQ(client.lease().server, kServer);
    STIPPLE_CHECK_FALSE(client.takeAcquired());
}

STIPPLE_TEST(DhcpClient, TellsTheCallerWhenARenewalMovedTheAddress) {
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 1000, 3600);
    STIPPLE_CHECK(client.takeAcquired());

    DhcpClient::Packet packet;
    STIPPLE_REQUIRE(client.tick(bound + 1800000, packet));

    ServerReply ack;
    ack.type = MessageType::kAck;
    ack.xid = xidOf(packet);
    ack.address = ip(192, 168, 1, 99);
    const std::vector<std::uint8_t> bytes = encode(ack);
    client.receive(bytes.data(), bytes.size(), bound + 1800100);

    STIPPLE_CHECK(client.takeAcquired());
    STIPPLE_CHECK_EQ(client.lease().address, ip(192, 168, 1, 99));
}

STIPPLE_TEST(DhcpClient, AsksForTheAddressItAlreadyHas) {
    // The point of this on a TC002: STIPPLE starts on an address the vendor
    // application obtained, and taking over the lease should be invisible to
    // everything else on the network.
    DhcpClient client;
    client.setPreferredAddress(kOurAddress);
    client.start(kMac, 0x1234u, 1000);

    DhcpClient::Packet packet;
    STIPPLE_REQUIRE(client.tick(1000, packet));

    std::vector<std::uint8_t> value;
    STIPPLE_REQUIRE(findOption(packet.data, packet.size, 50, value));
    STIPPLE_CHECK_EQ(value.size(), std::size_t(4));
    STIPPLE_CHECK_EQ(int(value[0]), 192);
    STIPPLE_CHECK_EQ(int(value[3]), 238);
}

STIPPLE_TEST(DhcpClient, NeverRenewsAnInfiniteLease) {
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 1000, kInfiniteLease);

    DhcpClient::Packet packet;
    STIPPLE_CHECK(client.state() == DhcpClient::State::kBound);
    STIPPLE_CHECK_FALSE(client.tick(bound + 7ull * 24ull * 60ull * 60ull * 1000ull, packet));
    STIPPLE_CHECK(client.bound());
    STIPPLE_CHECK_EQ(client.remainingSeconds(bound), kInfiniteLease);
}

STIPPLE_TEST(DhcpClient, KeepsTheRenewalInsideAVeryShortLease) {
    // A sixty-second lease should not become a packet storm, but the floor on
    // the renewal interval must never land after the lease has already gone.
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 1000, 20);

    DhcpClient::Packet packet;
    bool renewed = false;
    for (std::uint64_t now = bound; now < bound + 20000; now += 500) {
        if (client.tick(now, packet) && client.state() == DhcpClient::State::kRenewing) {
            renewed = true;
            break;
        }
    }
    STIPPLE_CHECK(renewed);
    STIPPLE_CHECK(client.bound());
}

STIPPLE_TEST(DhcpClient, SurvivesTheClockStepping) {
    // Not hypothetical: the system clock is set from the network well after
    // this starts running, so the first big step backwards is normal.
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 10000000, 3600);

    DhcpClient::Packet packet;
    STIPPLE_CHECK_FALSE(client.tick(bound + 1000, packet));

    // Time jumps back half an hour. The renewal must still be half an hour of
    // real time away, not parked an hour into the future.
    const std::uint64_t stepped = bound + 1000 - 1800000;
    STIPPLE_CHECK_FALSE(client.tick(stepped, packet));
    STIPPLE_REQUIRE(client.tick(stepped + 1799000, packet));
    STIPPLE_CHECK(client.state() == DhcpClient::State::kRenewing);
}

STIPPLE_TEST(DhcpClient, ReportsWhatIsLeftOnTheLease) {
    DhcpClient client;
    const std::uint64_t bound = handshake(client, 1000, 3600);

    STIPPLE_CHECK_EQ(client.remainingSeconds(bound), 3600u);
    STIPPLE_CHECK_EQ(client.remainingSeconds(bound + 600000), 3000u);
    STIPPLE_CHECK_EQ(client.remainingSeconds(bound + 3600000), 0u);
}

STIPPLE_TEST(DhcpClient, StoppingKeepsTheAddressBecauseItIsStillValid) {
    // stop() is what the hotspot calls before taking the radio. The address
    // does not become wrong just because nobody is renewing it, and the
    // caller may well want to put it straight back.
    DhcpClient client;
    handshake(client, 1000, 3600);
    client.stop();

    STIPPLE_CHECK(client.state() == DhcpClient::State::kIdle);
    STIPPLE_CHECK_EQ(client.lease().address, kOurAddress);
    STIPPLE_CHECK_FALSE(client.takeLost());
}
