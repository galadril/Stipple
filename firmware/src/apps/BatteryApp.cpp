// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/apps/BatteryApp.h"

#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/text/Text.h"

#include <cstdint>

namespace stipple {
namespace apps {
namespace {

// A cell wide enough to show charge at single-percent resolution would need a
// hundred columns; this panel has 52. Eleven inner columns means each lit
// column is worth about nine percent, which is why the number is drawn as well.
constexpr int kCellWidth = 15;
constexpr int kCellHeight = 8;
constexpr int kTerminalWidth = 2;
constexpr int kTerminalHeight = 4;

/// A lightning bolt, 3 wide and 6 tall, one bit per pixel from the top row down.
///
/// Drawn rather than written because "CHG" would not fit beside a cell and a
/// number on 52 columns, and because a bolt is read at a glance from across a
/// room, which is the distance this device is usually looked at from.
constexpr std::uint8_t kBolt[6] = {
    0b011,
    0b011,
    0b111,
    0b110,
    0b110,
    0b100,
};

/// Drawn over the fill, in a colour that reads against both the healthy and the
/// low fill, so the charge state is legible at 5% as well as at 95%.
void drawBolt(Canvas& canvas, int left, int top, Rgb color) {
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 3; ++column) {
            if ((kBolt[row] >> (2 - column)) & 1) {
                canvas.pixel(left + column, top + row, color);
            }
        }
    }
}

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

    // Over the fill, centred in the cell. A charging battery and a discharging
    // one at the same percentage are otherwise identical, which is what made a
    // gauge that sags under load look like a bug: the number moved and nothing
    // on screen explained why.
    if (status.chargingKnown && status.charging) {
        drawBolt(canvas, left + (kCellWidth - 3) / 2, top + 1, style.charging);
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
}  // namespace stipple
