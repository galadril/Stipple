// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace notrix {
namespace platform {

/// Time, split into the two kinds that behave differently.
///
/// Monotonic time always advances and is what animation, timeouts and press
/// durations must use. Wall-clock time is what the user sees, and on a device
/// with no RTC battery it is simply wrong until NTP lands — hence
/// `wallClockValid()`. A clock app that renders 01:00 on a cold boot because it
/// trusted an unsynchronised wall clock is the exact failure this separation
/// prevents.
class ISystemClock {
public:
    virtual ~ISystemClock() = default;

    /// Milliseconds since boot. Never decreases, never jumps when the wall
    /// clock is corrected.
    virtual std::uint64_t monotonicMillis() const = 0;

    /// Has the wall clock been set from a trustworthy source (NTP, user)?
    virtual bool wallClockValid() const = 0;

    /// Seconds since the Unix epoch, UTC. Meaningless unless
    /// `wallClockValid()` is true.
    virtual std::int64_t unixSeconds() const = 0;

    /// Offset applied to UTC for local display, in seconds. Kept separate from
    /// `unixSeconds` so stored timestamps stay unambiguous.
    virtual int utcOffsetSeconds() const = 0;
};

}  // namespace platform
}  // namespace notrix
