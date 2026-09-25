// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/apps/StopwatchApp.h"

#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/text/Text.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::apps::formatElapsed;
using stipple::apps::renderStopwatch;
using stipple::apps::Stopwatch;
using stipple::apps::StopwatchStyle;

STIPPLE_TEST(Stopwatch, StartsReadyAtZero) {
    Stopwatch watch;
    STIPPLE_CHECK(watch.state() == Stopwatch::State::Ready);
    STIPPLE_CHECK(!watch.running());
    STIPPLE_CHECK_EQ(watch.elapsedMillis(500000), 0u);
}

STIPPLE_TEST(Stopwatch, OneButtonGoesStartStopReset) {
    Stopwatch watch;
    STIPPLE_CHECK(watch.press(1000) == Stopwatch::State::Running);
    STIPPLE_CHECK(watch.press(4000) == Stopwatch::State::Stopped);
    STIPPLE_CHECK(watch.press(4500) == Stopwatch::State::Ready);
    // And round again, because a stopwatch you can only use once is a clock.
    STIPPLE_CHECK(watch.press(5000) == Stopwatch::State::Running);
}

STIPPLE_TEST(Stopwatch, CountsWhileRunning) {
    Stopwatch watch;
    watch.press(1000);
    STIPPLE_CHECK_EQ(watch.elapsedMillis(1000), 0u);
    STIPPLE_CHECK_EQ(watch.elapsedMillis(3500), 2500u);
}

STIPPLE_TEST(Stopwatch, HoldsTheTimeWhenStopped) {
    Stopwatch watch;
    watch.press(1000);
    watch.press(9000);
    // The clock keeps running; the stopwatch must not.
    STIPPLE_CHECK_EQ(watch.elapsedMillis(9000), 8000u);
    STIPPLE_CHECK_EQ(watch.elapsedMillis(90000), 8000u);
}

STIPPLE_TEST(Stopwatch, ResetGoesBackToZero) {
    Stopwatch watch;
    watch.press(1000);
    watch.press(9000);
    watch.press(9500);
    STIPPLE_CHECK_EQ(watch.elapsedMillis(20000), 0u);
    STIPPLE_CHECK(watch.state() == Stopwatch::State::Ready);
}

STIPPLE_TEST(Stopwatch, ARestartDoesNotAddToTheOldTime) {
    Stopwatch watch;
    watch.press(1000);
    watch.press(6000);          // 5s on the clock
    watch.press(6100);          // reset
    watch.press(10000);         // start again
    STIPPLE_CHECK_EQ(watch.elapsedMillis(12000), 2000u);
}

STIPPLE_TEST(Stopwatch, SurvivesTheClockSteppingBackwards) {
    Stopwatch watch;
    watch.press(10000000);
    // Must not underflow into fifty days of elapsed time.
    STIPPLE_CHECK_EQ(watch.elapsedMillis(5000), 0u);
}

STIPPLE_TEST(Stopwatch, KeepsCountingWhileSomethingElseIsOnScreen) {
    // The stopwatch lives on the host, not in the app, precisely so this
    // holds: the carousel moving on must not stop or reset it.
    Stopwatch watch;
    watch.press(1000);
    for (std::uint64_t now = 1000; now <= 61000; now += 1000) {
        STIPPLE_CHECK(watch.running());
    }
    STIPPLE_CHECK_EQ(watch.elapsedMillis(61000), 60000u);
}

STIPPLE_TEST(Stopwatch, FormatsMinutesSecondsAndTenths) {
    STIPPLE_CHECK_EQ(formatElapsed(0), std::string("00:00.0"));
    STIPPLE_CHECK_EQ(formatElapsed(1500), std::string("00:01.5"));
    STIPPLE_CHECK_EQ(formatElapsed(59900), std::string("00:59.9"));
    STIPPLE_CHECK_EQ(formatElapsed(60000), std::string("01:00.0"));
    STIPPLE_CHECK_EQ(formatElapsed(3599900), std::string("59:59.9"));
}

STIPPLE_TEST(Stopwatch, DropsTenthsPastAnHour) {
    // 1:02:03.4 is nine characters and does not fit across 52 pixels, and
    // tenths stop meaning anything at that scale anyway.
    STIPPLE_CHECK_EQ(formatElapsed(3600000), std::string("1:00:00"));
    STIPPLE_CHECK_EQ(formatElapsed(3723400), std::string("1:02:03"));
    STIPPLE_CHECK_EQ(formatElapsed(36000000), std::string("10:00:00"));
}

STIPPLE_TEST(Stopwatch, EveryFormatFitsThePanel) {
    // The whole point of the hour switch. Checked rather than argued: a
    // string one pixel too wide is clipped on hardware and nowhere else.
    const stipple::text::BitmapFont& font = stipple::text::font5x7();
    const std::uint64_t moments[] = {0,       999,      59999,    60000,
                                     3599999, 3600000,  3723400,  36000000};
    for (const std::uint64_t at : moments) {
        const std::string text = formatElapsed(at);
        // +1 for the emboldening, which draws one column wider.
        const int width = stipple::text::measureLine(text, font) + 1;
        STIPPLE_CHECK(width <= Framebuffer::kWidth);
    }
}

STIPPLE_TEST(Stopwatch, ReadyMatchesGolden) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    Stopwatch watch;
    renderStopwatch(canvas, watch, 1000, StopwatchStyle{});
    STIPPLE_CHECK_GOLDEN("stopwatch-ready", framebuffer);
}

STIPPLE_TEST(Stopwatch, RunningMatchesGolden) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    Stopwatch watch;
    watch.press(1000);
    // 12.3 seconds in, and 300 ms into the current second so the sweep is
    // partly drawn rather than at either end.
    renderStopwatch(canvas, watch, 13300, StopwatchStyle{});
    STIPPLE_CHECK_GOLDEN("stopwatch-running", framebuffer);
}

STIPPLE_TEST(Stopwatch, StoppedMatchesGolden) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    Stopwatch watch;
    watch.press(1000);
    watch.press(13300);
    renderStopwatch(canvas, watch, 20000, StopwatchStyle{});
    STIPPLE_CHECK_GOLDEN("stopwatch-stopped", framebuffer);
}

STIPPLE_TEST(Stopwatch, TheSweepOnlyDrawsWhileRunning) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    Stopwatch watch;
    watch.press(1000);
    watch.press(13300);

    renderStopwatch(canvas, watch, 20000, StopwatchStyle{});

    // A sweep left behind on a stopped stopwatch would say time is still
    // passing, which is the one thing it exists to deny.
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
        STIPPLE_CHECK(framebuffer.at(x, Framebuffer::kHeight - 1) == stipple::colors::kBlack);
    }
}
