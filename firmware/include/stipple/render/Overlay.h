// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "stipple/core/Rgb.h"

namespace stipple {

class Framebuffer;

namespace render {

/// Scenery drawn over whatever app is showing (DESIGN.md §7).
///
/// Deliberately a short, physical list. These are the things a pixel clock can
/// suggest in a handful of pixels, not a taxonomy of meteorological conditions
/// — "light rain showers" and "rain" look identical at this size, so offering
/// both would be a lie told in a dropdown. The test for adding one is whether
/// it is *recognisable* at 52x16 and distinct from everything already here;
/// a longer menu of things that look the same is not more choice.
///
/// Most are weather. Sparkle and Confetti are not, and are here because an
/// occasion is a thing a clock on a shelf is asked to mark.
enum class Overlay : std::uint8_t {
    None,
    Rain,
    Snow,
    Storm,
    Frost,
    /// Slow twinkling points. A clear night.
    Stars,
    /// Dim bands drifting sideways.
    Fog,
    /// Occasional bright glints, gone as quickly as they arrive.
    Sparkle,
    /// Falling flecks that tumble and are not all one colour.
    Confetti,
};

Overlay overlayFromName(std::string_view name) noexcept;
const char* overlayName(Overlay overlay) noexcept;

/// Number of overlays, for a settings UI that wants to list them.
inline constexpr int kOverlayCount = 9;
Overlay overlayAt(int index) noexcept;

struct OverlayStyle {
    /// Cool and dim, so a raindrop never reads as part of the time. Deliberately
    /// neither the primary nor the accent role (DESIGN.md §3).
    ///
    /// Confetti ignores this and picks its own colours, because grey confetti
    /// is not confetti. It is the one overlay whose whole point is colour.
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
}  // namespace stipple
