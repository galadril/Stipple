// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/input/Rescue.h"

#include "support/TestFramework.h"

using stipple::input::Rescue;
using stipple::platform::ButtonPhase;
using stipple::platform::InputEvent;
using stipple::platform::RawInput;

namespace {

InputEvent press(RawInput source, std::uint64_t at) {
    InputEvent event;
    event.source = source;
    event.phase = ButtonPhase::Down;
    event.timestampMillis = at;
    return event;
}

InputEvent release(RawInput source, std::uint64_t at) {
    InputEvent event;
    event.source = source;
    event.phase = ButtonPhase::Up;
    event.timestampMillis = at;
    return event;
}

/// Hold both buttons from `at`, then run the clock forward to `until`.
bool holdFor(Rescue& rescue, std::uint64_t at, std::uint64_t until) {
    rescue.handle(press(RawInput::KeyMinus, at));
    rescue.handle(press(RawInput::KeyPlus, at));

    bool fired = false;
    for (std::uint64_t now = at; now <= until; now += 50) {
        if (rescue.tick(now)) {
            fired = true;
        }
    }
    return fired;
}

}  // namespace

STIPPLE_TEST(Rescue, BothButtonsHeldLongEnoughFires) {
    Rescue rescue;
    STIPPLE_CHECK(holdFor(rescue, 1000, 1000 + Rescue::kHoldMillis + 100));
}

STIPPLE_TEST(Rescue, ItFiresExactlyOnce) {
    // The caller does something irreversible with this. A gesture that repeated
    // every tick while the buttons stayed down would do it thirty times a
    // second.
    Rescue rescue;
    rescue.handle(press(RawInput::KeyMinus, 0));
    rescue.handle(press(RawInput::KeyPlus, 0));

    int fires = 0;
    for (std::uint64_t now = 0; now < Rescue::kHoldMillis * 3; now += 50) {
        if (rescue.tick(now)) {
            ++fires;
        }
    }
    STIPPLE_CHECK_EQ(fires, 1);
}

STIPPLE_TEST(Rescue, OneButtonIsNotEnough) {
    // Either alone is an ordinary adjustment somebody may hold for a while.
    Rescue rescue;
    rescue.handle(press(RawInput::KeyMinus, 0));

    bool fired = false;
    for (std::uint64_t now = 0; now < Rescue::kHoldMillis * 2; now += 50) {
        if (rescue.tick(now)) { fired = true; }
    }
    STIPPLE_CHECK_FALSE(fired);
    STIPPLE_CHECK_FALSE(rescue.counting());
}

STIPPLE_TEST(Rescue, LettingGoEarlyStartsOverRatherThanResuming) {
    // Four seconds, a pause, and one more second must not add up to a rescue
    // nobody meant to perform.
    Rescue rescue;
    rescue.handle(press(RawInput::KeyMinus, 0));
    rescue.handle(press(RawInput::KeyPlus, 0));
    for (std::uint64_t now = 0; now < Rescue::kHoldMillis - 1000; now += 50) {
        STIPPLE_CHECK_FALSE(rescue.tick(now));
    }

    rescue.handle(release(RawInput::KeyPlus, Rescue::kHoldMillis - 1000));
    STIPPLE_CHECK_FALSE(rescue.counting());

    // Held again, but only briefly.
    rescue.handle(press(RawInput::KeyPlus, Rescue::kHoldMillis - 900));
    bool fired = false;
    for (std::uint64_t now = Rescue::kHoldMillis - 900; now < Rescue::kHoldMillis + 500;
         now += 50) {
        if (rescue.tick(now)) { fired = true; }
    }
    STIPPLE_CHECK_FALSE(fired);
}

STIPPLE_TEST(Rescue, TheOrderTheButtonsGoDownDoesNotMatter) {
    // Nobody presses two buttons at the same instant.
    Rescue rescue;
    rescue.handle(press(RawInput::KeyPlus, 0));
    rescue.handle(press(RawInput::KeyMinus, 300));

    bool fired = false;
    for (std::uint64_t now = 300; now <= 300 + Rescue::kHoldMillis + 100; now += 50) {
        if (rescue.tick(now)) { fired = true; }
    }
    STIPPLE_CHECK(fired);
}

STIPPLE_TEST(Rescue, TheCountdownIsVisibleWhileItRuns) {
    // A device that resets itself with no warning is indistinguishable from one
    // that crashed, and somebody who started the hold by accident needs a
    // reason to stop.
    Rescue rescue;
    rescue.handle(press(RawInput::KeyMinus, 1000));
    rescue.handle(press(RawInput::KeyPlus, 1000));

    STIPPLE_CHECK(rescue.counting());
    STIPPLE_CHECK_EQ(rescue.remainingMillis(1000), Rescue::kHoldMillis);
    STIPPLE_CHECK_EQ(rescue.remainingMillis(3000), Rescue::kHoldMillis - 2000);

    // And it stops once spent, rather than reading as a fresh countdown.
    rescue.tick(1000 + Rescue::kHoldMillis);
    STIPPLE_CHECK_EQ(rescue.remainingMillis(1000 + Rescue::kHoldMillis), 0u);
}

STIPPLE_TEST(Rescue, NothingIsCountingBeforeAnythingIsHeld) {
    Rescue rescue;
    STIPPLE_CHECK_FALSE(rescue.counting());
    STIPPLE_CHECK_EQ(rescue.remainingMillis(10000), 0u);
    STIPPLE_CHECK_FALSE(rescue.tick(10000));
}

STIPPLE_TEST(Rescue, ARotaryDetentCannotBeHeld) {
    Rescue rescue;
    InputEvent tick;
    tick.source = RawInput::RotaryRight;
    tick.phase = ButtonPhase::Tick;
    rescue.handle(tick);
    STIPPLE_CHECK_FALSE(rescue.counting());
}

STIPPLE_TEST(Rescue, AClockSteppingBackwardsCannotCompleteTheHoldEarly) {
    // NTP correcting mid-hold. Neither finishing early nor restarting forever
    // is acceptable: one is a rescue nobody asked for, the other is a rescue
    // somebody needs and cannot have.
    Rescue rescue;
    rescue.handle(press(RawInput::KeyMinus, 10000));
    rescue.handle(press(RawInput::KeyPlus, 10000));

    STIPPLE_CHECK_FALSE(rescue.tick(5000));  // clock jumped back
    STIPPLE_CHECK(rescue.counting());

    // The hold now runs from the corrected time.
    STIPPLE_CHECK_FALSE(rescue.tick(5000 + Rescue::kHoldMillis - 100));
    STIPPLE_CHECK(rescue.tick(5000 + Rescue::kHoldMillis));
}

STIPPLE_TEST(Rescue, ResetForgetsAHoldInProgress) {
    Rescue rescue;
    rescue.handle(press(RawInput::KeyMinus, 0));
    rescue.handle(press(RawInput::KeyPlus, 0));
    STIPPLE_CHECK(rescue.counting());

    rescue.reset();
    STIPPLE_CHECK_FALSE(rescue.counting());
    STIPPLE_CHECK_FALSE(rescue.tick(Rescue::kHoldMillis * 2));
}
