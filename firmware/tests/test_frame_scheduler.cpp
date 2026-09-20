// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/render/FrameScheduler.h"
#include "notrix/render/Transition.h"

#include "support/TestFramework.h"

using notrix::render::FrameScheduler;

namespace {

FrameScheduler::Config config(int minimumInterval, int targetFps,
                              std::uint32_t refresh = 0) {
    FrameScheduler::Config c;
    c.minimumIntervalMillis = minimumInterval;
    c.targetFps = targetFps;
    c.periodicRefreshMillis = refresh;  // off by default so tests isolate dirtiness
    return c;
}

/// Drive the scheduler over a span of time, rendering whenever it says to.
struct Driver {
    FrameScheduler scheduler;
    std::uint64_t now = 0;

    explicit Driver(const FrameScheduler::Config& c) : scheduler(c) {}

    void run(std::uint64_t untilMillis, std::uint32_t renderCost = 1, int stepMillis = 1) {
        while (now < untilMillis) {
            if (scheduler.beginFrame(now)) {
                scheduler.endFrame(now, renderCost);
            }
            now += static_cast<std::uint64_t>(stepMillis);
        }
    }
};

}  // namespace

// --- interval ----------------------------------------------------------------

NOTRIX_TEST(FrameScheduler, HardwareFloorBeatsAnOptimisticTargetRate) {
    // Asking for more than the panel can do would only produce overruns, so the
    // hardware floor wins (blueprint §9.4).
    FrameScheduler tooFast(config(15, 100));  // 10 ms requested, below the floor
    NOTRIX_CHECK_EQ(tooFast.intervalMillis(), 15);

    FrameScheduler fast(config(15, 60));  // 16 ms, which clears the floor
    NOTRIX_CHECK_EQ(fast.intervalMillis(), 16);

    FrameScheduler normal(config(15, 30));
    NOTRIX_CHECK_EQ(normal.intervalMillis(), 33);
}

NOTRIX_TEST(FrameScheduler, MinimumIntervalCanBeAdoptedFromTheDisplay) {
    FrameScheduler scheduler(config(1, 60));
    NOTRIX_CHECK_EQ(scheduler.intervalMillis(), 16);

    scheduler.setMinimumInterval(50);  // a slower panel than assumed
    NOTRIX_CHECK_EQ(scheduler.intervalMillis(), 50);
}

NOTRIX_TEST(FrameScheduler, DegenerateConfigurationDoesNotDivideByZero) {
    FrameScheduler scheduler(config(0, 0));
    NOTRIX_CHECK(scheduler.intervalMillis() >= 1);
}

// --- pacing ------------------------------------------------------------------

NOTRIX_TEST(FrameScheduler, FirstFrameRendersImmediately) {
    FrameScheduler scheduler(config(15, 30));
    NOTRIX_CHECK(scheduler.beginFrame(0));
}

NOTRIX_TEST(FrameScheduler, NeverPresentsFasterThanTheInterval) {
    // The panel's throttle is a hardware constraint, not a suggestion.
    Driver driver(config(15, 30));
    driver.scheduler.invalidate();

    // Stay dirty for a simulated second at 1 ms resolution.
    for (std::uint64_t t = 0; t < 1000; ++t) {
        driver.scheduler.invalidate();
        if (driver.scheduler.beginFrame(t)) {
            driver.scheduler.endFrame(t, 1);
        }
    }

    // 1000 ms at a 33 ms interval is about 31 frames, never 1000.
    const std::uint32_t rendered = driver.scheduler.stats().rendered;
    NOTRIX_CHECK(rendered <= 31);
    NOTRIX_CHECK(rendered >= 29);
}

NOTRIX_TEST(FrameScheduler, NextDueLetsTheCallerSleep) {
    // The device main loop sleeps until this rather than busy-waiting.
    FrameScheduler scheduler(config(15, 30));
    scheduler.beginFrame(0);
    scheduler.endFrame(0, 1);

    NOTRIX_CHECK_EQ(scheduler.nextDueMillis(0), std::uint64_t(33));
    NOTRIX_CHECK_EQ(scheduler.nextDueMillis(10), std::uint64_t(33));
    // Already overdue: no sleep.
    NOTRIX_CHECK_EQ(scheduler.nextDueMillis(90), std::uint64_t(90));
}

// --- dirty tracking ----------------------------------------------------------

NOTRIX_TEST(FrameScheduler, StaticContentIsNotRedrawn) {
    // This is the whole point: a clock showing a static minute must not cost 30
    // renders a second.
    Driver driver(config(15, 30));
    driver.run(10000);

    NOTRIX_CHECK_EQ(driver.scheduler.stats().rendered, 1u);  // the first frame only
    NOTRIX_CHECK(driver.scheduler.stats().skipped > 100);
}

NOTRIX_TEST(FrameScheduler, InvalidatingCausesExactlyOneRedraw) {
    Driver driver(config(15, 30));
    driver.run(1000);
    const std::uint32_t before = driver.scheduler.stats().rendered;

    driver.scheduler.invalidate();
    driver.run(2000);

    NOTRIX_CHECK_EQ(driver.scheduler.stats().rendered, before + 1u);
}

NOTRIX_TEST(FrameScheduler, AnimatingContentRendersContinuously) {
    // Scrolling text invalidates every frame, which should give the full rate.
    FrameScheduler scheduler(config(15, 30));
    for (std::uint64_t t = 0; t < 1000; ++t) {
        scheduler.invalidate();
        if (scheduler.beginFrame(t)) {
            scheduler.endFrame(t, 1);
        }
    }
    NOTRIX_CHECK(scheduler.stats().rendered >= 29);
    NOTRIX_CHECK_EQ(scheduler.stats().skipped, 0u);
}

NOTRIX_TEST(FrameScheduler, PeriodicRefreshHealsAStalePanel) {
    // A dropped SPI frame would otherwise persist until the content changed.
    Driver driver(config(15, 30, 1000));
    driver.run(5000);

    // One initial frame plus roughly one per refresh interval.
    NOTRIX_CHECK(driver.scheduler.stats().rendered >= 5);
    NOTRIX_CHECK(driver.scheduler.stats().rendered <= 7);
}

NOTRIX_TEST(FrameScheduler, PeriodicRefreshCanBeDisabled) {
    Driver driver(config(15, 30, 0));
    driver.run(60000);
    NOTRIX_CHECK_EQ(driver.scheduler.stats().rendered, 1u);
}

// --- diagnostics -------------------------------------------------------------

NOTRIX_TEST(FrameScheduler, OverrunsAreCountedNotHidden) {
    // "It feels laggy" should be a number in diagnostics, not a bug report.
    FrameScheduler scheduler(config(15, 30));

    scheduler.invalidate();
    scheduler.beginFrame(0);
    scheduler.endFrame(0, 5);  // comfortably inside the 33 ms budget
    NOTRIX_CHECK_EQ(scheduler.stats().overruns, 0u);

    scheduler.invalidate();
    scheduler.beginFrame(100);
    scheduler.endFrame(100, 80);  // took more than twice the budget
    NOTRIX_CHECK_EQ(scheduler.stats().overruns, 1u);
}

NOTRIX_TEST(FrameScheduler, TracksWorstCaseRenderTime) {
    FrameScheduler scheduler(config(15, 30));

    const std::uint32_t costs[] = {3, 9, 4, 21, 6};
    std::uint64_t now = 0;
    for (const std::uint32_t cost : costs) {
        scheduler.invalidate();
        if (scheduler.beginFrame(now)) {
            scheduler.endFrame(now, cost);
        }
        now += 50;
    }

    NOTRIX_CHECK_EQ(scheduler.stats().worstRenderMillis, 21u);
    NOTRIX_CHECK_EQ(scheduler.stats().lastRenderMillis, 6u);
}

NOTRIX_TEST(FrameScheduler, StatsCanBeReset) {
    Driver driver(config(15, 30));
    driver.run(1000);
    NOTRIX_CHECK(driver.scheduler.stats().skipped > 0);

    driver.scheduler.resetStats();
    NOTRIX_CHECK_EQ(driver.scheduler.stats().skipped, 0u);
    NOTRIX_CHECK_EQ(driver.scheduler.stats().rendered, 0u);
}

// --- robustness --------------------------------------------------------------

NOTRIX_TEST(FrameScheduler, BackwardsClockDoesNotStallRendering) {
    // If the clock steps back, rendering must not freeze until real time
    // catches up.
    FrameScheduler scheduler(config(15, 30));
    scheduler.invalidate();
    scheduler.beginFrame(100000);
    scheduler.endFrame(100000, 1);

    scheduler.invalidate();
    NOTRIX_CHECK(scheduler.beginFrame(50));
}

NOTRIX_TEST(FrameScheduler, RenderingIsNotRequiredAfterBeginFrameSaysYes) {
    // A caller may decide not to draw after all; the scheduler must not then
    // believe a frame was presented.
    FrameScheduler scheduler(config(15, 30));
    NOTRIX_CHECK(scheduler.beginFrame(0));
    NOTRIX_CHECK_EQ(scheduler.stats().rendered, 0u);
    NOTRIX_CHECK(scheduler.dirty());
}

// --- transitions -------------------------------------------------------------

namespace {

notrix::Framebuffer solid(notrix::Rgb color) {
    notrix::Framebuffer frame;
    frame.fill(color);
    return frame;
}

}  // namespace

NOTRIX_TEST(Transition, EndpointsAreExactlyTheTwoFrames) {
    // Whatever happens in between, a transition must start as one frame and end
    // as the other. Anything else leaves a seam at the join.
    using namespace notrix::render;
    const notrix::Framebuffer from = solid(notrix::rgb(255, 0, 0));
    const notrix::Framebuffer to = solid(notrix::rgb(0, 0, 255));

    for (const TransitionStyle style :
         {TransitionStyle::None, TransitionStyle::Slide, TransitionStyle::Fade}) {
        notrix::Framebuffer out;
        composite(out, from, to, style, TransitionDirection::Forward, 1000);
        NOTRIX_CHECK(out == to);
    }

    notrix::Framebuffer start;
    composite(start, from, to, TransitionStyle::Slide, TransitionDirection::Forward, 0);
    NOTRIX_CHECK(start == from);
}

NOTRIX_TEST(Transition, ProgressIsClamped) {
    using namespace notrix::render;
    const notrix::Framebuffer from = solid(notrix::rgb(255, 0, 0));
    const notrix::Framebuffer to = solid(notrix::rgb(0, 0, 255));

    notrix::Framebuffer under;
    notrix::Framebuffer over;
    composite(under, from, to, TransitionStyle::Slide, TransitionDirection::Forward, -500);
    composite(over, from, to, TransitionStyle::Slide, TransitionDirection::Forward, 9999);

    NOTRIX_CHECK(under == from);
    NOTRIX_CHECK(over == to);
}

NOTRIX_TEST(Transition, SlideMovesContentTheWayTheKnobTurned) {
    // Forward slides content left, so a marker near the left edge of the
    // outgoing frame should leave first. Backward is the mirror.
    using namespace notrix::render;

    notrix::Framebuffer from;
    from.set(0, 8, notrix::rgb(255, 0, 0));
    from.set(notrix::Framebuffer::kWidth - 1, 8, notrix::rgb(0, 255, 0));
    const notrix::Framebuffer to;  // black

    notrix::Framebuffer forward;
    composite(forward, from, to, TransitionStyle::Slide, TransitionDirection::Forward, 100);
    // Content moved left, so the red pixel that was at column 0 is gone.
    NOTRIX_CHECK(forward.at(0, 8) != notrix::rgb(255, 0, 0));

    notrix::Framebuffer backward;
    composite(backward, from, to, TransitionStyle::Slide, TransitionDirection::Backward, 100);
    // Content moved right, so the green pixel that was at the right edge is gone.
    NOTRIX_CHECK(backward.at(notrix::Framebuffer::kWidth - 1, 8) != notrix::rgb(0, 255, 0));
}

NOTRIX_TEST(Transition, FadeGoesThroughBlackRatherThanBlendingTwoFrames) {
    // Blending would spend the middle of every transition showing two times
    // superimposed, which on 52x16 is unreadable.
    using namespace notrix::render;
    const notrix::Framebuffer from = solid(notrix::rgb(255, 255, 255));
    const notrix::Framebuffer to = solid(notrix::rgb(255, 255, 255));

    notrix::Framebuffer middle;
    composite(middle, from, to, TransitionStyle::Fade, TransitionDirection::Forward, 500);

    // Both ends are full white; a blend would stay white all the way through.
    NOTRIX_CHECK(middle.at(26, 8) == notrix::colors::kBlack);
}

NOTRIX_TEST(Transition, NoneIsTheDestinationImmediately) {
    using namespace notrix::render;
    const notrix::Framebuffer from = solid(notrix::rgb(255, 0, 0));
    const notrix::Framebuffer to = solid(notrix::rgb(0, 0, 255));

    notrix::Framebuffer out;
    composite(out, from, to, TransitionStyle::None, TransitionDirection::Forward, 1);
    NOTRIX_CHECK(out == to);
}

NOTRIX_TEST(Transition, NamesRoundTrip) {
    using namespace notrix::render;
    for (const TransitionStyle style :
         {TransitionStyle::None, TransitionStyle::Slide, TransitionStyle::Fade}) {
        NOTRIX_CHECK(transitionStyleFromName(transitionStyleName(style)) == style);
    }
    NOTRIX_CHECK(transitionStyleFromName("nonsense") == TransitionStyle::None);
}
