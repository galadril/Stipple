// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "stipple/core/Rgb.h"
#include "stipple/platform/PlatformServices.h"

namespace stipple {

class Canvas;

namespace apps {

struct BatteryStyle {
    /// Outline and text.
    Rgb color = colors::kWhite;

    /// Fill colour when the charge is comfortable.
    Rgb healthy = rgb(0, 200, 80);
    /// Fill colour below `lowPercent`. A battery app that looked identical at
    /// 90% and 9% would be decoration rather than information.
    Rgb low = rgb(255, 80, 0);
    int lowPercent = 20;

    /// The charging bolt. White by default so it reads against both fills.
    Rgb charging = colors::kWhite;

    /// Show the number beside the cell.
    bool showPercent = true;
};

/// A battery cell and its charge.
///
/// Renders "unknown" rather than a plausible zero when the platform cannot
/// report a battery. The value comes from IPowerSource, which says explicitly
/// whether it knows, precisely so this app never has to guess.
void renderBattery(Canvas& canvas,
                   const platform::BatteryStatus& status,
                   const BatteryStyle& style);

}  // namespace apps
}  // namespace stipple
