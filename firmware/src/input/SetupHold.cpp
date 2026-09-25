// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/input/SetupHold.h"

namespace stipple {
namespace input {

void SetupHold::handle(const platform::InputEvent& event) noexcept {
    // Only the knob. KeyMiddle is a separate button on this hardware and is
    // bound to Back, where a hidden five-second meaning would be a trap.
    if (event.source != platform::RawInput::RotaryPress) {
        return;
    }

    switch (event.phase) {
        case platform::ButtonPhase::Down:
            down_ = true;
            since_ = event.timestampMillis;
            counting_ = true;
            visible_ = false;
            fired_ = false;
            break;
        case platform::ButtonPhase::Up:
            // Releasing always ends it, whether or not it fired. Re-arming
            // here rather than in tick() is what makes a second setup request
            // need a second deliberate hold.
            down_ = false;
            counting_ = false;
            visible_ = false;
            fired_ = false;
            break;
        case platform::ButtonPhase::Tick:
            // Detents have no duration and cannot be held.
            break;
    }
}

bool SetupHold::tick(std::uint64_t nowMillis) noexcept {
    if (!counting_ || fired_ || !down_) {
        return false;
    }

    // Held long enough to mean it, so the panel may say so now.
    visible_ = nowMillis >= since_ && (nowMillis - since_) >= kRevealMillis;
    // Guarded rather than assumed: a clock that has not reached `since_` yet
    // would underflow the subtraction and fire instantly.
    if (nowMillis < since_ || nowMillis - since_ < kHoldMillis) {
        return false;
    }
    fired_ = true;
    counting_ = false;
    visible_ = false;
    return true;
}

std::uint64_t SetupHold::remainingMillis(std::uint64_t nowMillis) const noexcept {
    if (!counting_ || nowMillis < since_) {
        return 0;
    }
    const std::uint64_t held = nowMillis - since_;
    return held >= kHoldMillis ? 0 : kHoldMillis - held;
}

void SetupHold::reset() noexcept {
    down_ = false;
    since_ = 0;
    counting_ = false;
    visible_ = false;
    fired_ = false;
}

}  // namespace input
}  // namespace stipple
