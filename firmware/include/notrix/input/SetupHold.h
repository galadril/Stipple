// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/platform/Input.h"

namespace notrix {
namespace input {

/// Setup mode on request: hold the knob.
///
/// The rescue gesture (Rescue) answers "nobody can reach this device". This
/// answers a much more ordinary question - "I would like to reconfigure it" -
/// and so it is deliberately weaker. It clears no password, forgets no
/// network and changes nothing that survives a reboot. All it does is ask for
/// the hotspot.
///
/// **It does not conflict with the settings screen, despite sharing a
/// button.** `rotaryPress` is bound to SettingsToggle on long press, and the
/// mapper decides long-versus-short *on release*. This fires while the knob
/// is still down, at five seconds, and the host discards the in-progress
/// press when it does - so a normal hold still opens settings on release, and
/// only a hold that keeps going becomes setup mode.
///
/// Pure, and fed timestamps rather than reading a clock, so it is testable
/// without waiting five real seconds.
class SetupHold {
public:
    /// Ten times the mapper's long-press threshold, and the same as the
    /// rescue gesture. Long enough that nobody reaches it while using the
    /// settings screen normally, and short enough to hold deliberately while
    /// watching a countdown.
    static constexpr std::uint64_t kHoldMillis = 5000;

    /// Feed every raw event, before the mapper sees it.
    void handle(const platform::InputEvent& event) noexcept;

    /// Call once per tick. Returns true exactly once, on the tick the hold
    /// completes, so a caller can act without tracking whether it already did.
    bool tick(std::uint64_t nowMillis) noexcept;

    /// Whether the knob is down and the countdown is running.
    bool counting() const noexcept { return counting_; }

    /// Milliseconds left, for the panel to show. Zero when not counting.
    ///
    /// A gesture with no feedback is a gesture nobody discovers and nobody
    /// trusts - and somebody who started the hold by accident needs a reason
    /// to let go before it does anything.
    std::uint64_t remainingMillis(std::uint64_t nowMillis) const noexcept;

    /// Forget any hold in progress.
    void reset() noexcept;

private:
    bool down_ = false;

    /// When the knob went down. Only meaningful while `counting_`.
    std::uint64_t since_ = 0;
    bool counting_ = false;

    /// Set once the hold completes, so the knob must be released and pressed
    /// again to fire a second time rather than it repeating every tick.
    bool fired_ = false;
};

}  // namespace input
}  // namespace notrix
