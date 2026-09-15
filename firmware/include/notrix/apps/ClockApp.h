// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/core/Rgb.h"

namespace notrix {

class Canvas;

namespace platform {
class ISystemClock;
}

namespace apps {

struct ClockStyle {
    bool twentyFourHour = true;
    Rgb color = colors::kWhite;
    /// Colon blink period. Zero holds it lit — some people find the blink
    /// distracting, and it is the only moving part on an otherwise static face.
    std::uint32_t blinkPeriodMillis = 1000;
};

/// Local wall-clock time as HH:MM.
///
/// When the wall clock has not been set, this draws `--:--` rather than a
/// plausible-looking wrong time. A device that boots before NTP and confidently
/// shows 01:00 is worse than one that admits it does not know yet (§40).
///
/// Deliberately time-of-day only. Rendering a date needs a calendar, and a
/// calendar needs leap-year and month-length handling that is easy to get subtly
/// wrong; it can arrive with a real date app rather than be smuggled in here.
void renderClock(Canvas& canvas,
                 const platform::ISystemClock& clock,
                 const ClockStyle& style = ClockStyle{});

/// True when the clock face would change between these two moments, so a static
/// minute does not force a redraw every frame.
bool clockChanged(const platform::ISystemClock& clock,
                  const ClockStyle& style,
                  std::uint64_t previousMillis,
                  std::uint64_t nowMillis);

}  // namespace apps
}  // namespace notrix
