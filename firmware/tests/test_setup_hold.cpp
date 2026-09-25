// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/input/SetupHold.h"

#include "support/TestFramework.h"

using stipple::input::SetupHold;
using stipple::platform::ButtonPhase;
using stipple::platform::InputEvent;
using stipple::platform::RawInput;

namespace {

InputEvent event(RawInput source, ButtonPhase phase, std::uint64_t at) {
    InputEvent e;
    e.source = source;
    e.phase = phase;
    e.timestampMillis = at;
    return e;
}

/// Hold the knob from `at`, then run the clock forward to `until`.
bool holdFor(SetupHold& hold, std::uint64_t at, std::uint64_t until) {
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, at));

    bool fired = false;
    for (std::uint64_t now = at; now <= until; now += 50) {
        if (hold.tick(now)) {
            fired = true;
        }
    }
    return fired;
}

}  // namespace

STIPPLE_TEST(SetupHold, KnobHeldLongEnoughFires) {
    SetupHold hold;
    STIPPLE_CHECK(holdFor(hold, 1000, 1000 + SetupHold::kHoldMillis));
}

STIPPLE_TEST(SetupHold, ShortPressDoesNotFire) {
    SetupHold hold;
    // The settings toggle lives at 500 ms on this same button. A gesture that
    // fired anywhere near it would make the settings screen unusable.
    STIPPLE_CHECK(!holdFor(hold, 1000, 1900));
}

STIPPLE_TEST(SetupHold, FiresExactlyOncePerHold) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 0));

    int fires = 0;
    for (std::uint64_t now = 0; now <= SetupHold::kHoldMillis * 3; now += 50) {
        if (hold.tick(now)) {
            ++fires;
        }
    }
    STIPPLE_CHECK_EQ(fires, 1);
}

STIPPLE_TEST(SetupHold, ReleasingRearmsIt) {
    SetupHold hold;
    STIPPLE_CHECK(holdFor(hold, 0, SetupHold::kHoldMillis));
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Up, SetupHold::kHoldMillis));
    STIPPLE_CHECK(holdFor(hold, 20000, 20000 + SetupHold::kHoldMillis));
}

STIPPLE_TEST(SetupHold, ReleasingEarlyCancels) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 0));
    hold.tick(1000);
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Up, 1200));

    bool fired = false;
    for (std::uint64_t now = 1200; now <= 20000; now += 50) {
        if (hold.tick(now)) {
            fired = true;
        }
    }
    STIPPLE_CHECK(!fired);
    STIPPLE_CHECK(!hold.counting());
}

STIPPLE_TEST(SetupHold, OtherButtonsAreIgnored) {
    SetupHold hold;
    // KeyMiddle is a separate button bound to Back. A hidden five-second
    // meaning there would be a trap rather than a feature.
    for (const RawInput source :
         {RawInput::KeyMinus, RawInput::KeyPlus, RawInput::KeyMiddle}) {
        SetupHold other;
        other.handle(event(source, ButtonPhase::Down, 0));
        bool fired = false;
        for (std::uint64_t now = 0; now <= SetupHold::kHoldMillis * 2; now += 50) {
            if (other.tick(now)) {
                fired = true;
            }
        }
        STIPPLE_CHECK(!fired);
    }
    STIPPLE_CHECK(!hold.counting());
}

STIPPLE_TEST(SetupHold, DetentsCannotBeHeld) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryRight, ButtonPhase::Tick, 0));
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Tick, 0));
    STIPPLE_CHECK(!hold.counting());
    STIPPLE_CHECK(!hold.tick(SetupHold::kHoldMillis * 2));
}

STIPPLE_TEST(SetupHold, CountdownRunsDownAndStopsAtZero) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 1000));
    STIPPLE_CHECK_EQ(hold.remainingMillis(1000), SetupHold::kHoldMillis);
    STIPPLE_CHECK_EQ(hold.remainingMillis(3000), SetupHold::kHoldMillis - 2000);
    STIPPLE_CHECK_EQ(hold.remainingMillis(1000 + SetupHold::kHoldMillis), 0u);
}

STIPPLE_TEST(SetupHold, SurvivesTheClockSteppingBackwards) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 10000000));
    // Must not underflow into an instant fire.
    STIPPLE_CHECK(!hold.tick(5000));
    STIPPLE_CHECK_EQ(hold.remainingMillis(5000), 0u);
    STIPPLE_CHECK(hold.tick(10000000 + SetupHold::kHoldMillis));
}

STIPPLE_TEST(SetupHold, ResetForgetsAHoldInProgress) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 0));
    hold.reset();
    STIPPLE_CHECK(!hold.counting());
    STIPPLE_CHECK(!hold.tick(SetupHold::kHoldMillis * 2));
}

STIPPLE_TEST(SetupHold, AnOrdinaryPressNeverShowsTheCountdown) {
    // The bug this exists for: the countdown appeared on the press itself, so
    // every normal use of the knob - pausing the carousel, opening settings,
    // starting the stopwatch - flashed SETUP across the panel on the way.
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 1000));

    for (std::uint64_t now = 1000; now < 1000 + SetupHold::kRevealMillis; now += 25) {
        hold.tick(now);
        STIPPLE_CHECK(!hold.counting());
    }

    // Released before the reveal: the panel never mentioned it.
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Up, 1000 + 600));
    STIPPLE_CHECK(!hold.counting());
}

STIPPLE_TEST(SetupHold, TheCountdownAppearsOnceTheHoldIsDeliberate) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 1000));

    hold.tick(1000 + SetupHold::kRevealMillis - 1);
    STIPPLE_CHECK(!hold.counting());

    hold.tick(1000 + SetupHold::kRevealMillis);
    STIPPLE_CHECK(hold.counting());
}

STIPPLE_TEST(SetupHold, TheRevealIsClearOfTheLongPressThreshold) {
    // The mapper calls anything over 500 ms a long press, and a long press on
    // this button opens settings. Showing the countdown before that point
    // would put SETUP on screen every time somebody opened the menu.
    STIPPLE_CHECK(SetupHold::kRevealMillis > 500);
    // And it still has to leave most of the hold visible, or the countdown
    // would appear and fire almost together.
    STIPPLE_CHECK(SetupHold::kRevealMillis < SetupHold::kHoldMillis / 2);
}

STIPPLE_TEST(SetupHold, StillFiresAtTheFullHoldDespiteTheReveal) {
    SetupHold hold;
    STIPPLE_CHECK(holdFor(hold, 1000, 1000 + SetupHold::kHoldMillis));
    // And stops showing the countdown the moment it has fired.
    STIPPLE_CHECK(!hold.counting());
}
