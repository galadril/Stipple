// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "notrix/core/Rgb.h"

namespace notrix {

class Canvas;

namespace platform {
class INetworkManager;
}

namespace apps {

struct SplashStyle {
    /// The product name, drawn emboldened. Accent rather than primary: the
    /// splash is the one screen where the brand outranks the content.
    Rgb titleColor = rgb(0, 200, 255);
    /// Deliberately well below the title, so the hierarchy reads at a glance
    /// even though both lines are the same 7 rows tall (DESIGN.md §8 - there is
    /// only one font, so weight and value carry the whole difference).
    Rgb detailColor = rgb(110, 110, 110);
    /// The draining rule along the bottom row.
    Rgb ruleColor = rgb(0, 90, 120);
};

/// The first thing the device shows (blueprint §40).
///
/// Two lines: the product name, and a detail line carrying the version and how
/// to reach the device. The detail scrolls when it does not fit, which it
/// usually will not — "0.1.0 - 192.168.1.42" is far wider than 52 pixels, and an
/// IP address truncated to "192..." is useless to someone trying to open the web
/// UI.
///
/// The splash exists to answer the two questions a person has when a new device
/// lights up: did it start, and where do I find it.
/// `durationMillis` is how long the splash will be shown in total, and drives
/// the progress rule on the bottom row. Zero omits it, which is what a caller
/// with no fixed duration should pass rather than inventing one.
void renderSplash(Canvas& canvas,
                  std::string_view title,
                  std::string_view detail,
                  std::uint64_t elapsedMillis,
                  std::uint64_t durationMillis = 0,
                  const SplashStyle& style = SplashStyle{});

/// Build the detail line: version plus address, or a plain statement that there
/// is no network rather than a blank space that reads like a failure.
///
/// Allocates, so the host builds this once at boot rather than per frame.
std::string splashDetail(std::string_view version, const platform::INetworkManager* network);

}  // namespace apps
}  // namespace notrix
