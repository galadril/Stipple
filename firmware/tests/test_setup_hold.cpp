// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/input/SetupHold.h"

#include "support/TestFramework.h"

using notrix::input::SetupHold;
using notrix::platform::ButtonPhase;
using notrix::platform::InputEvent;
using notrix::platform::RawInput;

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

NOTRIX_TEST(SetupHold, KnobHeldLongEnoughFires) {
    SetupHold hold;
    NOTRIX_CHECK(holdFor(hold, 1000, 1000 + SetupHold::kHoldMillis));
}

NOTRIX_TEST(SetupHold, ShortPressDoesNotFire) {
    SetupHold hold;
    // The settings toggle lives at 500 ms on this same button. A gesture that
    // fired anywhere near it would make the settings screen unusable.
    NOTRIX_CHECK(!holdFor(hold, 1000, 1900));
}

NOTRIX_TEST(SetupHold, FiresExactlyOncePerHold) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 0));

    int fires = 0;
    for (std::uint64_t now = 0; now <= SetupHold::kHoldMillis * 3; now += 50) {
        if (hold.tick(now)) {
            ++fires;
        }
    }
    NOTRIX_CHECK_EQ(fires, 1);
}

NOTRIX_TEST(SetupHold, ReleasingRearmsIt) {
    SetupHold hold;
    NOTRIX_CHECK(holdFor(hold, 0, SetupHold::kHoldMillis));
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Up, SetupHold::kHoldMillis));
    NOTRIX_CHECK(holdFor(hold, 20000, 20000 + SetupHold::kHoldMillis));
}

NOTRIX_TEST(SetupHold, ReleasingEarlyCancels) {
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
    NOTRIX_CHECK(!fired);
    NOTRIX_CHECK(!hold.counting());
}

NOTRIX_TEST(SetupHold, OtherButtonsAreIgnored) {
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
        NOTRIX_CHECK(!fired);
    }
    NOTRIX_CHECK(!hold.counting());
}

NOTRIX_TEST(SetupHold, DetentsCannotBeHeld) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryRight, ButtonPhase::Tick, 0));
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Tick, 0));
    NOTRIX_CHECK(!hold.counting());
    NOTRIX_CHECK(!hold.tick(SetupHold::kHoldMillis * 2));
}

NOTRIX_TEST(SetupHold, CountdownRunsDownAndStopsAtZero) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 1000));
    NOTRIX_CHECK_EQ(hold.remainingMillis(1000), SetupHold::kHoldMillis);
    NOTRIX_CHECK_EQ(hold.remainingMillis(3000), SetupHold::kHoldMillis - 2000);
    NOTRIX_CHECK_EQ(hold.remainingMillis(1000 + SetupHold::kHoldMillis), 0u);
}

NOTRIX_TEST(SetupHold, SurvivesTheClockSteppingBackwards) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 10000000));
    // Must not underflow into an instant fire.
    NOTRIX_CHECK(!hold.tick(5000));
    NOTRIX_CHECK_EQ(hold.remainingMillis(5000), 0u);
    NOTRIX_CHECK(hold.tick(10000000 + SetupHold::kHoldMillis));
}

NOTRIX_TEST(SetupHold, ResetForgetsAHoldInProgress) {
    SetupHold hold;
    hold.handle(event(RawInput::RotaryPress, ButtonPhase::Down, 0));
    hold.reset();
    NOTRIX_CHECK(!hold.counting());
    NOTRIX_CHECK(!hold.tick(SetupHold::kHoldMillis * 2));
}
