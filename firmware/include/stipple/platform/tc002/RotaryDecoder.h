// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace stipple {
namespace platform {
namespace tc002 {

/// What one `knob_key` ABS_X value means.
enum class Detent : std::uint8_t {
    None,              ///< not a detent boundary; emit nothing
    Clockwise,
    CounterClockwise,
};

/// Decode the TC002 knob.
///
/// `knob_key` reports `EV_ABS` on `ABS_X`, but the value is not a position: it
/// never ramps, and only four values ever appear. Captured from the hardware
/// while the knob was turned:
///
/// ```
/// (13,11) (8,1) (8,1) (13,11) (8,1) (8,1) (8,1) (13,11) (13,11) (13,11)
/// ```
///
/// **Each detent emits a pair**, always in that order, never a lone value and
/// never reversed. The pair identifies the direction; the second value exists
/// only so the next detent has something to change *to* — evdev suppresses an
/// `EV_ABS` event whose value is unchanged, so a driver that reported one
/// constant per direction would go silent on the second click.
///
/// So a detent is a pair, and the tick belongs on the leading value. Firing on
/// every value — which is what the first implementation did — advances the
/// carousel twice per click, which is precisely how this was reported: "it
/// swaps 2 apps while 1 knob rotation section was done".
///
/// Unrecognised values yield `None` rather than a guess. A wrong direction is
/// worse than a missed detent, because it moves the carousel the way the user
/// did not turn.
constexpr Detent decodeRotary(std::int32_t value) noexcept {
    switch (value) {
        case 8:  return Detent::Clockwise;         // leading half of (8, 1)
        case 13: return Detent::CounterClockwise;  // leading half of (13, 11)

        // Trailing halves. Deliberately listed rather than left to the default,
        // so that a future reader sees they are known values being ignored on
        // purpose and not simply unhandled.
        case 1:
        case 11:
        default:
            return Detent::None;
    }
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
