// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/core/Rgb.h"

#include "support/TestFramework.h"

using notrix::fromPacked;
using notrix::Rgb;
using notrix::rgb;
using notrix::scale;
using notrix::toPacked;
namespace colors = notrix::colors;

NOTRIX_TEST(Rgb, ComponentsRoundTripThroughPacking) {
    const Rgb color{18, 52, 86};
    NOTRIX_CHECK_EQ(toPacked(color), 0x123456u);
    NOTRIX_CHECK_EQ(fromPacked(0x123456u), color);
}

NOTRIX_TEST(Rgb, PackingIsRgbOrderNotBgr) {
    // Guards against the classic channel-swap bug when frames reach the panel.
    NOTRIX_CHECK_EQ(toPacked(colors::kRed), 0xFF0000u);
    NOTRIX_CHECK_EQ(toPacked(colors::kGreen), 0x00FF00u);
    NOTRIX_CHECK_EQ(toPacked(colors::kBlue), 0x0000FFu);
}

NOTRIX_TEST(Rgb, HelperClampsOutOfRangeComponents) {
    NOTRIX_CHECK_EQ(rgb(-40, 128, 999), Rgb({0, 128, 255}));
}

NOTRIX_TEST(Rgb, ScaleByFullFactorIsIdentity) {
    NOTRIX_CHECK_EQ(scale(colors::kWhite, 255), colors::kWhite);
    NOTRIX_CHECK_EQ(scale(colors::kOrange, 255), colors::kOrange);
}

NOTRIX_TEST(Rgb, ScaleByZeroIsBlack) {
    NOTRIX_CHECK_EQ(scale(colors::kWhite, 0), colors::kBlack);
}

NOTRIX_TEST(Rgb, ScaleIsMonotonicAndNeverOverflows) {
    // Brightness control runs through scale() on every frame; an overflow here
    // would show up as bright pixels at low brightness.
    Rgb previous = colors::kBlack;
    for (int factor = 0; factor <= 255; ++factor) {
        const Rgb current = scale(colors::kWhite, static_cast<std::uint8_t>(factor));
        NOTRIX_CHECK(current.r >= previous.r);
        NOTRIX_CHECK(current.r <= 255);
        previous = current;
    }
    NOTRIX_CHECK_EQ(previous, colors::kWhite);
}

NOTRIX_TEST(Rgb, DefaultConstructedIsBlack) {
    NOTRIX_CHECK_EQ(Rgb(), colors::kBlack);
}
