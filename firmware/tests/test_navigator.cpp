// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/input/Navigator.h"

#include "support/TestFramework.h"

using notrix::input::Mode;
using notrix::input::Navigator;
using notrix::input::SettingSlot;
using notrix::input::settingLabel;

namespace {

int slotCode(SettingSlot slot) { return static_cast<int>(slot); }

}  // namespace

NOTRIX_TEST(Navigator, StartsOnTheCarousel) {
    // The device spends almost all of its life here, and a clock that booted
    // into a menu would be a clock nobody could read.
    Navigator navigator;
    NOTRIX_CHECK(navigator.mode() == Mode::Browsing);
    NOTRIX_CHECK_FALSE(navigator.inSettings());
}

NOTRIX_TEST(Navigator, HoldingTheKnobEntersAndLeaves) {
    Navigator navigator;

    navigator.toggleSettings(1000);
    NOTRIX_CHECK(navigator.inSettings());

    navigator.toggleSettings(2000);
    NOTRIX_CHECK_FALSE(navigator.inSettings());
}

NOTRIX_TEST(Navigator, EnteringAlwaysStartsAtTheTop) {
    // Resuming where the last visit left off sounds helpful and is not: the
    // panel shows one setting at a time, so opening on whatever was touched an
    // hour ago gives no clue anything exists above it.
    Navigator navigator;

    navigator.toggleSettings(1000);
    navigator.moveCursor(2, 1100);
    NOTRIX_CHECK(navigator.current() != SettingSlot::Brightness);

    navigator.toggleSettings(1200);
    navigator.toggleSettings(1300);
    NOTRIX_CHECK_EQ(slotCode(navigator.current()), slotCode(SettingSlot::Brightness));
}

NOTRIX_TEST(Navigator, TheCursorWrapsAtBothEnds) {
    // Five settings on a panel that shows one at a time should not have ends a
    // user can get stuck against.
    Navigator navigator;
    navigator.toggleSettings(0);

    const int count = static_cast<int>(SettingSlot::Count);
    for (int i = 0; i < count; ++i) {
        navigator.moveCursor(1, 100);
    }
    NOTRIX_CHECK_EQ(slotCode(navigator.current()), slotCode(SettingSlot::Brightness));

    navigator.moveCursor(-1, 200);
    NOTRIX_CHECK_EQ(slotCode(navigator.current()), slotCode(SettingSlot::Volume));
}

NOTRIX_TEST(Navigator, AnUnavailableSettingIsSkippedNotShownDead) {
    // ADR 0013 on a 52x16 panel: the honest way to show an absent capability is
    // to leave the control out, not to list one that does nothing.
    Navigator navigator;
    navigator.setAvailable(SettingSlot::Volume, false);
    navigator.toggleSettings(0);

    for (int i = 0; i < 12; ++i) {
        navigator.moveCursor(1, 100);
        NOTRIX_CHECK(navigator.current() != SettingSlot::Volume);
    }
}

NOTRIX_TEST(Navigator, HidingWhatTheCursorIsOnMovesIt) {
    // Otherwise the cursor points at something invisible and the next turn of
    // the knob appears to skip a step.
    Navigator navigator;
    navigator.toggleSettings(0);
    navigator.moveCursor(3, 100);
    NOTRIX_REQUIRE(navigator.current() == SettingSlot::Volume);

    navigator.setAvailable(SettingSlot::Volume, false);
    NOTRIX_CHECK(navigator.current() != SettingSlot::Volume);
    NOTRIX_CHECK(navigator.available(navigator.current()));
}

NOTRIX_TEST(Navigator, SettingsCannotOutliveTheUsersAttention) {
    // The device has no way to say "you are in a menu" other than the menu
    // itself, so someone who walks away mid-adjustment would otherwise leave a
    // clock reading "BRIGHT 168" until the next person touched it.
    Navigator navigator;
    navigator.toggleSettings(1000);

    NOTRIX_CHECK_FALSE(navigator.tick(1000 + Navigator::kIdleExitMillis - 1));
    NOTRIX_CHECK(navigator.inSettings());

    NOTRIX_CHECK(navigator.tick(1000 + Navigator::kIdleExitMillis));
    NOTRIX_CHECK_FALSE(navigator.inSettings());
}

NOTRIX_TEST(Navigator, TheIdleExitFiresExactlyOnce) {
    // The host redraws on the returned true. Reporting it every frame
    // afterwards would pin the panel at full frame rate for nothing.
    Navigator navigator;
    navigator.toggleSettings(0);

    NOTRIX_CHECK(navigator.tick(Navigator::kIdleExitMillis));
    NOTRIX_CHECK_FALSE(navigator.tick(Navigator::kIdleExitMillis + 1));
    NOTRIX_CHECK_FALSE(navigator.tick(Navigator::kIdleExitMillis + 100000));
}

NOTRIX_TEST(Navigator, UsingItKeepsItOpen) {
    Navigator navigator;
    navigator.toggleSettings(0);

    // Something every few seconds, for well past the idle timeout.
    for (std::uint64_t at = 0; at < Navigator::kIdleExitMillis * 3; at += 2000) {
        navigator.noteActivity(at);
        NOTRIX_CHECK_FALSE(navigator.tick(at + 1));
    }
    NOTRIX_CHECK(navigator.inSettings());
}

NOTRIX_TEST(Navigator, AClockSteppingBackwardsNeitherStrandsNorSlamsShut) {
    // NTP correcting mid-session. Treating "now is before the last activity" as
    // an enormous idle gap would snap settings shut for no reason; ignoring it
    // would hold them open forever.
    Navigator navigator;
    navigator.toggleSettings(10'000);

    NOTRIX_CHECK_FALSE(navigator.tick(5'000));
    NOTRIX_CHECK(navigator.inSettings());

    // And the timer restarts from the corrected time rather than the old one.
    NOTRIX_CHECK_FALSE(navigator.tick(5'000 + Navigator::kIdleExitMillis - 1));
    NOTRIX_CHECK(navigator.tick(5'000 + Navigator::kIdleExitMillis));
}

NOTRIX_TEST(Navigator, TheCursorDoesNotMoveWhileBrowsing) {
    // The knob turns the carousel out here. A cursor that tracked it invisibly
    // would mean settings opened somewhere different each time.
    Navigator navigator;
    navigator.moveCursor(3, 100);
    NOTRIX_CHECK_EQ(slotCode(navigator.current()), slotCode(SettingSlot::Brightness));
}

NOTRIX_TEST(Navigator, EverySettingHasALabelThatFitsThePanel) {
    // 52 columns at 5px a character plus spacing leaves room for about eight,
    // and the value has to share the row.
    for (int i = 0; i < static_cast<int>(SettingSlot::Count); ++i) {
        const char* label = settingLabel(static_cast<SettingSlot>(i));
        NOTRIX_REQUIRE(label != nullptr);
        int length = 0;
        while (label[length] != 0) {
            ++length;
        }
        NOTRIX_CHECK(length > 0);
        NOTRIX_CHECK(length <= 8);
    }
}
