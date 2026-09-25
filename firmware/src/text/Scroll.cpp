// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/text/Scroll.h"

#include "stipple/graphics/Canvas.h"

namespace stipple {
namespace text {
namespace {

/// Pixels travelled in `millis` at `pixelsPerSecond`, rounded down so motion
/// never overshoots its end point.
std::int64_t travelled(std::uint64_t millis, int pixelsPerSecond) noexcept {
    if (pixelsPerSecond <= 0) {
        return 0;
    }
    return static_cast<std::int64_t>(millis) * pixelsPerSecond / 1000;
}

std::uint64_t millisToTravel(int pixels, int pixelsPerSecond) noexcept {
    if (pixelsPerSecond <= 0 || pixels <= 0) {
        return 0;
    }
    return (static_cast<std::uint64_t>(pixels) * 1000u) /
           static_cast<std::uint64_t>(pixelsPerSecond);
}

int marqueeOffset(int textWidth,
                  std::uint64_t elapsedMillis,
                  const ScrollConfig& config) noexcept {
    const int period = textWidth + (config.gapPixels > 0 ? config.gapPixels : 1);
    if (period <= 0) {
        return 0;
    }
    if (elapsedMillis <= config.startDelayMillis) {
        return 0;
    }
    const std::int64_t distance =
        travelled(elapsedMillis - config.startDelayMillis, config.pixelsPerSecond);
    return -static_cast<int>(distance % period);
}

int bounceOffset(int textWidth,
                 int boxWidth,
                 std::uint64_t elapsedMillis,
                 const ScrollConfig& config) noexcept {
    const int travel = textWidth - boxWidth;
    if (travel <= 0) {
        return 0;
    }

    const std::uint64_t travelMillis = millisToTravel(travel, config.pixelsPerSecond);
    const std::uint64_t cycle =
        config.startDelayMillis + travelMillis + config.endHoldMillis + travelMillis +
        config.endHoldMillis;
    if (cycle == 0) {
        return 0;
    }

    std::uint64_t phase = elapsedMillis % cycle;

    if (phase < config.startDelayMillis) {
        return 0;  // holding at the start
    }
    phase -= config.startDelayMillis;

    if (phase < travelMillis) {
        const std::int64_t distance = travelled(phase, config.pixelsPerSecond);
        return -static_cast<int>(distance > travel ? travel : distance);
    }
    phase -= travelMillis;

    if (phase < config.endHoldMillis) {
        return -travel;  // holding at the end
    }
    phase -= config.endHoldMillis;

    if (phase < travelMillis) {
        const std::int64_t distance = travelled(phase, config.pixelsPerSecond);
        const std::int64_t remaining = travel - distance;
        return -static_cast<int>(remaining < 0 ? 0 : remaining);
    }

    return 0;  // holding at the start again
}

}  // namespace

ScrollMode scrollModeFromName(std::string_view name) noexcept {
    if (name == "auto") return ScrollMode::Auto;
    if (name == "marquee") return ScrollMode::Marquee;
    if (name == "bounce") return ScrollMode::Bounce;
    return ScrollMode::None;
}

const char* scrollModeName(ScrollMode mode) noexcept {
    switch (mode) {
        case ScrollMode::Auto: return "auto";
        case ScrollMode::Marquee: return "marquee";
        case ScrollMode::Bounce: return "bounce";
        case ScrollMode::None: break;
    }
    return "none";
}

bool scrolls(ScrollMode mode, int textWidth, int boxWidth) noexcept {
    switch (mode) {
        case ScrollMode::None:
            return false;
        case ScrollMode::Marquee:
            return textWidth > 0;
        case ScrollMode::Auto:
        case ScrollMode::Bounce:
            return textWidth > boxWidth;
    }
    return false;
}

int scrollOffset(ScrollMode mode,
                 int textWidth,
                 int boxWidth,
                 std::uint64_t elapsedMillis,
                 const ScrollConfig& config) noexcept {
    if (!scrolls(mode, textWidth, boxWidth)) {
        return 0;
    }
    // No speed means no motion. Without this, a bounce's travel legs collapse to
    // zero duration while its end-holds survive, parking the text at the far end
    // instead of leaving it still.
    if (config.pixelsPerSecond <= 0) {
        return 0;
    }
    if (mode == ScrollMode::Bounce) {
        return bounceOffset(textWidth, boxWidth, elapsedMillis, config);
    }
    return marqueeOffset(textWidth, elapsedMillis, config);
}

void drawScrolling(Canvas& canvas,
                   std::string_view utf8,
                   const Rect& box,
                   const TextStyle& style,
                   ScrollMode mode,
                   std::uint64_t elapsedMillis,
                   const ScrollConfig& config) {
    if (box.empty()) {
        return;
    }

    const BitmapFont& font = style.font != nullptr ? *style.font : font5x7();
    const int textWidth = measureLine(utf8, font, style.letterSpacing);

    if (!scrolls(mode, textWidth, box.w)) {
        // Static text keeps its alignment; this is also what happens once a
        // value shortens enough to fit again.
        draw(canvas, utf8, box, style);
        return;
    }

    int y = box.y;
    switch (style.vAlign) {
        case VAlign::Top:
            break;
        case VAlign::Middle:
            y = box.y + (box.h - font.height()) / 2;
            break;
        case VAlign::Bottom:
            y = box.y + box.h - font.height();
            break;
    }

    ClipScope scope(canvas, box);

    const int offset = scrollOffset(mode, textWidth, box.w, elapsedMillis, config);
    drawLine(canvas, utf8, box.x + offset, y, font, style.color, style.letterSpacing);

    if (mode != ScrollMode::Bounce) {
        // Draw the repeat so the wrap is seamless. Without this the text would
        // disappear off the left edge and only return once the cycle restarts.
        const int period = textWidth + (config.gapPixels > 0 ? config.gapPixels : 1);
        drawLine(canvas, utf8, box.x + offset + period, y, font, style.color,
                 style.letterSpacing);
    }
}

}  // namespace text
}  // namespace stipple
