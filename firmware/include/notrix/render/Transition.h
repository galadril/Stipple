// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "notrix/graphics/Framebuffer.h"

namespace notrix {
namespace render {

/// How one app gives way to the next (DESIGN.md §6).
enum class TransitionStyle : std::uint8_t {
    /// Instant swap. What `apps.transitions = false` means, and what a device
    /// too busy to animate should fall back to.
    None,
    /// Outgoing leaves, incoming follows it in. App to app.
    Slide,
    /// Crossfade through black. App to notification and back, where a slide
    /// would imply the notification is just the next item in the rotation.
    Fade,
    /// The incoming frame is revealed column by column, travelling the way the
    /// knob went. Sharper than a slide: nothing moves, the new thing simply
    /// arrives across the panel.
    Wipe,
    /// Pixels change over in a fixed pseudo-random order. No direction, which
    /// makes it the one style that does not imply where the next app is.
    Dissolve,
};

/// Number of styles, for a settings UI that wants to list them.
inline constexpr int kTransitionStyleCount = 5;
TransitionStyle transitionStyleAt(int index) noexcept;

TransitionStyle transitionStyleFromName(std::string_view name) noexcept;
const char* transitionStyleName(TransitionStyle style) noexcept;

/// Which way the content travels.
///
/// Follows the input: turning the knob clockwise moves content left, the way
/// the knob went. A transition that contradicts the gesture reads as lag.
enum class TransitionDirection : std::uint8_t { Forward, Backward };

/// 300 ms (DESIGN.md §6). Long enough to read as motion, short enough that a
/// four-app carousel at eight seconds each does not spend a sixth of its life
/// in transit.
inline constexpr std::uint32_t kTransitionMillis = 300;

/// Composite two frames into one.
///
/// A pure function of its inputs, deliberately: an animation that depended on
/// how many frames happened to render would be untestable and would drift
/// between the emulator and the device (DESIGN.md §5.2). `progressPermille`
/// runs 0 (entirely `from`) to 1000 (entirely `to`) and is clamped.
///
/// `out` may alias neither `from` nor `to`.
void composite(Framebuffer& out,
               const Framebuffer& from,
               const Framebuffer& to,
               TransitionStyle style,
               TransitionDirection direction,
               int progressPermille) noexcept;

}  // namespace render
}  // namespace notrix
