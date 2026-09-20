// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "notrix/core/Rgb.h"
#include "notrix/platform/PlatformServices.h"

namespace notrix {

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
}  // namespace notrix
