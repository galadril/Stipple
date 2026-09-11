// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/input/InputMapper.h"

namespace notrix {
namespace input {

using platform::ButtonPhase;
using platform::InputEvent;
using platform::RawInput;

InputMapper::InputMapper(const InputMapperConfig& config) noexcept : config_(config) {
    reset();
}

void InputMapper::reset() noexcept {
    for (int i = 0; i < kButtonCount; ++i) {
        pressStart_[i] = kNoPress;
    }
    hasRotaryHistory_ = false;
    lastRotaryMillis_ = 0;
    lastRotaryWasRight_ = false;
    rotaryRepeat_ = 1;
}

int InputMapper::buttonIndex(RawInput source) noexcept {
    switch (source) {
        case RawInput::KeyLeft:
            return 0;
        case RawInput::KeyMiddle:
            return 1;
        case RawInput::KeyRight:
            return 2;
        case RawInput::RotaryPress:
            return 3;
        case RawInput::RotaryLeft:
        case RawInput::RotaryRight:
            break;
    }
    return -1;
}

const ButtonBinding& InputMapper::bindingFor(RawInput source) const noexcept {
    switch (source) {
        case RawInput::KeyLeft:
            return config_.keyLeft;
        case RawInput::KeyMiddle:
            return config_.keyMiddle;
        case RawInput::KeyRight:
            return config_.keyRight;
        case RawInput::RotaryPress:
        default:
            return config_.rotaryPress;
    }
}

bool InputMapper::handle(const InputEvent& event, ActionEvent& out) noexcept {
    out = ActionEvent{};

    if (event.source == RawInput::RotaryLeft || event.source == RawInput::RotaryRight) {
        const bool isRight = event.source == RawInput::RotaryRight;

        // Accelerate only while detents keep arriving quickly in the same
        // direction. A reversal restarts at 1, so correcting an overshoot is
        // precise instead of flinging the carousel back the other way.
        if (hasRotaryHistory_ && isRight == lastRotaryWasRight_ &&
            event.timestampMillis >= lastRotaryMillis_ &&
            (event.timestampMillis - lastRotaryMillis_) <=
                static_cast<std::uint64_t>(config_.rotaryAccelerationWindowMillis)) {
            if (rotaryRepeat_ < config_.maxRotaryRepeat) {
                ++rotaryRepeat_;
            }
        } else {
            rotaryRepeat_ = 1;
        }

        hasRotaryHistory_ = true;
        lastRotaryWasRight_ = isRight;
        lastRotaryMillis_ = event.timestampMillis;

        out.action = isRight ? Action::AppNext : Action::AppPrevious;
        out.repeat = rotaryRepeat_ < 1 ? 1 : rotaryRepeat_;
        return out.action != Action::None;
    }

    const int index = buttonIndex(event.source);
    if (index < 0) {
        return false;
    }

    if (event.phase == ButtonPhase::Down) {
        pressStart_[index] = event.timestampMillis;
        return false;  // nothing is decided until release
    }

    if (event.phase != ButtonPhase::Up) {
        return false;
    }

    const std::uint64_t start = pressStart_[index];
    pressStart_[index] = kNoPress;

    // A release with no matching press — the first event after reset(), or a
    // duplicate Up — is not an action.
    if (start == kNoPress) {
        return false;
    }

    // Guard against a non-monotonic timestamp rather than underflowing into a
    // gigantic duration that would read as a long press.
    const std::uint64_t duration =
        event.timestampMillis >= start ? event.timestampMillis - start : 0;

    const bool isLong = duration >= static_cast<std::uint64_t>(config_.longPressMillis);
    const ButtonBinding& binding = bindingFor(event.source);

    out.action = isLong ? binding.longPress : binding.shortPress;
    out.longPress = isLong;
    out.repeat = 1;

    // A long press bound to nothing falls back to the short-press action, so
    // holding a button that has no long binding still does the obvious thing
    // instead of feeling dead.
    if (out.action == Action::None && isLong) {
        out.action = binding.shortPress;
        out.longPress = false;
    }

    return out.action != Action::None;
}

}  // namespace input
}  // namespace notrix
