// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/input/InputMapper.h"

#include "support/TestFramework.h"

using notrix::input::Action;
using notrix::input::ActionEvent;
using notrix::input::ButtonBinding;
using notrix::input::InputMapper;
using notrix::input::InputMapperConfig;
using notrix::platform::ButtonPhase;
using notrix::platform::InputEvent;
using notrix::platform::RawInput;

namespace {

int actionCode(Action action) {
    return static_cast<int>(action);
}

/// Feed a press of the given duration and report the resulting action.
ActionEvent press(InputMapper& mapper,
                  RawInput source,
                  std::uint64_t downMillis,
                  std::uint64_t durationMillis) {
    ActionEvent result;
    ActionEvent ignored;
    mapper.handle(InputEvent({source, ButtonPhase::Down, downMillis}), ignored);
    mapper.handle(InputEvent({source, ButtonPhase::Up, downMillis + durationMillis}), result);
    return result;
}

ActionEvent rotate(InputMapper& mapper, bool clockwise, std::uint64_t timestampMillis) {
    ActionEvent result;
    mapper.handle(InputEvent({clockwise ? RawInput::RotaryRight : RawInput::RotaryLeft,
                              ButtonPhase::Tick, timestampMillis}),
                  result);
    return result;
}

}  // namespace

// --- the default layout ------------------------------------------------------
//
// These pin the mapping an unconfigured device ships with. They are not testing
// the mapper so much as the claim in ADR 0016 about which controls the TC002
// actually has — if that turns out to be wrong, these are what should fail.

NOTRIX_TEST(InputMapper, NavigationLivesOnTheKnob) {
    InputMapper mapper;

    NOTRIX_CHECK_EQ(actionCode(rotate(mapper, true, 0).action), actionCode(Action::AppNext));
    mapper.reset();
    NOTRIX_CHECK_EQ(actionCode(rotate(mapper, false, 0).action), actionCode(Action::AppPrevious));

    NOTRIX_CHECK_EQ(actionCode(press(mapper, RawInput::RotaryPress, 0, 50).action),
                    actionCode(Action::AppAction));
    NOTRIX_CHECK_EQ(actionCode(press(mapper, RawInput::RotaryPress, 1000, 900).action),
                    actionCode(Action::NotificationDismiss));
}

NOTRIX_TEST(InputMapper, TapAdjustsVolumeAndHoldAdjustsBrightness) {
    InputMapper mapper;

    NOTRIX_CHECK_EQ(actionCode(press(mapper, RawInput::KeyMinus, 0, 50).action),
                    actionCode(Action::VolumeDown));
    NOTRIX_CHECK_EQ(actionCode(press(mapper, RawInput::KeyPlus, 1000, 50).action),
                    actionCode(Action::VolumeUp));

    NOTRIX_CHECK_EQ(actionCode(press(mapper, RawInput::KeyMinus, 2000, 900).action),
                    actionCode(Action::BrightnessDown));
    NOTRIX_CHECK_EQ(actionCode(press(mapper, RawInput::KeyPlus, 3000, 900).action),
                    actionCode(Action::BrightnessUp));
}

NOTRIX_TEST(InputMapper, TheThirdButtonDoesSomethingIfItExists) {
    // Modelled because an unused enum value is invisible, whereas a real button
    // bound to nothing reads as broken firmware. See ADR 0016's amendment.
    InputMapper mapper;

    NOTRIX_CHECK_EQ(actionCode(press(mapper, RawInput::KeyExtra, 0, 50).action),
                    actionCode(Action::AppNext));
    NOTRIX_CHECK_EQ(actionCode(press(mapper, RawInput::KeyExtra, 1000, 900).action),
                    actionCode(Action::NotificationDismiss));
}

NOTRIX_TEST(InputMapper, EachControlTracksItsOwnPressIndependently) {
    // Four controls now share a press-start array; an off-by-one in buttonIndex
    // would let one button consume another's timestamp.
    InputMapper mapper;
    ActionEvent result;

    const RawInput buttons[] = {RawInput::KeyMinus, RawInput::KeyPlus, RawInput::KeyExtra,
                                RawInput::RotaryPress};

    // Press all of them, then release in the same order at staggered times.
    for (std::size_t i = 0; i < 4; ++i) {
        mapper.handle(InputEvent({buttons[i], ButtonPhase::Down, i * 10u}), result);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        // Each was held for a different span; none may be reported as long.
        mapper.handle(InputEvent({buttons[i], ButtonPhase::Up, 40u + i * 10u}), result);
        NOTRIX_CHECK_FALSE(result.longPress);
    }
}

NOTRIX_TEST(InputMapper, MinusAndPlusNeverDisagreeAboutDirection) {
    // A layout where − raised something would be a genuine usability bug, and an
    // easy one to introduce while remapping.
    const InputMapperConfig config;
    NOTRIX_CHECK_EQ(actionCode(config.keyMinus.shortPress), actionCode(Action::VolumeDown));
    NOTRIX_CHECK_EQ(actionCode(config.keyMinus.longPress), actionCode(Action::BrightnessDown));
    NOTRIX_CHECK_EQ(actionCode(config.keyPlus.shortPress), actionCode(Action::VolumeUp));
    NOTRIX_CHECK_EQ(actionCode(config.keyPlus.longPress), actionCode(Action::BrightnessUp));
}

NOTRIX_TEST(InputMapper, EveryPhysicalControlIsReachable) {
    // A control the mapper does not recognise is a button that does nothing on a
    // finished device, which is the hardest kind of bug to notice from code.
    const RawInput controls[] = {RawInput::KeyMinus,    RawInput::KeyPlus,
                                 RawInput::KeyExtra,    RawInput::RotaryPress,
                                 RawInput::RotaryLeft,  RawInput::RotaryRight};

    for (RawInput control : controls) {
        InputMapper mapper;
        ActionEvent result;
        bool fired = false;

        if (control == RawInput::RotaryLeft || control == RawInput::RotaryRight) {
            fired = mapper.handle(InputEvent({control, ButtonPhase::Tick, 0}), result);
        } else {
            mapper.handle(InputEvent({control, ButtonPhase::Down, 0}), result);
            fired = mapper.handle(InputEvent({control, ButtonPhase::Up, 50}), result);
        }

        NOTRIX_CHECK(fired);
        NOTRIX_CHECK(actionCode(result.action) != actionCode(Action::None));
    }
}

// --- presses -----------------------------------------------------------------

NOTRIX_TEST(InputMapper, ShortPressEmitsTheShortAction) {
    InputMapper mapper;
    const ActionEvent result = press(mapper, RawInput::KeyPlus, 1000, 50);

    NOTRIX_CHECK_EQ(actionCode(result.action), actionCode(Action::VolumeUp));
    NOTRIX_CHECK_FALSE(result.longPress);
    NOTRIX_CHECK_EQ(result.repeat, 1);
}

NOTRIX_TEST(InputMapper, LongPressEmitsTheLongAction) {
    InputMapper mapper;
    const ActionEvent result = press(mapper, RawInput::RotaryPress, 1000, 900);

    NOTRIX_CHECK_EQ(actionCode(result.action), actionCode(Action::NotificationDismiss));
    NOTRIX_CHECK(result.longPress);
}

NOTRIX_TEST(InputMapper, PressIsDecidedOnReleaseNotOnPressDown) {
    // Nothing may fire while a button is still held, or a long press would also
    // trigger the short action on its way down.
    InputMapper mapper;
    ActionEvent result;
    const bool fired =
        mapper.handle(InputEvent({RawInput::KeyPlus, ButtonPhase::Down, 100}), result);

    NOTRIX_CHECK_FALSE(fired);
    NOTRIX_CHECK_EQ(actionCode(result.action), actionCode(Action::None));
}

NOTRIX_TEST(InputMapper, ThresholdIsInclusive) {
    InputMapper mapper;
    const std::uint32_t threshold = mapper.config().longPressMillis;

    NOTRIX_CHECK(press(mapper, RawInput::RotaryPress, 0, threshold).longPress);
    NOTRIX_CHECK_FALSE(press(mapper, RawInput::RotaryPress, 0, threshold - 1).longPress);
}

NOTRIX_TEST(InputMapper, LongPressFallsBackWhenOnlyAShortBindingExists) {
    // A control bound only on short press should still do the obvious thing when
    // held, rather than feeling like a dead button.
    InputMapperConfig config;
    config.keyMinus = ButtonBinding{Action::AppPrevious, Action::None};
    InputMapper mapper(config);

    const ActionEvent result = press(mapper, RawInput::KeyMinus, 0, 5000);

    NOTRIX_CHECK_EQ(actionCode(result.action), actionCode(Action::AppPrevious));
    NOTRIX_CHECK_FALSE(result.longPress);
}

NOTRIX_TEST(InputMapper, ReleaseWithoutPressIsIgnored) {
    // A duplicate or orphaned Up must not synthesise an action.
    InputMapper mapper;
    ActionEvent result;
    NOTRIX_CHECK_FALSE(mapper.handle(InputEvent({RawInput::KeyPlus, ButtonPhase::Up, 500}), result));
}

NOTRIX_TEST(InputMapper, ResetDiscardsAnInFlightPress) {
    InputMapper mapper;
    ActionEvent result;

    mapper.handle(InputEvent({RawInput::KeyPlus, ButtonPhase::Down, 100}), result);
    mapper.reset();

    NOTRIX_CHECK_FALSE(mapper.handle(InputEvent({RawInput::KeyPlus, ButtonPhase::Up, 200}), result));
}

NOTRIX_TEST(InputMapper, BackwardsTimestampDoesNotBecomeALongPress) {
    // A clock that stepped backwards must not underflow into a huge duration.
    InputMapper mapper;
    ActionEvent result;

    mapper.handle(InputEvent({RawInput::RotaryPress, ButtonPhase::Down, 10000}), result);
    mapper.handle(InputEvent({RawInput::RotaryPress, ButtonPhase::Up, 5000}), result);

    NOTRIX_CHECK_FALSE(result.longPress);
    NOTRIX_CHECK_EQ(actionCode(result.action), actionCode(Action::AppAction));
}

NOTRIX_TEST(InputMapper, UnboundButtonProducesNothing) {
    InputMapperConfig config;
    config.keyMinus = ButtonBinding{Action::None, Action::None};
    InputMapper mapper(config);

    ActionEvent result;
    mapper.handle(InputEvent({RawInput::KeyMinus, ButtonPhase::Down, 0}), result);
    NOTRIX_CHECK_FALSE(mapper.handle(InputEvent({RawInput::KeyMinus, ButtonPhase::Up, 50}), result));
}

NOTRIX_TEST(InputMapper, EachButtonTracksItsOwnPressIndependently) {
    // Interleaved presses must not steal each other's start time.
    InputMapper mapper;
    ActionEvent result;

    mapper.handle(InputEvent({RawInput::KeyMinus, ButtonPhase::Down, 0}), result);
    mapper.handle(InputEvent({RawInput::KeyPlus, ButtonPhase::Down, 100}), result);

    mapper.handle(InputEvent({RawInput::KeyPlus, ButtonPhase::Up, 150}), result);
    NOTRIX_CHECK_FALSE(result.longPress);  // held 50 ms

    mapper.handle(InputEvent({RawInput::KeyMinus, ButtonPhase::Up, 900}), result);
    NOTRIX_CHECK(result.longPress);  // held 900 ms
    NOTRIX_CHECK_EQ(actionCode(result.action), actionCode(Action::BrightnessDown));
}

// --- rotary ------------------------------------------------------------------

NOTRIX_TEST(InputMapper, RotaryDirectionMapsToCarouselMovement) {
    InputMapper mapper;
    NOTRIX_CHECK_EQ(actionCode(rotate(mapper, true, 0).action), actionCode(Action::AppNext));

    mapper.reset();
    NOTRIX_CHECK_EQ(actionCode(rotate(mapper, false, 0).action), actionCode(Action::AppPrevious));
}

NOTRIX_TEST(InputMapper, SlowRotationDoesNotAccelerate) {
    InputMapper mapper;
    const std::uint64_t gap = mapper.config().rotaryAccelerationWindowMillis + 50;

    NOTRIX_CHECK_EQ(rotate(mapper, true, 0).repeat, 1);
    NOTRIX_CHECK_EQ(rotate(mapper, true, gap).repeat, 1);
    NOTRIX_CHECK_EQ(rotate(mapper, true, gap * 2).repeat, 1);
}

NOTRIX_TEST(InputMapper, FastRotationAccelerates) {
    InputMapper mapper;

    NOTRIX_CHECK_EQ(rotate(mapper, true, 0).repeat, 1);
    NOTRIX_CHECK_EQ(rotate(mapper, true, 10).repeat, 2);
    NOTRIX_CHECK_EQ(rotate(mapper, true, 20).repeat, 3);
}

NOTRIX_TEST(InputMapper, AccelerationIsCapped) {
    // A fast spin must not skip an unbounded number of apps.
    InputMapper mapper;
    const int cap = mapper.config().maxRotaryRepeat;

    ActionEvent result;
    for (int i = 0; i < 50; ++i) {
        result = rotate(mapper, true, static_cast<std::uint64_t>(i) * 5u);
    }
    NOTRIX_CHECK_EQ(result.repeat, cap);
}

NOTRIX_TEST(InputMapper, ReversingDirectionResetsAcceleration) {
    // Correcting an overshoot should be precise, not fling the carousel back.
    InputMapper mapper;

    rotate(mapper, true, 0);
    rotate(mapper, true, 10);
    rotate(mapper, true, 20);

    NOTRIX_CHECK_EQ(rotate(mapper, false, 30).repeat, 1);
}

NOTRIX_TEST(InputMapper, RotaryAccelerationIsConfigurable) {
    InputMapperConfig config;
    config.rotaryAccelerationWindowMillis = 5;
    InputMapper mapper(config);

    NOTRIX_CHECK_EQ(rotate(mapper, true, 0).repeat, 1);
    NOTRIX_CHECK_EQ(rotate(mapper, true, 50).repeat, 1);  // outside the tighter window
}

NOTRIX_TEST(InputMapper, RotaryPressIsSeparateFromRotation) {
    // Pushing the encoder must not disturb rotation acceleration state.
    InputMapper mapper;

    rotate(mapper, true, 0);
    NOTRIX_CHECK_EQ(rotate(mapper, true, 10).repeat, 2);

    press(mapper, RawInput::RotaryPress, 15, 20);

    NOTRIX_CHECK_EQ(rotate(mapper, true, 40).repeat, 3);
}
