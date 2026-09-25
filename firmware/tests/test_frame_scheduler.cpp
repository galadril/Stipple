// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/render/FrameScheduler.h"
#include "stipple/render/Overlay.h"
#include "stipple/render/Transition.h"

#include "support/TestFramework.h"

using stipple::render::FrameScheduler;

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

STIPPLE_TEST(FrameScheduler, HardwareFloorBeatsAnOptimisticTargetRate) {
    // Asking for more than the panel can do would only produce overruns, so the
    // hardware floor wins (blueprint §9.4).
    FrameScheduler tooFast(config(15, 100));  // 10 ms requested, below the floor
    STIPPLE_CHECK_EQ(tooFast.intervalMillis(), 15);

    FrameScheduler fast(config(15, 60));  // 16 ms, which clears the floor
    STIPPLE_CHECK_EQ(fast.intervalMillis(), 16);

    FrameScheduler normal(config(15, 30));
    STIPPLE_CHECK_EQ(normal.intervalMillis(), 33);
}

STIPPLE_TEST(FrameScheduler, MinimumIntervalCanBeAdoptedFromTheDisplay) {
    FrameScheduler scheduler(config(1, 60));
    STIPPLE_CHECK_EQ(scheduler.intervalMillis(), 16);

    scheduler.setMinimumInterval(50);  // a slower panel than assumed
    STIPPLE_CHECK_EQ(scheduler.intervalMillis(), 50);
}

STIPPLE_TEST(FrameScheduler, DegenerateConfigurationDoesNotDivideByZero) {
    FrameScheduler scheduler(config(0, 0));
    STIPPLE_CHECK(scheduler.intervalMillis() >= 1);
}

// --- pacing ------------------------------------------------------------------

STIPPLE_TEST(FrameScheduler, FirstFrameRendersImmediately) {
    FrameScheduler scheduler(config(15, 30));
    STIPPLE_CHECK(scheduler.beginFrame(0));
}

STIPPLE_TEST(FrameScheduler, NeverPresentsFasterThanTheInterval) {
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
    STIPPLE_CHECK(rendered <= 31);
    STIPPLE_CHECK(rendered >= 29);
}

STIPPLE_TEST(FrameScheduler, NextDueLetsTheCallerSleep) {
    // The device main loop sleeps until this rather than busy-waiting.
    FrameScheduler scheduler(config(15, 30));
    scheduler.beginFrame(0);
    scheduler.endFrame(0, 1);

    STIPPLE_CHECK_EQ(scheduler.nextDueMillis(0), std::uint64_t(33));
    STIPPLE_CHECK_EQ(scheduler.nextDueMillis(10), std::uint64_t(33));
    // Already overdue: no sleep.
    STIPPLE_CHECK_EQ(scheduler.nextDueMillis(90), std::uint64_t(90));
}

// --- dirty tracking ----------------------------------------------------------

STIPPLE_TEST(FrameScheduler, StaticContentIsNotRedrawn) {
    // This is the whole point: a clock showing a static minute must not cost 30
    // renders a second.
    Driver driver(config(15, 30));
    driver.run(10000);

    STIPPLE_CHECK_EQ(driver.scheduler.stats().rendered, 1u);  // the first frame only
    STIPPLE_CHECK(driver.scheduler.stats().skipped > 100);
}

STIPPLE_TEST(FrameScheduler, InvalidatingCausesExactlyOneRedraw) {
    Driver driver(config(15, 30));
    driver.run(1000);
    const std::uint32_t before = driver.scheduler.stats().rendered;

    driver.scheduler.invalidate();
    driver.run(2000);

    STIPPLE_CHECK_EQ(driver.scheduler.stats().rendered, before + 1u);
}

STIPPLE_TEST(FrameScheduler, AnimatingContentRendersContinuously) {
    // Scrolling text invalidates every frame, which should give the full rate.
    FrameScheduler scheduler(config(15, 30));
    for (std::uint64_t t = 0; t < 1000; ++t) {
        scheduler.invalidate();
        if (scheduler.beginFrame(t)) {
            scheduler.endFrame(t, 1);
        }
    }
    STIPPLE_CHECK(scheduler.stats().rendered >= 29);
    STIPPLE_CHECK_EQ(scheduler.stats().skipped, 0u);
}

STIPPLE_TEST(FrameScheduler, PeriodicRefreshHealsAStalePanel) {
    // A dropped SPI frame would otherwise persist until the content changed.
    Driver driver(config(15, 30, 1000));
    driver.run(5000);

    // One initial frame plus roughly one per refresh interval.
    STIPPLE_CHECK(driver.scheduler.stats().rendered >= 5);
    STIPPLE_CHECK(driver.scheduler.stats().rendered <= 7);
}

STIPPLE_TEST(FrameScheduler, PeriodicRefreshCanBeDisabled) {
    Driver driver(config(15, 30, 0));
    driver.run(60000);
    STIPPLE_CHECK_EQ(driver.scheduler.stats().rendered, 1u);
}

// --- diagnostics -------------------------------------------------------------

STIPPLE_TEST(FrameScheduler, OverrunsAreCountedNotHidden) {
    // "It feels laggy" should be a number in diagnostics, not a bug report.
    FrameScheduler scheduler(config(15, 30));

    scheduler.invalidate();
    scheduler.beginFrame(0);
    scheduler.endFrame(0, 5);  // comfortably inside the 33 ms budget
    STIPPLE_CHECK_EQ(scheduler.stats().overruns, 0u);

    scheduler.invalidate();
    scheduler.beginFrame(100);
    scheduler.endFrame(100, 80);  // took more than twice the budget
    STIPPLE_CHECK_EQ(scheduler.stats().overruns, 1u);
}

STIPPLE_TEST(FrameScheduler, TracksWorstCaseRenderTime) {
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

    STIPPLE_CHECK_EQ(scheduler.stats().worstRenderMillis, 21u);
    STIPPLE_CHECK_EQ(scheduler.stats().lastRenderMillis, 6u);
}

STIPPLE_TEST(FrameScheduler, StatsCanBeReset) {
    Driver driver(config(15, 30));
    driver.run(1000);
    STIPPLE_CHECK(driver.scheduler.stats().skipped > 0);

    driver.scheduler.resetStats();
    STIPPLE_CHECK_EQ(driver.scheduler.stats().skipped, 0u);
    STIPPLE_CHECK_EQ(driver.scheduler.stats().rendered, 0u);
}

// --- robustness --------------------------------------------------------------

STIPPLE_TEST(FrameScheduler, BackwardsClockDoesNotStallRendering) {
    // If the clock steps back, rendering must not freeze until real time
    // catches up.
    FrameScheduler scheduler(config(15, 30));
    scheduler.invalidate();
    scheduler.beginFrame(100000);
    scheduler.endFrame(100000, 1);

    scheduler.invalidate();
    STIPPLE_CHECK(scheduler.beginFrame(50));
}

STIPPLE_TEST(FrameScheduler, RenderingIsNotRequiredAfterBeginFrameSaysYes) {
    // A caller may decide not to draw after all; the scheduler must not then
    // believe a frame was presented.
    FrameScheduler scheduler(config(15, 30));
    STIPPLE_CHECK(scheduler.beginFrame(0));
    STIPPLE_CHECK_EQ(scheduler.stats().rendered, 0u);
    STIPPLE_CHECK(scheduler.dirty());
}

// --- transitions -------------------------------------------------------------

namespace {

stipple::Framebuffer solid(stipple::Rgb color) {
    stipple::Framebuffer frame;
    frame.fill(color);
    return frame;
}

}  // namespace

STIPPLE_TEST(Transition, EndpointsAreExactlyTheTwoFrames) {
    // Whatever happens in between, a transition must start as one frame and end
    // as the other. Anything else leaves a seam at the join.
    using namespace stipple::render;
    const stipple::Framebuffer from = solid(stipple::rgb(255, 0, 0));
    const stipple::Framebuffer to = solid(stipple::rgb(0, 0, 255));

    // Every style, by index rather than by name. Listing them here meant a
    // style added later was silently untested - which is how wipe and dissolve
    // arrived with no endpoint check at all.
    for (int i = 0; i < kTransitionStyleCount; ++i) {
        const TransitionStyle style = transitionStyleAt(i);
        for (const TransitionDirection direction :
             {TransitionDirection::Forward, TransitionDirection::Backward}) {
            stipple::Framebuffer out;
            composite(out, from, to, style, direction, 1000);
            STIPPLE_CHECK(out == to);

            stipple::Framebuffer start;
            composite(start, from, to, style, direction, 0);
            // None is the destination at any progress, by definition.
            STIPPLE_CHECK(start == (style == TransitionStyle::None ? to : from));
        }
    }

}

STIPPLE_TEST(Transition, ProgressIsClamped) {
    using namespace stipple::render;
    const stipple::Framebuffer from = solid(stipple::rgb(255, 0, 0));
    const stipple::Framebuffer to = solid(stipple::rgb(0, 0, 255));

    stipple::Framebuffer under;
    stipple::Framebuffer over;
    composite(under, from, to, TransitionStyle::Slide, TransitionDirection::Forward, -500);
    composite(over, from, to, TransitionStyle::Slide, TransitionDirection::Forward, 9999);

    STIPPLE_CHECK(under == from);
    STIPPLE_CHECK(over == to);
}

STIPPLE_TEST(Transition, SlideMovesContentTheWayTheKnobTurned) {
    // Forward slides content left, so a marker near the left edge of the
    // outgoing frame should leave first. Backward is the mirror.
    using namespace stipple::render;

    stipple::Framebuffer from;
    from.set(0, 8, stipple::rgb(255, 0, 0));
    from.set(stipple::Framebuffer::kWidth - 1, 8, stipple::rgb(0, 255, 0));
    const stipple::Framebuffer to;  // black

    stipple::Framebuffer forward;
    composite(forward, from, to, TransitionStyle::Slide, TransitionDirection::Forward, 100);
    // Content moved left, so the red pixel that was at column 0 is gone.
    STIPPLE_CHECK(forward.at(0, 8) != stipple::rgb(255, 0, 0));

    stipple::Framebuffer backward;
    composite(backward, from, to, TransitionStyle::Slide, TransitionDirection::Backward, 100);
    // Content moved right, so the green pixel that was at the right edge is gone.
    STIPPLE_CHECK(backward.at(stipple::Framebuffer::kWidth - 1, 8) != stipple::rgb(0, 255, 0));
}

STIPPLE_TEST(Transition, FadeGoesThroughBlackRatherThanBlendingTwoFrames) {
    // Blending would spend the middle of every transition showing two times
    // superimposed, which on 52x16 is unreadable.
    using namespace stipple::render;
    const stipple::Framebuffer from = solid(stipple::rgb(255, 255, 255));
    const stipple::Framebuffer to = solid(stipple::rgb(255, 255, 255));

    stipple::Framebuffer middle;
    composite(middle, from, to, TransitionStyle::Fade, TransitionDirection::Forward, 500);

    // Both ends are full white; a blend would stay white all the way through.
    STIPPLE_CHECK(middle.at(26, 8) == stipple::colors::kBlack);
}

STIPPLE_TEST(Transition, NoneIsTheDestinationImmediately) {
    using namespace stipple::render;
    const stipple::Framebuffer from = solid(stipple::rgb(255, 0, 0));
    const stipple::Framebuffer to = solid(stipple::rgb(0, 0, 255));

    stipple::Framebuffer out;
    composite(out, from, to, TransitionStyle::None, TransitionDirection::Forward, 1);
    STIPPLE_CHECK(out == to);
}

STIPPLE_TEST(Transition, NamesRoundTrip) {
    using namespace stipple::render;
    for (int i = 0; i < kTransitionStyleCount; ++i) {
        const TransitionStyle style = transitionStyleAt(i);
        STIPPLE_CHECK(transitionStyleFromName(transitionStyleName(style)) == style);
    }
    STIPPLE_CHECK(transitionStyleFromName("nonsense") == TransitionStyle::None);

    // Every index maps to a distinct style, or a settings UI listing them by
    // index would offer the same thing twice.
    for (int i = 0; i < kTransitionStyleCount; ++i) {
        for (int j = i + 1; j < kTransitionStyleCount; ++j) {
            STIPPLE_CHECK(transitionStyleAt(i) != transitionStyleAt(j));
        }
    }
}


// --- overlays ----------------------------------------------------------------

STIPPLE_TEST(Overlay, NeverOverwritesWhatTheAppDrew) {
    // The one rule that makes overlays safe (DESIGN.md section 7). Without it,
    // sparseness and colour choice are good intentions and one unlucky
    // raindrop lands on the stroke of a digit.
    using namespace stipple::render;

    for (int i = 0; i < kOverlayCount; ++i) {
        const Overlay overlay = overlayAt(i);
        for (std::uint64_t t = 0; t < 4000; t += 137) {
            stipple::Framebuffer frame;
            frame.fill(stipple::rgb(255, 255, 255));

            drawOverlay(frame, overlay, t);

            for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
                for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
                    STIPPLE_CHECK(frame.at(x, y) == stipple::rgb(255, 255, 255));
                }
            }
        }
    }
}

STIPPLE_TEST(Overlay, StaysOutOfTheRowsTypeLivesIn) {
    // Rows 4-10 are where a single centred line of text sits. An overlay that
    // wandered in would be drawing on the time even while obeying the additive
    // rule, because the gaps inside glyphs are black.
    using namespace stipple::render;

    for (int i = 1; i < kOverlayCount; ++i) {
        for (std::uint64_t t = 0; t < 8000; t += 91) {
            stipple::Framebuffer frame;
            drawOverlay(frame, overlayAt(i), t);

            for (int y = 4; y <= 10; ++y) {
                for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
                    STIPPLE_CHECK(frame.at(x, y) == stipple::colors::kBlack);
                }
            }
        }
    }
}

STIPPLE_TEST(Overlay, IsSparseRatherThanACurtain) {
    // "A few falling columns, not a curtain." Measured against the rows an
    // overlay is actually allowed to use, so the centre-band rule does not
    // flatter the number.
    using namespace stipple::render;
    const int usable = stipple::Framebuffer::kWidth * (stipple::Framebuffer::kHeight - 7);

    for (int i = 1; i < kOverlayCount; ++i) {
        for (std::uint64_t t = 0; t < 8000; t += 211) {
            stipple::Framebuffer frame;
            drawOverlay(frame, overlayAt(i), t);

            int lit = 0;
            for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
                for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
                    if (frame.at(x, y) != stipple::colors::kBlack) { ++lit; }
                }
            }
            STIPPLE_CHECK(lit * 2 < usable);
        }
    }
}

STIPPLE_TEST(Overlay, IsAPureFunctionOfTime) {
    // Same instant, same pixels - which is what makes any of this testable and
    // what stops the emulator and the device drifting apart.
    using namespace stipple::render;

    for (int i = 1; i < kOverlayCount; ++i) {
        stipple::Framebuffer a;
        stipple::Framebuffer b;
        drawOverlay(a, overlayAt(i), 4321);
        drawOverlay(b, overlayAt(i), 4321);
        STIPPLE_CHECK(a == b);
    }
}

STIPPLE_TEST(Overlay, ActuallyMoves) {
    // The mirror of the test above: pure does not mean static. A frozen
    // overlay would pass every other check here.
    using namespace stipple::render;

    for (int i = 1; i < kOverlayCount; ++i) {
        stipple::Framebuffer early;
        stipple::Framebuffer later;
        drawOverlay(early, overlayAt(i), 0);
        drawOverlay(later, overlayAt(i), 1500);
        STIPPLE_CHECK(early != later);
    }
}

STIPPLE_TEST(Overlay, NoneDrawsNothing) {
    using namespace stipple::render;
    stipple::Framebuffer frame;
    drawOverlay(frame, Overlay::None, 1234);

    const stipple::Framebuffer blank;
    STIPPLE_CHECK(frame == blank);
}

STIPPLE_TEST(Overlay, NamesRoundTrip) {
    using namespace stipple::render;
    for (int i = 0; i < kOverlayCount; ++i) {
        const Overlay overlay = overlayAt(i);
        STIPPLE_CHECK(overlayFromName(overlayName(overlay)) == overlay);
    }
    STIPPLE_CHECK(overlayFromName("hurricane") == Overlay::None);
}

STIPPLE_TEST(Transition, WipeRevealsWithoutMovingEitherFrame) {
    // The difference from a slide, and the reason to have both: here nothing
    // travels, so each pixel is one frame or the other rather than a shifted
    // copy.
    using namespace stipple::render;
    stipple::Framebuffer from;
    stipple::Framebuffer to;
    for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
        for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
            from.set(x, y, stipple::rgb(static_cast<std::uint8_t>(x * 4 + 1), 0, 0));
            to.set(x, y, stipple::rgb(0, 0, static_cast<std::uint8_t>(x * 4 + 1)));
        }
    }

    stipple::Framebuffer out;
    composite(out, from, to, TransitionStyle::Wipe, TransitionDirection::Forward, 500);

    for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
        for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
            const stipple::Rgb pixel = out.at(x, y);
            STIPPLE_CHECK(pixel == from.at(x, y) || pixel == to.at(x, y));
        }
    }
}

STIPPLE_TEST(Transition, WipeArrivesFromTheSideTheContentWouldHaveTravelled) {
    // A wipe that contradicted the slide would mean the two styles disagree
    // about which way "next" is, and the knob would feel different depending
    // on a setting.
    using namespace stipple::render;
    const stipple::Framebuffer from = solid(stipple::rgb(255, 0, 0));
    const stipple::Framebuffer to = solid(stipple::rgb(0, 0, 255));

    stipple::Framebuffer forward;
    composite(forward, from, to, TransitionStyle::Wipe, TransitionDirection::Forward, 250);
    STIPPLE_CHECK(forward.at(stipple::Framebuffer::kWidth - 1, 0) == to.at(0, 0));
    STIPPLE_CHECK(forward.at(0, 0) == from.at(0, 0));

    stipple::Framebuffer backward;
    composite(backward, from, to, TransitionStyle::Wipe, TransitionDirection::Backward, 250);
    STIPPLE_CHECK(backward.at(0, 0) == to.at(0, 0));
    STIPPLE_CHECK(backward.at(stipple::Framebuffer::kWidth - 1, 0) == from.at(0, 0));
}

STIPPLE_TEST(Transition, DissolveTurnsPixelsOverGraduallyAndEvenly) {
    using namespace stipple::render;
    const stipple::Framebuffer from = solid(stipple::rgb(255, 0, 0));
    const stipple::Framebuffer to = solid(stipple::rgb(0, 0, 255));

    auto converted = [&](int permille) {
        stipple::Framebuffer out;
        composite(out, from, to, TransitionStyle::Dissolve, TransitionDirection::Forward, permille);
        int count = 0;
        for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
            for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
                if (out.at(x, y) == to.at(0, 0)) { ++count; }
            }
        }
        return count;
    };

    const int total = stipple::Framebuffer::kWidth * stipple::Framebuffer::kHeight;
    STIPPLE_CHECK_EQ(converted(0), 0);
    STIPPLE_CHECK_EQ(converted(1000), total);

    // Monotonic, and roughly in step with progress - a dissolve that did most
    // of its work in the last tenth reads as a jump, not a dissolve.
    STIPPLE_CHECK(converted(250) < converted(500));
    STIPPLE_CHECK(converted(500) < converted(750));
    STIPPLE_CHECK(converted(500) > total / 4);
    STIPPLE_CHECK(converted(500) < (total * 3) / 4);
}

STIPPLE_TEST(Transition, DissolveIgnoresDirection) {
    // It has none. That is the point: it is the one style that does not say
    // where the next app came from.
    using namespace stipple::render;
    const stipple::Framebuffer from = solid(stipple::rgb(255, 0, 0));
    const stipple::Framebuffer to = solid(stipple::rgb(0, 0, 255));

    stipple::Framebuffer forward;
    stipple::Framebuffer backward;
    composite(forward, from, to, TransitionStyle::Dissolve, TransitionDirection::Forward, 400);
    composite(backward, from, to, TransitionStyle::Dissolve, TransitionDirection::Backward, 400);
    STIPPLE_CHECK(forward == backward);
}
