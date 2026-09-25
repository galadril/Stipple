// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/demo/TestPattern.h"

#include "stipple/graphics/Canvas.h"

namespace stipple {
namespace demo {
namespace {

/// Positive modulo — frame counters must not misbehave if a caller ever passes
/// a negative or wrapped value.
int wrap(int value, int period) noexcept {
    if (period <= 0) {
        return 0;
    }
    const int remainder = value % period;
    return remainder < 0 ? remainder + period : remainder;
}

}  // namespace

void drawTestPattern(Canvas& canvas, int frame) {
    const Rect panel = Framebuffer::bounds();

    canvas.clear(colors::kBlack);

    // Border — proves the outline primitive and the panel extents.
    canvas.rect(panel, rgb(0, 0, 70));

    const Rect inner{panel.x + 1, panel.y + 1, panel.w - 2, panel.h - 2};
    if (inner.empty()) {
        return;
    }

    // Diagonals, drawn deliberately oversized so the ClipScope has to cut them
    // back. If clipping regresses, these bleed over the border immediately.
    {
        ClipScope scope(canvas, inner);
        const Rgb diagonal = rgb(45, 45, 45);
        canvas.line(panel.x - 4, panel.y - 4, panel.right() + 4, panel.bottom() + 4, diagonal);
        canvas.line(panel.right() + 4, panel.y - 4, panel.x - 4, panel.bottom() + 4, diagonal);
    }

    // Horizontal gradient — a per-column sweep that makes channel-order mistakes
    // (RGB vs BGR on the panel) obvious at a glance.
    const Rect gradient{panel.x + 2, panel.y + 6, panel.w - 4, 4};
    if (!gradient.empty() && gradient.w > 1) {
        ClipScope scope(canvas, gradient);
        for (int x = gradient.x; x < gradient.right(); ++x) {
            const int t = ((x - gradient.x) * 255) / (gradient.w - 1);
            canvas.vLine(x, gradient.y, gradient.h, rgb(t, 255 - t, 96));
        }
    }

    // Animated scan bar, ping-ponging across the inner area. Any stutter or
    // tearing here points at frame pacing rather than at the renderer.
    if (inner.w > 0) {
        const int period = inner.w * 2 - 2;
        int phase = wrap(frame, period > 0 ? period : 1);
        if (phase >= inner.w) {
            phase = period - phase;
        }
        canvas.vLine(inner.x + phase, inner.y, inner.h, rgb(255, 255, 255));
    }

    // Corner markers last so they are never overdrawn: red, green, blue, yellow
    // clockwise from top-left. These confirm orientation and that the panel is
    // not mirrored or rotated.
    canvas.pixel(panel.x, panel.y, colors::kRed);
    canvas.pixel(panel.right() - 1, panel.y, colors::kGreen);
    canvas.pixel(panel.right() - 1, panel.bottom() - 1, colors::kYellow);
    canvas.pixel(panel.x, panel.bottom() - 1, colors::kBlue);
}

}  // namespace demo
}  // namespace stipple
