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
    Rgb titleColor = rgb(0, 200, 255);
    Rgb detailColor = rgb(150, 150, 150);
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
void renderSplash(Canvas& canvas,
                  std::string_view title,
                  std::string_view detail,
                  std::uint64_t elapsedMillis,
                  const SplashStyle& style = SplashStyle{});

/// Build the detail line: version plus address, or a plain statement that there
/// is no network rather than a blank space that reads like a failure.
///
/// Allocates, so the host builds this once at boot rather than per frame.
std::string splashDetail(std::string_view version, const platform::INetworkManager* network);

}  // namespace apps
}  // namespace notrix
