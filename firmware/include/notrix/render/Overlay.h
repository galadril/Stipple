// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "notrix/core/Rgb.h"

namespace notrix {

class Framebuffer;

namespace render {

/// Weather drawn over whatever app is showing (DESIGN.md §7).
///
/// Deliberately a short, physical list. These are the things a pixel clock can
/// suggest in a handful of pixels, not a taxonomy of meteorological conditions
/// — "light rain showers" and "rain" look identical at this size, so offering
/// both would be a lie told in a dropdown.
enum class Overlay : std::uint8_t {
    None,
    Rain,
    Snow,
    Storm,
    Frost,
};

Overlay overlayFromName(std::string_view name) noexcept;
const char* overlayName(Overlay overlay) noexcept;

/// Number of overlays, for a settings UI that wants to list them.
inline constexpr int kOverlayCount = 5;
Overlay overlayAt(int index) noexcept;

struct OverlayStyle {
    /// Cool and dim, so a raindrop never reads as part of the time. Deliberately
    /// neither the primary nor the accent role (DESIGN.md §3).
    Rgb color = rgb(90, 140, 200);

    /// 0–255, applied on top of the colour. Overlays are scenery; they should
    /// sit visibly below whatever they are drawn over.
    std::uint8_t intensity = 150;
};

/// Draw an overlay over a frame that has already been rendered.
///
/// **Additive.** Only pixels the app left black are touched, so a drop passes
/// behind the digits rather than through them. That single rule is what keeps
/// an overlay from ever making the content unreadable, and it is why this takes
/// a Framebuffer rather than a Canvas — it has to read what is already there.
///
/// A pure function of `elapsedMillis` (DESIGN.md §5.2), so it is golden-testable
/// and cannot drift between the emulator and the device.
void drawOverlay(Framebuffer& frame,
                 Overlay overlay,
                 std::uint64_t elapsedMillis,
                 const OverlayStyle& style = OverlayStyle{});

}  // namespace render
}  // namespace notrix
