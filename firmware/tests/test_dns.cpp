// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/net/DnsMessage.h"

#include <cstring>
#include <string>
#include <vector>

#include "support/TestFramework.h"

using stipple::net::dns::build;
using stipple::net::dns::kMaxMessageBytes;
using stipple::net::dns::parse;
using stipple::net::dns::Result;

namespace {

void u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

/// A reply header: id, flags, and the four counts.
std::vector<std::uint8_t> header(std::uint16_t id, std::uint16_t answers,
                                 std::uint8_t flagsHigh = 0x81, std::uint8_t flagsLow = 0x80) {
    std::vector<std::uint8_t> out;
    u16(out, id);
    out.push_back(flagsHigh);
    out.push_back(flagsLow);
    u16(out, 1);        // one question, echoed back
    u16(out, answers);
    u16(out, 0);
    u16(out, 0);
    return out;
}

void name(std::vector<std::uint8_t>& out, const std::string& host) {
    std::size_t start = 0;
    for (std::size_t i = 0; i <= host.size(); ++i) {
        if (i != host.size() && host[i] != '.') { continue; }
        out.push_back(static_cast<std::uint8_t>(i - start));
        for (std::size_t j = start; j < i; ++j) {
            out.push_back(static_cast<std::uint8_t>(host[j]));
        }
        start = i + 1;
    }
    out.push_back(0);
}

/// A record: a name pointer to offset 12, then type/class/ttl/rdata.
void aRecord(std::vector<std::uint8_t>& out, std::uint32_t packed) {
    out.push_back(0xC0);
    out.push_back(0x0C);
    u16(out, 1);   // A
    u16(out, 1);   // IN
    u16(out, 0);
    u16(out, 60);  // ttl
    u16(out, 4);   // rdlength
    out.push_back(static_cast<std::uint8_t>((packed >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((packed >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((packed >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(packed & 0xFFu));
}

std::vector<std::uint8_t> replyFor(const std::string& host, std::uint16_t id,
                                   std::uint32_t packed) {
    std::vector<std::uint8_t> out = header(id, 1);
    name(out, host);
    u16(out, 1);
    u16(out, 1);
    aRecord(out, packed);
    return out;
}

/// 192.0.2.1 in network byte order, which is what parse reports.
constexpr std::uint32_t kExpected = 0x010200C0u;

}  // namespace

STIPPLE_TEST(Dns, BuildsAQueryThatLooksLikeOne) {
    std::uint8_t query[kMaxMessageBytes];
    const std::size_t size = build("pool.ntp.org", 0xBEEF, query, sizeof(query));
    STIPPLE_REQUIRE(size > 0);

    STIPPLE_CHECK_EQ(static_cast<int>(query[0]), 0xBE);
    STIPPLE_CHECK_EQ(static_cast<int>(query[1]), 0xEF);
    // Recursion desired: this device asks a resolver to walk the tree, it is
    // not one itself.
    STIPPLE_CHECK_EQ(static_cast<int>(query[2]), 0x01);
    STIPPLE_CHECK_EQ(static_cast<int>(query[5]), 1);  // one question

    // pool(4) ntp(3) org(3) encoded with a length byte each, then root.
    STIPPLE_CHECK_EQ(static_cast<int>(query[12]), 4);
    STIPPLE_CHECK_EQ(size, std::size_t{12 + 14 + 4});
}

STIPPLE_TEST(Dns, RefusesNamesItCannotEncode) {
    std::uint8_t query[kMaxMessageBytes];
    STIPPLE_CHECK_EQ(build("", 1, query, sizeof(query)), std::size_t{0});
    STIPPLE_CHECK_EQ(build("a..b", 1, query, sizeof(query)), std::size_t{0});
    STIPPLE_CHECK_EQ(build(".leading", 1, query, sizeof(query)), std::size_t{0});
    // A label over 63 bytes.
    STIPPLE_CHECK_EQ(build(std::string(64, 'a'), 1, query, sizeof(query)), std::size_t{0});
    // A name over 255 bytes.
    STIPPLE_CHECK_EQ(build(std::string(300, 'a'), 1, query, sizeof(query)), std::size_t{0});
    // And a buffer too small to hold the result.
    STIPPLE_CHECK_EQ(build("example.com", 1, query, 8), std::size_t{0});
}

STIPPLE_TEST(Dns, ATrailingDotIsFine) {
    std::uint8_t query[kMaxMessageBytes];
    STIPPLE_CHECK(build("example.com.", 1, query, sizeof(query)) > 0);
}

STIPPLE_TEST(Dns, ReadsAnAddressBack) {
    const std::vector<std::uint8_t> reply = replyFor("example.com", 0x1234, 0xC0000201u);
    std::uint32_t address = 0;
    STIPPLE_CHECK(parse(reply.data(), reply.size(), 0x1234, address) == Result::Ok);
    STIPPLE_CHECK_EQ(address, kExpected);
}

STIPPLE_TEST(Dns, AReplyToADifferentQuestionIsRefused) {
    // Without this check any packet arriving on the socket could answer a
    // question it was never asked.
    const std::vector<std::uint8_t> reply = replyFor("example.com", 0x1234, 0xC0000201u);
    std::uint32_t address = 0;
    STIPPLE_CHECK(parse(reply.data(), reply.size(), 0x9999, address) == Result::WrongId);
    STIPPLE_CHECK_EQ(address, 0u);
}

STIPPLE_TEST(Dns, ReportsWhatTheServerSaid) {
    std::uint32_t address = 0;

    std::vector<std::uint8_t> missing = header(7, 0, 0x81, 0x83);  // NXDOMAIN
    name(missing, "nope.example");
    u16(missing, 1);
    u16(missing, 1);
    STIPPLE_CHECK(parse(missing.data(), missing.size(), 7, address) == Result::NoSuchName);

    std::vector<std::uint8_t> failed = header(8, 0, 0x81, 0x82);  // SERVFAIL
    name(failed, "nope.example");
    u16(failed, 1);
    u16(failed, 1);
    STIPPLE_CHECK(parse(failed.data(), failed.size(), 8, address) == Result::ServerFailure);

    std::vector<std::uint8_t> cut = header(9, 1, 0x83, 0x80);  // truncation bit
    STIPPLE_CHECK(parse(cut.data(), cut.size(), 9, address) == Result::Truncated);
}

STIPPLE_TEST(Dns, AnAnswerWithNoAddressIsNotAnError) {
    // A name that exists with only IPv6 records is a real state, and a
    // different one from "no such host".
    std::vector<std::uint8_t> reply = header(11, 1);
    name(reply, "example.com");
    u16(reply, 1);
    u16(reply, 1);
    reply.push_back(0xC0);
    reply.push_back(0x0C);
    u16(reply, 28);  // AAAA
    u16(reply, 1);
    u16(reply, 0);
    u16(reply, 60);
    u16(reply, 16);
    for (int i = 0; i < 16; ++i) { reply.push_back(0); }

    std::uint32_t address = 0;
    STIPPLE_CHECK(parse(reply.data(), reply.size(), 11, address) == Result::NoAddress);
}

STIPPLE_TEST(Dns, SkipsPastACnameToTheAddress) {
    // A recursive resolver puts the target's A record in the same reply, so
    // the chain never has to be walked by hand.
    std::vector<std::uint8_t> reply = header(12, 2);
    name(reply, "www.example.com");
    u16(reply, 1);
    u16(reply, 1);

    reply.push_back(0xC0);
    reply.push_back(0x0C);
    u16(reply, 5);   // CNAME
    u16(reply, 1);
    u16(reply, 0);
    u16(reply, 60);
    std::vector<std::uint8_t> target;
    name(target, "example.com");
    u16(reply, static_cast<std::uint16_t>(target.size()));
    reply.insert(reply.end(), target.begin(), target.end());

    aRecord(reply, 0xC0000201u);

    std::uint32_t address = 0;
    STIPPLE_CHECK(parse(reply.data(), reply.size(), 12, address) == Result::Ok);
    STIPPLE_CHECK_EQ(address, kExpected);
}

STIPPLE_TEST(Dns, ANameThatPointsAtItselfDoesNotHang) {
    // The whole reason the jump count exists. A reply whose name is a pointer
    // to its own offset would loop for ever in a naive reader - and this runs
    // on the thread that draws the panel.
    std::vector<std::uint8_t> reply = header(13, 1);
    name(reply, "example.com");
    u16(reply, 1);
    u16(reply, 1);

    // The answer's name points at itself.
    const std::size_t here = reply.size();
    reply.push_back(0xC0);
    reply.push_back(static_cast<std::uint8_t>(here));
    u16(reply, 1);
    u16(reply, 1);
    u16(reply, 0);
    u16(reply, 60);
    u16(reply, 4);
    for (int i = 0; i < 4; ++i) { reply.push_back(1); }

    std::uint32_t address = 0;
    STIPPLE_CHECK(parse(reply.data(), reply.size(), 13, address) == Result::Malformed);
}

STIPPLE_TEST(Dns, TwoNamesPointingAtEachOtherDoNotHang) {
    std::vector<std::uint8_t> reply = header(14, 1);
    name(reply, "a.example");
    u16(reply, 1);
    u16(reply, 1);

    const std::size_t first = reply.size();
    reply.push_back(0xC0);
    reply.push_back(static_cast<std::uint8_t>(first + 2));
    reply.push_back(0xC0);
    reply.push_back(static_cast<std::uint8_t>(first));

    std::uint32_t address = 0;
    STIPPLE_CHECK(parse(reply.data(), reply.size(), 14, address) == Result::Malformed);
}

STIPPLE_TEST(Dns, EveryTruncationOfAGoodReplyIsRefusedRatherThanRead) {
    // A poor fuzzer, and worth more than it looks: every prefix of a valid
    // message is a plausible short read from a socket, and any one of them
    // reading past its own end would be an out-of-bounds read on untrusted
    // input. Under ASan in CI, this test is the one that would say so.
    const std::vector<std::uint8_t> reply = replyFor("example.com", 0x2222, 0xC0000201u);

    for (std::size_t cut = 0; cut < reply.size(); ++cut) {
        std::uint32_t address = 0;
        const Result result = parse(reply.data(), cut, 0x2222, address);
        STIPPLE_CHECK(result != Result::Ok);
    }

    // And the whole thing still resolves, so the loop above was not passing
    // because the fixture is broken.
    std::uint32_t address = 0;
    STIPPLE_CHECK(parse(reply.data(), reply.size(), 0x2222, address) == Result::Ok);
}

STIPPLE_TEST(Dns, RubbishIsRefused) {
    std::uint32_t address = 0;
    STIPPLE_CHECK(parse(nullptr, 40, 1, address) == Result::Malformed);

    std::vector<std::uint8_t> noise(64, 0xEE);
    STIPPLE_CHECK(parse(noise.data(), noise.size(), 1, address) != Result::Ok);

    // Longer than a UDP DNS message is allowed to be.
    std::vector<std::uint8_t> huge(kMaxMessageBytes + 1, 0);
    STIPPLE_CHECK(parse(huge.data(), huge.size(), 0, address) == Result::Malformed);
}

STIPPLE_TEST(Dns, EveryResultHasWordsForIt) {
    for (const Result result : {Result::Ok, Result::Malformed, Result::WrongId,
                                Result::NoSuchName, Result::NoAddress, Result::Truncated,
                                Result::ServerFailure}) {
        const std::string words = stipple::net::dns::describe(result);
        STIPPLE_CHECK(!words.empty());
        STIPPLE_CHECK(words != "unrecognised");
    }
}
