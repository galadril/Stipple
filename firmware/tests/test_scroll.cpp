// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/text/Scroll.h"

#include "stipple/graphics/Canvas.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::Rect;
using stipple::text::drawScrolling;
using stipple::text::font5x7;
using stipple::text::measureLine;
using stipple::text::ScrollConfig;
using stipple::text::ScrollMode;
using stipple::text::scrollModeFromName;
using stipple::text::scrollOffset;
using stipple::text::scrolls;
using stipple::text::TextStyle;
namespace colors = stipple::colors;

namespace {

int countLit(const Framebuffer& framebuffer) {
    int count = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (framebuffer.at(x, y) != colors::kBlack) {
                ++count;
            }
        }
    }
    return count;
}

TextStyle plainStyle() {
    TextStyle style;
    style.font = &font5x7();
    style.color = colors::kWhite;
    return style;
}

}  // namespace

// --- mode selection ----------------------------------------------------------

STIPPLE_TEST(Scroll, ParsesModeNames) {
    STIPPLE_CHECK(scrollModeFromName("auto") == ScrollMode::Auto);
    STIPPLE_CHECK(scrollModeFromName("marquee") == ScrollMode::Marquee);
    STIPPLE_CHECK(scrollModeFromName("bounce") == ScrollMode::Bounce);
    STIPPLE_CHECK(scrollModeFromName("none") == ScrollMode::None);
    STIPPLE_CHECK(scrollModeFromName("nonsense") == ScrollMode::None);
    STIPPLE_CHECK(scrollModeFromName("") == ScrollMode::None);
}

STIPPLE_TEST(Scroll, AutoOnlyScrollsWhenTextOverflows) {
    STIPPLE_CHECK_FALSE(scrolls(ScrollMode::Auto, 20, 52));
    STIPPLE_CHECK_FALSE(scrolls(ScrollMode::Auto, 52, 52));  // exactly fits
    STIPPLE_CHECK(scrolls(ScrollMode::Auto, 53, 52));
}

STIPPLE_TEST(Scroll, MarqueeScrollsEvenWhenItFits) {
    // The distinction between Auto and Marquee: one is a fallback, the other is
    // explicitly requested motion.
    STIPPLE_CHECK(scrolls(ScrollMode::Marquee, 20, 52));
}

STIPPLE_TEST(Scroll, NoneNeverScrolls) {
    STIPPLE_CHECK_FALSE(scrolls(ScrollMode::None, 500, 10));
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::None, 500, 10, 99999), 0);
}

STIPPLE_TEST(Scroll, BounceNeedsOverflow) {
    STIPPLE_CHECK_FALSE(scrolls(ScrollMode::Bounce, 30, 52));
    STIPPLE_CHECK(scrolls(ScrollMode::Bounce, 90, 52));
}

// --- marquee -----------------------------------------------------------------

STIPPLE_TEST(Scroll, MarqueeHoldsBeforeMoving) {
    // The start of the message must be readable before it slides away.
    ScrollConfig config;
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Auto, 100, 52, 0, config), 0);
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Auto, 100, 52, config.startDelayMillis, config), 0);
    STIPPLE_CHECK(scrollOffset(ScrollMode::Auto, 100, 52, config.startDelayMillis + 500, config) < 0);
}

STIPPLE_TEST(Scroll, MarqueeMovesLeftOverTime) {
    ScrollConfig config;
    const std::uint64_t start = config.startDelayMillis;

    const int early = scrollOffset(ScrollMode::Auto, 100, 52, start + 1000, config);
    const int later = scrollOffset(ScrollMode::Auto, 100, 52, start + 2000, config);

    STIPPLE_CHECK(early < 0);
    STIPPLE_CHECK(later < early);  // further left
}

STIPPLE_TEST(Scroll, MarqueeSpeedMatchesConfiguration) {
    ScrollConfig config;
    config.pixelsPerSecond = 20;
    config.startDelayMillis = 0;

    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Auto, 1000, 52, 1000, config), -20);
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Auto, 1000, 52, 2000, config), -40);
}

STIPPLE_TEST(Scroll, MarqueeWrapsAndNeverRunsAway) {
    // Offset must stay inside one period, or a long-running clock would push the
    // text arbitrarily far off screen.
    ScrollConfig config;
    config.startDelayMillis = 0;

    const int textWidth = 100;
    const int period = textWidth + config.gapPixels;

    for (std::uint64_t t = 0; t < 600000; t += 997) {
        const int offset = scrollOffset(ScrollMode::Auto, textWidth, 52, t, config);
        STIPPLE_CHECK(offset <= 0);
        STIPPLE_CHECK(offset > -period);
    }
}

STIPPLE_TEST(Scroll, MarqueeReturnsToTheStartAfterOnePeriod) {
    ScrollConfig config;
    config.startDelayMillis = 0;
    config.pixelsPerSecond = 10;
    config.gapPixels = 10;

    const int textWidth = 90;                  // period = 100 px
    const std::uint64_t periodMillis = 10000;  // 100 px at 10 px/s

    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Auto, textWidth, 52, 0, config), 0);
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Auto, textWidth, 52, periodMillis, config), 0);
}

// --- bounce ------------------------------------------------------------------

STIPPLE_TEST(Scroll, BounceStopsAtTheEndNotBeyond) {
    ScrollConfig config;
    config.startDelayMillis = 0;
    config.pixelsPerSecond = 10;

    const int textWidth = 100;
    const int boxWidth = 52;
    const int travel = textWidth - boxWidth;  // 48 px

    // Well past the outbound leg, during the end hold.
    const int offset = scrollOffset(ScrollMode::Bounce, textWidth, boxWidth, 5000, config);
    STIPPLE_CHECK_EQ(offset, -travel);
}

STIPPLE_TEST(Scroll, BounceNeverExceedsItsTravel) {
    // Overshooting would clip the last characters, which is exactly the bug
    // scrolling is meant to fix.
    ScrollConfig config;
    const int textWidth = 140;
    const int boxWidth = 52;
    const int travel = textWidth - boxWidth;

    for (std::uint64_t t = 0; t < 300000; t += 313) {
        const int offset = scrollOffset(ScrollMode::Bounce, textWidth, boxWidth, t, config);
        STIPPLE_CHECK(offset <= 0);
        STIPPLE_CHECK(offset >= -travel);
    }
}

STIPPLE_TEST(Scroll, BounceReturnsToTheStart) {
    ScrollConfig config;
    config.startDelayMillis = 0;
    config.endHoldMillis = 0;
    config.pixelsPerSecond = 10;

    const int textWidth = 102;
    const int boxWidth = 52;
    // travel = 50 px, 5000 ms each way, so a full cycle is 10000 ms.
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Bounce, textWidth, boxWidth, 0, config), 0);
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Bounce, textWidth, boxWidth, 5000, config), -50);
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Bounce, textWidth, boxWidth, 10000, config), 0);
}

STIPPLE_TEST(Scroll, BounceIsPeriodic) {
    ScrollConfig config;
    const int textWidth = 120;

    for (std::uint64_t t = 0; t < 20000; t += 251) {
        const int first = scrollOffset(ScrollMode::Bounce, textWidth, 52, t, config);
        // One cycle: delay + travel + hold + travel + hold.
        const std::uint64_t travelMillis =
            static_cast<std::uint64_t>(textWidth - 52) * 1000u /
            static_cast<std::uint64_t>(config.pixelsPerSecond);
        const std::uint64_t cycle = config.startDelayMillis + 2u * travelMillis +
                                    2u * config.endHoldMillis;
        const int second = scrollOffset(ScrollMode::Bounce, textWidth, 52, t + cycle, config);
        STIPPLE_CHECK_EQ(first, second);
    }
}

// --- degenerate input --------------------------------------------------------

STIPPLE_TEST(Scroll, ZeroSpeedDoesNotDivideByZero) {
    ScrollConfig config;
    config.pixelsPerSecond = 0;
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Auto, 100, 52, 50000, config), 0);
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Bounce, 100, 52, 50000, config), 0);
}

STIPPLE_TEST(Scroll, ZeroGapStillAdvances) {
    ScrollConfig config;
    config.gapPixels = 0;
    config.startDelayMillis = 0;
    STIPPLE_CHECK(scrollOffset(ScrollMode::Auto, 100, 52, 3000, config) < 0);
}

STIPPLE_TEST(Scroll, EmptyTextDoesNotScroll) {
    STIPPLE_CHECK_FALSE(scrolls(ScrollMode::Marquee, 0, 52));
    STIPPLE_CHECK_EQ(scrollOffset(ScrollMode::Auto, 0, 52, 10000), 0);
}

// --- drawing -----------------------------------------------------------------

STIPPLE_TEST(Scroll, ShortTextIsDrawnNormallyAndStaysPut) {
    Framebuffer first;
    Framebuffer later;

    Canvas canvasA(first);
    drawScrolling(canvasA, "HI", Rect({0, 0, 52, 7}), plainStyle(), ScrollMode::Auto, 0);

    Canvas canvasB(later);
    drawScrolling(canvasB, "HI", Rect({0, 0, 52, 7}), plainStyle(), ScrollMode::Auto, 30000);

    STIPPLE_CHECK(first == later);
}

STIPPLE_TEST(Scroll, LongTextChangesOverTime) {
    const char* message = "This message is far too wide for the panel";

    Framebuffer early;
    Canvas canvasA(early);
    drawScrolling(canvasA, message, Rect({0, 0, 52, 7}), plainStyle(), ScrollMode::Auto, 0);

    Framebuffer later;
    Canvas canvasB(later);
    drawScrolling(canvasB, message, Rect({0, 0, 52, 7}), plainStyle(), ScrollMode::Auto, 4000);

    STIPPLE_CHECK(early != later);
    STIPPLE_CHECK(countLit(early) > 0);
    STIPPLE_CHECK(countLit(later) > 0);
}

STIPPLE_TEST(Scroll, ScrollingTextStaysInsideItsBox) {
    // Two text elements must be able to sit side by side while one scrolls.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const Rect box{10, 4, 20, 7};
    for (std::uint64_t t = 0; t < 30000; t += 250) {
        framebuffer.clear();
        drawScrolling(canvas, "A very long scrolling label", box, plainStyle(), ScrollMode::Auto,
                      t);

        for (int y = 0; y < Framebuffer::kHeight; ++y) {
            for (int x = 0; x < Framebuffer::kWidth; ++x) {
                if (framebuffer.at(x, y) != colors::kBlack) {
                    STIPPLE_CHECK(box.contains(x, y));
                }
            }
        }
    }
}

STIPPLE_TEST(Scroll, MarqueeIsNeverBlankMidWrap) {
    // Without drawing the repeat, the text would vanish between cycles.
    ScrollConfig config;
    config.startDelayMillis = 0;

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    for (std::uint64_t t = 0; t < 40000; t += 200) {
        framebuffer.clear();
        drawScrolling(canvas, "Scrolling status message", Rect({0, 0, 52, 7}), plainStyle(),
                      ScrollMode::Auto, t, config);
        STIPPLE_CHECK(countLit(framebuffer) > 0);
    }
}

STIPPLE_TEST(Scroll, MidScrollFrameMatchesGolden) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    ScrollConfig config;
    config.startDelayMillis = 0;
    config.pixelsPerSecond = 12;

    drawScrolling(canvas, "Living room", Rect({0, 4, 40, 7}), plainStyle(), ScrollMode::Auto, 2000,
                  config);

    STIPPLE_CHECK_GOLDEN("scroll-midway", framebuffer);
}
