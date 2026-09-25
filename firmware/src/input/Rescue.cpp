// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/input/Rescue.h"

namespace stipple {
namespace input {

void Rescue::handle(const platform::InputEvent& event) noexcept {
    const bool down = event.phase == platform::ButtonPhase::Down;
    const bool up = event.phase == platform::ButtonPhase::Up;
    if (!down && !up) {
        return;  // a rotary detent is neither, and cannot be held
    }

    if (event.source == platform::RawInput::KeyMinus) {
        minusDown_ = down;
    } else if (event.source == platform::RawInput::KeyPlus) {
        plusDown_ = down;
    } else {
        return;
    }

    if (minusDown_ && plusDown_) {
        if (!counting_) {
            counting_ = true;
            since_ = event.timestampMillis;
        }
        return;
    }

    // Either button released. The countdown restarts from scratch next time
    // rather than resuming - holding for four seconds, letting go, and holding
    // for one more should not add up to a rescue nobody meant.
    counting_ = false;
    fired_ = false;
}

bool Rescue::tick(std::uint64_t nowMillis) noexcept {
    if (!counting_ || fired_) {
        return false;
    }

    // A clock that steps backwards - NTP correcting - must not be able to
    // complete the hold early or restart it forever. Treating it as the moment
    // the hold began does neither.
    if (nowMillis < since_) {
        since_ = nowMillis;
        return false;
    }

    if (nowMillis - since_ < kHoldMillis) {
        return false;
    }

    // Fires once. The caller does something irreversible with this, and a
    // gesture that repeated every tick while the buttons stayed down would do
    // it thirty times a second.
    fired_ = true;
    return true;
}

std::uint64_t Rescue::remainingMillis(std::uint64_t nowMillis) const noexcept {
    if (!counting_ || fired_ || nowMillis < since_) {
        return 0;
    }
    const std::uint64_t elapsed = nowMillis - since_;
    return elapsed >= kHoldMillis ? 0 : kHoldMillis - elapsed;
}

void Rescue::reset() noexcept {
    minusDown_ = false;
    plusDown_ = false;
    counting_ = false;
    fired_ = false;
    since_ = 0;
}

}  // namespace input
}  // namespace stipple
