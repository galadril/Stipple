// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/platform/Input.h"

namespace notrix {
namespace input {

/// The way back into a device nobody can reach (ADR 0018).
///
/// Everything built before this was safe to get wrong: a bad overlay is ugly, a
/// bad timezone is an hour out, a crashed process is a power cycle away from the
/// stock firmware. Wi-Fi credentials and an access password are not. They live
/// in /data, they survive a reboot, and a device that will not join a network or
/// will not accept a password has no remaining route in - USB-C on this hardware
/// is mass storage, no serial header is exposed, and ADB arrives over the very
/// network that just broke.
///
/// So this exists before either of those features does. It needs no network, no
/// password and no tools: hold **minus and plus together** for five seconds and
/// the device clears its access password and starts its hotspot.
///
/// It deliberately does **not** factory reset. Somebody locked out of a clock
/// wants their apps and settings to still be there when they get back in, and a
/// rescue that costs a week of an integration's work is one people avoid using
/// until it is too late.
///
/// Pure, and fed timestamps rather than reading a clock, so the whole thing is
/// testable without waiting five real seconds.
class Rescue {
public:
    /// How long both buttons must be held. Long enough that a hand resting on
    /// the case cannot do it, short enough to hold deliberately while reading a
    /// countdown.
    static constexpr std::uint64_t kHoldMillis = 5000;

    /// Feed every raw event, before the mapper sees it.
    ///
    /// Before, not instead: the gesture must work when the device is showing a
    /// notification, sitting in settings, or refusing an unauthenticated
    /// request. A rescue that can be blocked by whatever is on screen is not a
    /// rescue.
    void handle(const platform::InputEvent& event) noexcept;

    /// Call once per tick. Returns true exactly once, on the tick the hold
    /// completes - so a caller can act without having to remember whether it
    /// already did.
    bool tick(std::uint64_t nowMillis) noexcept;

    /// Whether both buttons are down and the countdown is running.
    bool counting() const noexcept { return counting_; }

    /// Milliseconds left, for the panel to show. Zero when not counting.
    ///
    /// Shown, rather than the gesture happening silently, because a device that
    /// resets itself with no warning is indistinguishable from a device that
    /// crashed - and because somebody who started the hold by accident needs a
    /// reason to stop.
    std::uint64_t remainingMillis(std::uint64_t nowMillis) const noexcept;

    /// Forget any hold in progress.
    void reset() noexcept;

private:
    bool minusDown_ = false;
    bool plusDown_ = false;

    /// When both became held. Only meaningful while `counting_`.
    std::uint64_t since_ = 0;
    bool counting_ = false;

    /// Set once the hold completes, so releasing and re-holding is needed to
    /// fire again rather than it repeating every tick.
    bool fired_ = false;
};

}  // namespace input
}  // namespace notrix
