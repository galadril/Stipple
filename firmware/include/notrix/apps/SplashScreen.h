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
    /// The travelling wave on the first page. Two lines, offset in phase:
    /// the blue one leads, the grey one trails. Both dimmer than the title,
    /// because this is the one thing on the panel carrying no information.
    Rgb waveColor = rgb(0, 110, 160);
    Rgb waveTrailColor = rgb(64, 68, 76);
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
/// **Two pages, and the split is functional rather than decorative.** The
/// first shows the name over a travelling wave; the second replaces the wave
/// with the detail line. Nothing about the network is claimed until the second
/// page, which buys the radio the ten to twenty seconds it needs to associate.
///
/// That matters because the alternative was measured and was worse: the detail
/// line was computed once at startup, said "no Wi-Fi", and went on saying it
/// while the device sat on the network serving its own configuration page.
/// Showing an answer before one exists is how a working device looks broken.
///
/// `durationMillis` is how long the splash will be shown in total, and drives
/// the progress rule on the bottom row. Zero omits it, which is what a caller
/// with no fixed duration should pass rather than inventing one.
/// `address` is only used when `durationMillis` is non-zero - that is, by the
/// splash rather than by a notice. On the second page it becomes the lower of
/// two lines, with `detail` above it, and the title is dropped: by then the
/// name has been on screen for five seconds and the panel is better spent on
/// the two things somebody actually needs.
///
/// The address still scrolls even with a line to itself. "192.168.1.238" is
/// about 78 pixels in the only font this device has, and the panel is 52.
void renderSplash(Canvas& canvas,
                  std::string_view title,
                  std::string_view detail,
                  std::uint64_t elapsedMillis,
                  std::uint64_t durationMillis = 0,
                  const SplashStyle& style = SplashStyle{},
                  std::string_view address = {});

/// Build the detail line: version plus address, or a plain statement that there
/// is no network rather than a blank space that reads like a failure.
///
/// Allocates, so the host builds this once at boot rather than per frame.
std::string splashDetail(std::string_view version, const platform::INetworkManager* network);

/// Just the address, or why there is not one, for the second page's lower
/// line. Same honesty as splashDetail: "no Wi-Fi" and "no network" are
/// different states and neither is a blank space (ADR 0013).
std::string splashAddress(const platform::INetworkManager* network);

}  // namespace apps
}  // namespace notrix
