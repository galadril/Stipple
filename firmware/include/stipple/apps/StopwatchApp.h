// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "stipple/core/Rgb.h"

namespace stipple {

class Canvas;

namespace apps {

/// A stopwatch, driven by one button.
///
/// **Three states and one control**, because this device has one control that
/// an app may use: a short press on the knob. Anything else is already spoken
/// for - a long press opens settings, the side buttons adjust brightness, and
/// the rotary itself moves the carousel.
///
///     Ready    -> press -> Running    starts from zero
///     Running  -> press -> Stopped    holds the time on screen
///     Stopped  -> press -> Ready      back to zero
///
/// Start, stop, reset, in the order somebody actually wants them, with no
/// hidden gesture and nothing to discover.
///
/// Pure, and fed timestamps rather than reading a clock, so a test can run an
/// hour through it without waiting. It also means the elapsed time is derived
/// from the monotonic clock rather than accumulated per tick: a stopwatch that
/// added up frame intervals would drift, and would lose time entirely while
/// the panel was asleep.
class Stopwatch {
public:
    enum class State : std::uint8_t {
        Ready,
        Running,
        Stopped,
    };

    /// The one control. Returns the state it moved to.
    State press(std::uint64_t nowMillis) noexcept;

    /// Milliseconds on the clock face. Keeps counting while Running, holds
    /// still while Stopped, zero while Ready.
    std::uint64_t elapsedMillis(std::uint64_t nowMillis) const noexcept;

    State state() const noexcept { return state_; }
    bool running() const noexcept { return state_ == State::Running; }

    /// Back to zero without a press, for a caller that needs to.
    void reset() noexcept;

private:
    State state_ = State::Ready;

    /// When the current run began. Only meaningful while Running.
    std::uint64_t startedMillis_ = 0;

    /// What was on the clock when it last stopped.
    std::uint64_t heldMillis_ = 0;
};

/// `MM:SS.d`, or `H:MM:SS` once an hour has passed.
///
/// The switch is deliberate rather than a formatting accident. Tenths are the
/// point of a stopwatch for anything short, and meaningless after an hour -
/// and `1:02:03.4` is nine characters, which does not fit across 52 pixels in
/// the only font this device has.
std::string formatElapsed(std::uint64_t millis);

struct StopwatchStyle {
    /// Counting. The brightest thing on the panel, because it is the only
    /// thing on the panel.
    Rgb runningColor = rgb(0, 230, 140);

    /// Stopped with a time on it - a result worth reading, so it is warm
    /// rather than dim.
    Rgb stoppedColor = rgb(255, 176, 0);

    /// Ready at zero. Dim, because nothing has happened yet and it should not
    /// compete with the clock in the rotation beside it.
    Rgb readyColor = rgb(110, 110, 110);

    /// The sweep along the bottom row while running.
    Rgb sweepColor = rgb(0, 110, 80);
};

/// Draw the stopwatch.
void renderStopwatch(Canvas& canvas,
                     const Stopwatch& stopwatch,
                     std::uint64_t nowMillis,
                     const StopwatchStyle& style = StopwatchStyle{});

}  // namespace apps
}  // namespace stipple
