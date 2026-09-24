// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/StopwatchApp.h"

#include "notrix/graphics/Canvas.h"
#include "notrix/graphics/Framebuffer.h"
#include "notrix/text/Text.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using notrix::Canvas;
using notrix::Framebuffer;
using notrix::apps::formatElapsed;
using notrix::apps::renderStopwatch;
using notrix::apps::Stopwatch;
using notrix::apps::StopwatchStyle;

NOTRIX_TEST(Stopwatch, StartsReadyAtZero) {
    Stopwatch watch;
    NOTRIX_CHECK(watch.state() == Stopwatch::State::Ready);
    NOTRIX_CHECK(!watch.running());
    NOTRIX_CHECK_EQ(watch.elapsedMillis(500000), 0u);
}

NOTRIX_TEST(Stopwatch, OneButtonGoesStartStopReset) {
    Stopwatch watch;
    NOTRIX_CHECK(watch.press(1000) == Stopwatch::State::Running);
    NOTRIX_CHECK(watch.press(4000) == Stopwatch::State::Stopped);
    NOTRIX_CHECK(watch.press(4500) == Stopwatch::State::Ready);
    // And round again, because a stopwatch you can only use once is a clock.
    NOTRIX_CHECK(watch.press(5000) == Stopwatch::State::Running);
}

NOTRIX_TEST(Stopwatch, CountsWhileRunning) {
    Stopwatch watch;
    watch.press(1000);
    NOTRIX_CHECK_EQ(watch.elapsedMillis(1000), 0u);
    NOTRIX_CHECK_EQ(watch.elapsedMillis(3500), 2500u);
}

NOTRIX_TEST(Stopwatch, HoldsTheTimeWhenStopped) {
    Stopwatch watch;
    watch.press(1000);
    watch.press(9000);
    // The clock keeps running; the stopwatch must not.
    NOTRIX_CHECK_EQ(watch.elapsedMillis(9000), 8000u);
    NOTRIX_CHECK_EQ(watch.elapsedMillis(90000), 8000u);
}

NOTRIX_TEST(Stopwatch, ResetGoesBackToZero) {
    Stopwatch watch;
    watch.press(1000);
    watch.press(9000);
    watch.press(9500);
    NOTRIX_CHECK_EQ(watch.elapsedMillis(20000), 0u);
    NOTRIX_CHECK(watch.state() == Stopwatch::State::Ready);
}

NOTRIX_TEST(Stopwatch, ARestartDoesNotAddToTheOldTime) {
    Stopwatch watch;
    watch.press(1000);
    watch.press(6000);          // 5s on the clock
    watch.press(6100);          // reset
    watch.press(10000);         // start again
    NOTRIX_CHECK_EQ(watch.elapsedMillis(12000), 2000u);
}

NOTRIX_TEST(Stopwatch, SurvivesTheClockSteppingBackwards) {
    Stopwatch watch;
    watch.press(10000000);
    // Must not underflow into fifty days of elapsed time.
    NOTRIX_CHECK_EQ(watch.elapsedMillis(5000), 0u);
}

NOTRIX_TEST(Stopwatch, KeepsCountingWhileSomethingElseIsOnScreen) {
    // The stopwatch lives on the host, not in the app, precisely so this
    // holds: the carousel moving on must not stop or reset it.
    Stopwatch watch;
    watch.press(1000);
    for (std::uint64_t now = 1000; now <= 61000; now += 1000) {
        NOTRIX_CHECK(watch.running());
    }
    NOTRIX_CHECK_EQ(watch.elapsedMillis(61000), 60000u);
}

NOTRIX_TEST(Stopwatch, FormatsMinutesSecondsAndTenths) {
    NOTRIX_CHECK_EQ(formatElapsed(0), std::string("00:00.0"));
    NOTRIX_CHECK_EQ(formatElapsed(1500), std::string("00:01.5"));
    NOTRIX_CHECK_EQ(formatElapsed(59900), std::string("00:59.9"));
    NOTRIX_CHECK_EQ(formatElapsed(60000), std::string("01:00.0"));
    NOTRIX_CHECK_EQ(formatElapsed(3599900), std::string("59:59.9"));
}

NOTRIX_TEST(Stopwatch, DropsTenthsPastAnHour) {
    // 1:02:03.4 is nine characters and does not fit across 52 pixels, and
    // tenths stop meaning anything at that scale anyway.
    NOTRIX_CHECK_EQ(formatElapsed(3600000), std::string("1:00:00"));
    NOTRIX_CHECK_EQ(formatElapsed(3723400), std::string("1:02:03"));
    NOTRIX_CHECK_EQ(formatElapsed(36000000), std::string("10:00:00"));
}

NOTRIX_TEST(Stopwatch, EveryFormatFitsThePanel) {
    // The whole point of the hour switch. Checked rather than argued: a
    // string one pixel too wide is clipped on hardware and nowhere else.
    const notrix::text::BitmapFont& font = notrix::text::font5x7();
    const std::uint64_t moments[] = {0,       999,      59999,    60000,
                                     3599999, 3600000,  3723400,  36000000};
    for (const std::uint64_t at : moments) {
        const std::string text = formatElapsed(at);
        // +1 for the emboldening, which draws one column wider.
        const int width = notrix::text::measureLine(text, font) + 1;
        NOTRIX_CHECK(width <= Framebuffer::kWidth);
    }
}

NOTRIX_TEST(Stopwatch, ReadyMatchesGolden) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    Stopwatch watch;
    renderStopwatch(canvas, watch, 1000, StopwatchStyle{});
    NOTRIX_CHECK_GOLDEN("stopwatch-ready", framebuffer);
}

NOTRIX_TEST(Stopwatch, RunningMatchesGolden) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    Stopwatch watch;
    watch.press(1000);
    // 12.3 seconds in, and 300 ms into the current second so the sweep is
    // partly drawn rather than at either end.
    renderStopwatch(canvas, watch, 13300, StopwatchStyle{});
    NOTRIX_CHECK_GOLDEN("stopwatch-running", framebuffer);
}

NOTRIX_TEST(Stopwatch, StoppedMatchesGolden) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    Stopwatch watch;
    watch.press(1000);
    watch.press(13300);
    renderStopwatch(canvas, watch, 20000, StopwatchStyle{});
    NOTRIX_CHECK_GOLDEN("stopwatch-stopped", framebuffer);
}

NOTRIX_TEST(Stopwatch, TheSweepOnlyDrawsWhileRunning) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    Stopwatch watch;
    watch.press(1000);
    watch.press(13300);

    renderStopwatch(canvas, watch, 20000, StopwatchStyle{});

    // A sweep left behind on a stopped stopwatch would say time is still
    // passing, which is the one thing it exists to deny.
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
        NOTRIX_CHECK(framebuffer.at(x, Framebuffer::kHeight - 1) == notrix::colors::kBlack);
    }
}
