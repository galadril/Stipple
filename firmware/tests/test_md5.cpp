// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/core/Md5.h"

#include <string>

#include "support/TestFramework.h"

using stipple::Md5;

namespace {

std::string hex(const std::string& text) {
    return Md5::hex(text.data(), text.size());
}

}  // namespace

STIPPLE_TEST(Md5, MatchesTheVectorsInRfc1321) {
    // Straight from the RFC's own test suite. An implementation that passes
    // these and nothing else is still worth more than one checked by eye.
    STIPPLE_CHECK_EQ(hex(""), std::string("d41d8cd98f00b204e9800998ecf8427e"));
    STIPPLE_CHECK_EQ(hex("a"), std::string("0cc175b9c0f1b6a831c399e269772661"));
    STIPPLE_CHECK_EQ(hex("abc"), std::string("900150983cd24fb0d6963f7d28e17f72"));
    STIPPLE_CHECK_EQ(hex("message digest"),
                    std::string("f96b697d7cb7938d525a2f31aaf161d0"));
    STIPPLE_CHECK_EQ(hex("abcdefghijklmnopqrstuvwxyz"),
                    std::string("c3fcd3d76192e4007dfb496cca67e13b"));
    STIPPLE_CHECK_EQ(
        hex("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"),
        std::string("d174ab98d277d9f5a5611c2c9f419d9f"));
    STIPPLE_CHECK_EQ(
        hex("123456789012345678901234567890123456789012345678901234567890"
            "12345678901234567890"),
        std::string("57edf4a22be3c955ac49da2e2107b67a"));
}

STIPPLE_TEST(Md5, HandlesTheBlockBoundariesThatTripImplementationsUp) {
    // 55, 56 and 64 bytes are where the padding decides whether it needs one
    // extra block. An implementation that is wrong is usually wrong here and
    // nowhere else.
    for (std::size_t length : {std::size_t(54), std::size_t(55), std::size_t(56),
                               std::size_t(57), std::size_t(63), std::size_t(64),
                               std::size_t(65), std::size_t(119), std::size_t(120)}) {
        const std::string block(length, 'x');
        Md5 incremental;
        for (const char c : block) {
            incremental.update(&c, 1);
        }
        STIPPLE_CHECK_EQ(incremental.finishHex(), Md5::hex(block.data(), block.size()));
    }
}

STIPPLE_TEST(Md5, FeedingItInPiecesGivesTheSameAnswer) {
    // The whole reason this is incremental: a multi-megabyte firmware image
    // is hashed without a second copy of it in RAM.
    const std::string whole(10000, 'q');

    Md5 oneGo;
    oneGo.update(whole.data(), whole.size());

    Md5 inPieces;
    std::size_t at = 0;
    for (std::size_t step = 1; at < whole.size(); step = (step * 3) % 997 + 1) {
        const std::size_t take = (at + step > whole.size()) ? whole.size() - at : step;
        inPieces.update(whole.data() + at, take);
        at += take;
    }

    STIPPLE_CHECK_EQ(inPieces.finishHex(), oneGo.finishHex());
}

STIPPLE_TEST(Md5, HandlesBytesThatAreNotText) {
    std::uint8_t raw[256];
    for (int i = 0; i < 256; ++i) {
        raw[i] = static_cast<std::uint8_t>(i);
    }
    STIPPLE_CHECK_EQ(Md5::hex(raw, sizeof(raw)),
                    std::string("e2c865db4162bed963bfaa9ef6ac18f0"));
}

STIPPLE_TEST(Md5, ResetMakesItReusable) {
    Md5 md5;
    md5.update("abc", 3);
    STIPPLE_CHECK_EQ(md5.finishHex(), std::string("900150983cd24fb0d6963f7d28e17f72"));

    md5.reset();
    md5.update("a", 1);
    STIPPLE_CHECK_EQ(md5.finishHex(), std::string("0cc175b9c0f1b6a831c399e269772661"));
}
