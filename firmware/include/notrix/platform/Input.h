// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace notrix {
namespace platform {

/// Physical controls on the TC002: three buttons plus a rotary encoder
/// (blueprint §4, §15). This enum describes hardware, not meaning — what a
/// press *does* is decided by InputMapper, so users can remap freely.
enum class RawInput {
    KeyLeft,
    KeyMiddle,
    KeyRight,
    RotaryPress,
    RotaryLeft,   ///< one detent counter-clockwise
    RotaryRight,  ///< one detent clockwise
};

enum class ButtonPhase {
    Down,
    Up,
    /// Rotary detents are momentary: they have no press duration, so they
    /// arrive as a single Tick rather than a Down/Up pair.
    Tick,
};

struct InputEvent {
    RawInput source = RawInput::KeyLeft;
    ButtonPhase phase = ButtonPhase::Tick;
    /// Monotonic milliseconds, from the same clock as ISystemClock. Press
    /// duration and rotary acceleration are both derived from this, so it must
    /// never go backwards.
    std::uint64_t timestampMillis = 0;
};

/// Source of raw input events.
///
/// Polled rather than callback-driven: the application loop decides when input
/// is handled, which keeps input off whatever thread or interrupt the platform
/// happens to deliver it on, and keeps the render path free of surprises.
class IInputDevice {
public:
    virtual ~IInputDevice() = default;

    /// Take the oldest pending event. Returns false when none is waiting.
    ///
    /// Implementations must use a bounded queue and drop the *oldest* event on
    /// overflow (blueprint §38 forbids unbounded queues). Dropping the newest
    /// would make a stuck button hide every later press.
    virtual bool poll(InputEvent& event) = 0;

    /// Events discarded due to queue overflow since boot. Surfaced in
    /// diagnostics: a non-zero value means input is being lost.
    virtual std::uint32_t droppedEventCount() const = 0;
};

}  // namespace platform
}  // namespace notrix
