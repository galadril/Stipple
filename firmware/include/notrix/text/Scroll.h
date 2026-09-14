// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "notrix/core/Geometry.h"
#include "notrix/text/Text.h"

namespace notrix {

class Canvas;

namespace text {

/// How text behaves when it does not fit its box (blueprint §10).
enum class ScrollMode : std::uint8_t {
    /// Clip. The caller has decided the text fits, or that truncation is fine.
    None,
    /// Scroll only when the text is wider than its box. The sensible default
    /// for anything driven by external data, where the length is not known in
    /// advance.
    Auto,
    /// Always scroll, even when the text would fit. Explicitly requested motion.
    Marquee,
    /// Slide to the end, hold, slide back, hold. Easier to read than a marquee
    /// for text only slightly too wide, because it never wraps mid-word.
    Bounce,
};

ScrollMode scrollModeFromName(std::string_view name) noexcept;
const char* scrollModeName(ScrollMode mode) noexcept;

struct ScrollConfig {
    /// Slow enough to read on a 52-pixel panel. Blueprint §39 targets 20-30 FPS,
    /// so this works out to well under one pixel per frame.
    int pixelsPerSecond = 12;

    /// Blank space between the end of the text and the start of its repeat, so
    /// a marquee does not read as one run-on string.
    int gapPixels = 8;

    /// Hold at the start before moving, so the beginning is readable.
    std::uint32_t startDelayMillis = 1200;

    /// Hold at each end of a bounce.
    std::uint32_t endHoldMillis = 900;
};

/// Does this combination actually move?
bool scrolls(ScrollMode mode, int textWidth, int boxWidth) noexcept;

/// Horizontal offset to apply to text drawn at the box's left edge.
///
/// Always <= 0: text only ever slides left from its origin. A pure function of
/// elapsed time, which is what makes scrolling testable without waiting and
/// identical between the emulator and the device.
int scrollOffset(ScrollMode mode,
                 int textWidth,
                 int boxWidth,
                 std::uint64_t elapsedMillis,
                 const ScrollConfig& config = ScrollConfig{}) noexcept;

/// Draw one line of scrolling text, clipped to `box`.
///
/// For marquee modes this draws the repeat as well, so the wrap is seamless
/// rather than the text vanishing and reappearing. When the text is not
/// scrolling, `style`'s horizontal alignment applies as usual; a scrolling line
/// always starts at the left edge, since alignment is meaningless mid-slide.
void drawScrolling(Canvas& canvas,
                   std::string_view utf8,
                   const Rect& box,
                   const TextStyle& style,
                   ScrollMode mode,
                   std::uint64_t elapsedMillis,
                   const ScrollConfig& config = ScrollConfig{});

}  // namespace text
}  // namespace notrix
