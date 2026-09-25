// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/core/Rgb.h"

#include <string>

#include "support/TestFramework.h"

using stipple::fromPacked;
using stipple::Rgb;
using stipple::rgb;
using stipple::scale;
using stipple::toPacked;
namespace colors = stipple::colors;

STIPPLE_TEST(Rgb, ComponentsRoundTripThroughPacking) {
    const Rgb color{18, 52, 86};
    STIPPLE_CHECK_EQ(toPacked(color), 0x123456u);
    STIPPLE_CHECK_EQ(fromPacked(0x123456u), color);
}

STIPPLE_TEST(Rgb, PackingIsRgbOrderNotBgr) {
    // Guards against the classic channel-swap bug when frames reach the panel.
    STIPPLE_CHECK_EQ(toPacked(colors::kRed), 0xFF0000u);
    STIPPLE_CHECK_EQ(toPacked(colors::kGreen), 0x00FF00u);
    STIPPLE_CHECK_EQ(toPacked(colors::kBlue), 0x0000FFu);
}

STIPPLE_TEST(Rgb, HelperClampsOutOfRangeComponents) {
    STIPPLE_CHECK_EQ(rgb(-40, 128, 999), Rgb({0, 128, 255}));
}

STIPPLE_TEST(Rgb, ScaleByFullFactorIsIdentity) {
    STIPPLE_CHECK_EQ(scale(colors::kWhite, 255), colors::kWhite);
    STIPPLE_CHECK_EQ(scale(colors::kOrange, 255), colors::kOrange);
}

STIPPLE_TEST(Rgb, ScaleByZeroIsBlack) {
    STIPPLE_CHECK_EQ(scale(colors::kWhite, 0), colors::kBlack);
}

STIPPLE_TEST(Rgb, ScaleIsMonotonicAndNeverOverflows) {
    // Brightness control runs through scale() on every frame; an overflow here
    // would show up as bright pixels at low brightness.
    Rgb previous = colors::kBlack;
    for (int factor = 0; factor <= 255; ++factor) {
        const Rgb current = scale(colors::kWhite, static_cast<std::uint8_t>(factor));
        STIPPLE_CHECK(current.r >= previous.r);
        STIPPLE_CHECK(current.r <= 255);
        previous = current;
    }
    STIPPLE_CHECK_EQ(previous, colors::kWhite);
}

STIPPLE_TEST(Rgb, DefaultConstructedIsBlack) {
    STIPPLE_CHECK_EQ(Rgb(), colors::kBlack);
}

// --- hex text ----------------------------------------------------------------

STIPPLE_TEST(Rgb, ParsesHexWithAndWithoutHash) {
    Rgb color;
    STIPPLE_CHECK(stipple::parseHexColor("#FF8800", color));
    STIPPLE_CHECK_EQ(color, Rgb({255, 136, 0}));
    STIPPLE_CHECK(stipple::parseHexColor("ff8800", color));
    STIPPLE_CHECK_EQ(color, Rgb({255, 136, 0}));
}

STIPPLE_TEST(Rgb, RejectsAnythingThatIsNotSixHexDigits) {
    const char* bad[] = {"", "#", "#FFF", "#FFFFFFF", "FFFFFG", "#12345", "blue", "# FFFFF"};
    for (const char* text : bad) {
        Rgb color = colors::kOrange;
        STIPPLE_CHECK_FALSE(stipple::parseHexColor(text, color));
        // A rejected parse must not leave the caller holding a half-parsed value.
        STIPPLE_CHECK_EQ(color, colors::kOrange);
    }
}

STIPPLE_TEST(Rgb, HexRoundTripsForEveryChannelValue) {
    // Config and the API both round-trip colours through text; a formatter that
    // dropped a leading zero would corrupt a colour on every save.
    for (int value = 0; value <= 255; ++value) {
        const Rgb original = rgb(value, 255 - value, value / 2);
        char text[8];
        stipple::formatHexColor(original, text);

        Rgb parsed;
        STIPPLE_CHECK(stipple::parseHexColor(text, parsed));
        STIPPLE_CHECK_EQ(parsed, original);
    }
}

STIPPLE_TEST(Rgb, FormatsUppercaseWithHashAndTerminator) {
    char text[8];
    stipple::formatHexColor(Rgb({0, 190, 255}), text);
    STIPPLE_CHECK_EQ(std::string(text), std::string("#00BEFF"));
    STIPPLE_CHECK_EQ(text[7], '\0');
}
