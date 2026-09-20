// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace notrix {
namespace platform {

/// Physical controls on the TC002: a rotary encoder that also presses, plus two
/// buttons labelled − and + (blueprint §4, §15).
///
/// This enum describes hardware, not meaning — what a press *does* is decided by
/// InputMapper, so users can remap freely. The names are the labels on the case
/// rather than positions, because a control named `KeyLeft` invites bindings
/// that make no sense on a button marked −.
///
/// Earlier revisions of this file modelled three unlabelled buttons plus a knob,
/// which described hardware the TC002 does not appear to have. See
/// docs/adr/0016-tc002-input-layout.md — including what to check first if a real
/// device disagrees.
enum class RawInput {
    KeyMinus,
    KeyPlus,
    /// The middle button, between - and +.
    ///
    /// It was called KeyExtra while one source reported a third button and
    /// another did not. Settled on hardware 2026-09-20: pressing it emits
    /// KEY_LEFT (105) on /dev/input/event67, alongside - as KEY_DOWN (108),
    /// + as KEY_RIGHT (106) and the knob press as KEY_UP (103). The codes are
    /// arbitrary arrow keys the device tree happened to pick; only the mapping
    /// means anything.
    KeyMiddle,
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
    RawInput source = RawInput::KeyMinus;
    ButtonPhase phase = ButtonPhase::Tick;
    /// Monotonic milliseconds, from the same clock as ISystemClock. Press
    /// duration and rotary acceleration are both derived from this, so it must
    /// never go backwards.
    std::uint64_t timestampMillis = 0;
};

/// Somewhere raw input events can be delivered *to*.
///
/// The mirror of IInputDevice, and it exists because a press has more than one
/// legitimate origin. The web UI's on-screen buttons must reach exactly the
/// same code as the physical ones — the mapper, the long-press timing, the
/// splash dismissal — or the two would drift and the browser would be testing
/// something the device never does.
///
/// Deliberately not a method on IInputDevice: a device is a source, and giving
/// it a way to fabricate events would let anything holding one lie about the
/// hardware.
class IInputSink {
public:
    virtual ~IInputSink() = default;

    /// Handle an event as though it had come from the panel's own controls.
    virtual void inject(const InputEvent& event) = 0;
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
