// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/StopwatchApp.h"

#include <cstdio>

#include "notrix/graphics/Canvas.h"
#include "notrix/graphics/Framebuffer.h"
#include "notrix/text/Text.h"

namespace notrix {
namespace apps {
namespace {

constexpr std::uint64_t kMillisPerHour = 3600u * 1000u;

/// Rows 4-10: the time sits on the vertical centre of the panel rather than
/// at the top, because it is the only line here and a lone line pinned to the
/// top edge reads as though something failed to draw below it.
constexpr int kTimeY = 4;

/// The sweep. One row, at the bottom, out of the way of the digits.
constexpr int kSweepY = Framebuffer::kHeight - 1;

/// The same fake-bold as the splash: the glyphs twice, one column apart.
/// There is one font (DESIGN.md section 8) and a 2x scale would be 14 rows
/// tall, so weight is the only way to make this read as the headline it is.
void drawBoldCentred(Canvas& canvas, const std::string& text, int y, Rgb color) {
    const text::BitmapFont& font = text::font5x7();
    const int width = text::measureLine(text, font) + 1;
    const int x = (Framebuffer::kWidth - width) / 2;
    text::drawLine(canvas, text, x, y, font, color);
    text::drawLine(canvas, text, x + 1, y, font, color);
}

}  // namespace

Stopwatch::State Stopwatch::press(std::uint64_t nowMillis) noexcept {
    switch (state_) {
        case State::Ready:
            startedMillis_ = nowMillis;
            heldMillis_ = 0;
            state_ = State::Running;
            break;

        case State::Running:
            heldMillis_ = elapsedMillis(nowMillis);
            state_ = State::Stopped;
            break;

        case State::Stopped:
            reset();
            break;
    }
    return state_;
}

std::uint64_t Stopwatch::elapsedMillis(std::uint64_t nowMillis) const noexcept {
    if (state_ != State::Running) {
        return heldMillis_;
    }
    // Guarded rather than assumed. The monotonic clock should never go
    // backwards, but a stopwatch that underflowed to fifty days because it
    // did would be a memorable bug.
    if (nowMillis < startedMillis_) {
        return 0;
    }
    return nowMillis - startedMillis_;
}

void Stopwatch::reset() noexcept {
    state_ = State::Ready;
    startedMillis_ = 0;
    heldMillis_ = 0;
}

std::string formatElapsed(std::uint64_t millis) {
    char text[16] = {};

    if (millis >= kMillisPerHour) {
        const unsigned hours = static_cast<unsigned>(millis / kMillisPerHour);
        const unsigned minutes = static_cast<unsigned>((millis / 60000u) % 60u);
        const unsigned seconds = static_cast<unsigned>((millis / 1000u) % 60u);
        // No tenths past an hour: they stop meaning anything, and the extra
        // two characters do not fit across the panel.
        std::snprintf(text, sizeof(text), "%u:%02u:%02u", hours, minutes, seconds);
        return text;
    }

    const unsigned minutes = static_cast<unsigned>((millis / 60000u) % 60u);
    const unsigned seconds = static_cast<unsigned>((millis / 1000u) % 60u);
    const unsigned tenths = static_cast<unsigned>((millis / 100u) % 10u);
    std::snprintf(text, sizeof(text), "%02u:%02u.%u", minutes, seconds, tenths);
    return text;
}

void renderStopwatch(Canvas& canvas,
                     const Stopwatch& stopwatch,
                     std::uint64_t nowMillis,
                     const StopwatchStyle& style) {
    const std::uint64_t elapsed = stopwatch.elapsedMillis(nowMillis);

    // Three states, three colours. Somebody glancing at the panel from across
    // a room can tell a running stopwatch from a stopped one without reading
    // the digits, which is most of what this app is for.
    Rgb color = style.readyColor;
    if (stopwatch.state() == Stopwatch::State::Running) {
        color = style.runningColor;
    } else if (stopwatch.state() == Stopwatch::State::Stopped) {
        color = style.stoppedColor;
    }

    drawBoldCentred(canvas, formatElapsed(elapsed), kTimeY, color);

    if (!stopwatch.running()) {
        return;
    }

    // A bar that fills across the current second and starts again.
    //
    // It earns its place under DESIGN.md section 5.1 by carrying information
    // rather than decorating: the digits show tenths, which at a glance is a
    // blur, and this says at a glance that time is still passing. A frozen
    // stopwatch and a running one would otherwise look identical in a
    // photograph - or to anyone not staring at the last digit.
    const int width = static_cast<int>(
        (static_cast<std::uint64_t>(Framebuffer::kWidth) * (elapsed % 1000u)) / 1000u);
    if (width > 0) {
        canvas.hLine(0, kSweepY, width, style.sweepColor);
    }
}

}  // namespace apps
}  // namespace notrix
