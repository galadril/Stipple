// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/text/Utf8.h"

#include <string>

#include "support/TestFramework.h"

using stipple::text::countCodepoints;
using stipple::text::DecodedChar;
using stipple::text::decodeUtf8;
using stipple::text::kReplacementChar;

namespace {

/// Decode the whole string and return the codepoints, so malformed input can be
/// checked for the substitutions it produces as well as the length it consumes.
std::u32string decodeAll(std::string_view text) {
    std::u32string out;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const DecodedChar decoded = decodeUtf8(text, offset);
        out.push_back(decoded.codepoint);
        offset += decoded.size;
    }
    return out;
}

}  // namespace

STIPPLE_TEST(Utf8, DecodesAscii) {
    const DecodedChar decoded = decodeUtf8("A", 0);
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decoded.codepoint), 0x41u);
    STIPPLE_CHECK_EQ(decoded.size, std::size_t(1));
}

STIPPLE_TEST(Utf8, DecodesTwoByteSequence) {
    // U+00B0 DEGREE SIGN — the one non-ASCII character the blueprint calls out
    // by name, because a clock without it cannot show a temperature.
    const DecodedChar decoded = decodeUtf8("\xC2\xB0", 0);
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decoded.codepoint), 0xB0u);
    STIPPLE_CHECK_EQ(decoded.size, std::size_t(2));
}

STIPPLE_TEST(Utf8, DecodesThreeByteSequence) {
    const DecodedChar decoded = decodeUtf8("\xE2\x82\xAC", 0);  // U+20AC EURO SIGN
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decoded.codepoint), 0x20ACu);
    STIPPLE_CHECK_EQ(decoded.size, std::size_t(3));
}

STIPPLE_TEST(Utf8, DecodesFourByteSequence) {
    const DecodedChar decoded = decodeUtf8("\xF0\x9F\x98\x80", 0);  // U+1F600
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decoded.codepoint), 0x1F600u);
    STIPPLE_CHECK_EQ(decoded.size, std::size_t(4));
}

STIPPLE_TEST(Utf8, RejectsOverlongEncodings) {
    // 0xC0 0x80 is a two-byte encoding of NUL — the classic way to smuggle a
    // character past a naive filter.
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decodeUtf8("\xC0\x80", 0).codepoint),
                    static_cast<std::uint32_t>(kReplacementChar));
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decodeUtf8("\xE0\x80\xAF", 0).codepoint),
                    static_cast<std::uint32_t>(kReplacementChar));
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decodeUtf8("\xF0\x80\x80\xAF", 0).codepoint),
                    static_cast<std::uint32_t>(kReplacementChar));
}

STIPPLE_TEST(Utf8, RejectsLoneSurrogates) {
    // U+D800 encoded as UTF-8 (CESU-8 style) is not valid UTF-8.
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decodeUtf8("\xED\xA0\x80", 0).codepoint),
                    static_cast<std::uint32_t>(kReplacementChar));
}

STIPPLE_TEST(Utf8, RejectsCodepointsAboveUnicodeRange) {
    // U+110000, one past the top of the range.
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decodeUtf8("\xF4\x90\x80\x80", 0).codepoint),
                    static_cast<std::uint32_t>(kReplacementChar));
}

STIPPLE_TEST(Utf8, RejectsTruncatedSequences) {
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decodeUtf8("\xC2", 0).codepoint),
                    static_cast<std::uint32_t>(kReplacementChar));
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decodeUtf8("\xE2\x82", 0).codepoint),
                    static_cast<std::uint32_t>(kReplacementChar));
}

STIPPLE_TEST(Utf8, RejectsStrayContinuationByte) {
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decodeUtf8("\x80", 0).codepoint),
                    static_cast<std::uint32_t>(kReplacementChar));
}

STIPPLE_TEST(Utf8, MalformedInputConsumesExactlyOneByte) {
    // Forward progress is the property that keeps a bad byte from hanging the
    // render loop or eating the rest of the string.
    STIPPLE_CHECK_EQ(decodeUtf8("\xFF", 0).size, std::size_t(1));
    STIPPLE_CHECK_EQ(decodeUtf8("\x80", 0).size, std::size_t(1));
    STIPPLE_CHECK_EQ(decodeUtf8("\xC0\x80", 0).size, std::size_t(1));
}

STIPPLE_TEST(Utf8, RecoversAfterAMalformedByte) {
    // A bad byte costs one replacement character, not the rest of the text.
    const std::u32string decoded = decodeAll("A\xFF" "B");
    STIPPLE_CHECK_EQ(decoded.size(), std::size_t(3));
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decoded[0]), 0x41u);
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decoded[1]), static_cast<std::uint32_t>(kReplacementChar));
    STIPPLE_CHECK_EQ(static_cast<std::uint32_t>(decoded[2]), 0x42u);
}

STIPPLE_TEST(Utf8, NeverStallsOnAnyByteValue) {
    // Every possible single byte must still advance the cursor.
    for (int value = 0; value < 256; ++value) {
        const std::string input(1, static_cast<char>(value));
        STIPPLE_CHECK(decodeUtf8(input, 0).size >= 1);
    }
}

STIPPLE_TEST(Utf8, CountsCodepointsNotBytes) {
    STIPPLE_CHECK_EQ(countCodepoints(""), std::size_t(0));
    STIPPLE_CHECK_EQ(countCodepoints("abc"), std::size_t(3));
    // "21.4°C" is 7 bytes but 6 codepoints — the degree sign takes two bytes.
    STIPPLE_CHECK_EQ(countCodepoints("21.4\xC2\xB0" "C"), std::size_t(6));
}

STIPPLE_TEST(Utf8, OffsetPastEndIsSafe) {
    STIPPLE_CHECK_EQ(decodeUtf8("abc", 99).size, std::size_t(1));
    STIPPLE_CHECK_EQ(decodeUtf8("", 0).size, std::size_t(1));
}
