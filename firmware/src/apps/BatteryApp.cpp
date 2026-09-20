// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/BatteryApp.h"

#include "notrix/graphics/Canvas.h"
#include "notrix/graphics/Framebuffer.h"
#include "notrix/text/Text.h"

namespace notrix {
namespace apps {
namespace {

// A cell wide enough to show charge at single-percent resolution would need a
// hundred columns; this panel has 52. Eleven inner columns means each lit
// column is worth about nine percent, which is why the number is drawn as well.
constexpr int kCellWidth = 15;
constexpr int kCellHeight = 8;
constexpr int kTerminalWidth = 2;
constexpr int kTerminalHeight = 4;

}  // namespace

void renderBattery(Canvas& canvas,
                   const platform::BatteryStatus& status,
                   const BatteryStyle& style) {
    const int top = (Framebuffer::kHeight - kCellHeight) / 2;

    if (!status.known) {
        // Honest about not knowing, matching how the clock handles an unset
        // wall clock. "0%" would be a confident lie.
        text::TextStyle unknown;
        unknown.color = style.color;
        unknown.hAlign = text::HAlign::Center;
        unknown.vAlign = text::VAlign::Middle;
        text::draw(canvas, "NO BATT", Framebuffer::bounds(), unknown);
        return;
    }

    const int percent = status.percent < 0 ? 0 : (status.percent > 100 ? 100 : status.percent);

    const int totalWidth = kCellWidth + kTerminalWidth;
    const int left = style.showPercent ? 1 : (Framebuffer::kWidth - totalWidth) / 2;

    // Outline.
    canvas.fillRect(Rect{left, top, kCellWidth, 1}, style.color);
    canvas.fillRect(Rect{left, top + kCellHeight - 1, kCellWidth, 1}, style.color);
    canvas.fillRect(Rect{left, top, 1, kCellHeight}, style.color);
    canvas.fillRect(Rect{left + kCellWidth - 1, top, 1, kCellHeight}, style.color);

    // Terminal nub on the right, vertically centred.
    canvas.fillRect(Rect{left + kCellWidth, top + (kCellHeight - kTerminalHeight) / 2,
                         kTerminalWidth, kTerminalHeight},
                    style.color);

    // Fill. Rounded up so any charge at all lights at least one column - a
    // battery at 3% should not look identical to a flat one.
    const int inner = kCellWidth - 4;
    int filled = (percent * inner + 99) / 100;
    if (filled > inner) {
        filled = inner;
    }
    if (filled > 0) {
        canvas.fillRect(Rect{left + 2, top + 2, filled, kCellHeight - 4},
                        percent <= style.lowPercent ? style.low : style.healthy);
    }

    if (!style.showPercent) {
        return;
    }

    char label[5];
    int at = 0;
    if (percent >= 100) {
        label[at++] = '1';
        label[at++] = '0';
        label[at++] = '0';
    } else if (percent >= 10) {
        label[at++] = static_cast<char>('0' + percent / 10);
        label[at++] = static_cast<char>('0' + percent % 10);
    } else {
        label[at++] = static_cast<char>('0' + percent);
    }
    label[at++] = '%';
    label[at] = '\0';

    const int textLeft = left + totalWidth + 2;
    text::TextStyle numbers;
    numbers.color = style.color;
    numbers.hAlign = text::HAlign::Left;
    numbers.vAlign = text::VAlign::Middle;
    text::draw(canvas, label,
               Rect{textLeft, 0, Framebuffer::kWidth - textLeft, Framebuffer::kHeight},
               numbers);
}

}  // namespace apps
}  // namespace notrix
