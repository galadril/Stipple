// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace stipple {
namespace input {

/// What the controls are currently pointed at (ADR 0017).
///
/// The mode changes what a control applies *to*, never what it means. Turning
/// the knob moves between things in both modes; it is only the things that
/// differ.
enum class Mode : std::uint8_t {
    /// The carousel. Where the device sits almost always.
    Browsing,
    /// One setting on screen at a time.
    Settings,
};

/// The settings reachable from the device itself.
///
/// Deliberately few. This is a 52x16 panel with five controls, not a
/// configuration page — anything that needs typing, choosing from a long list
/// or seeing more than one value at once belongs in the web UI. What is here is
/// what someone standing in front of the clock wants to change without finding
/// a browser first.
///
/// The order is the order they appear. It is explicit for the same reason app
/// ordering is: derived order is order nobody chose.
/// Note there is deliberately no panel on/off here.
///
/// It exists in the settings model and over the API, where it makes sense: a
/// browser or an automation can blank the panel and can plainly still be used
/// afterwards. On the panel itself it is circular - the control lives on the
/// only surface it switches off, so using it hides the way back. Turning
/// brightness up already restores a blank panel, which is the gesture someone
/// reaches for anyway.
enum class SettingSlot : std::uint8_t {
    Brightness,
    Overlay,
    Volume,
    Count,
};

/// Short enough to fit beside a value on 52 columns.
const char* settingLabel(SettingSlot slot) noexcept;

/// Owns the mode, the settings cursor, and the rule that the device can never
/// be stranded in a mode.
///
/// Pure: it is fed timestamps rather than reading a clock, and it holds no
/// reference to settings or to a platform. It decides *what is selected*; the
/// host decides what changing it does. That split is what keeps the mode logic
/// testable without standing up a whole device.
class Navigator {
public:
    /// How long settings mode survives with no input before returning to the
    /// carousel.
    ///
    /// The device has no way to say "you are in a menu" other than the menu
    /// itself, so someone who walks away mid-adjustment would otherwise leave a
    /// clock showing "BRIGHT 168" until the next person touched it. Ten seconds
    /// is long enough to think and short enough that the failure is invisible.
    static constexpr std::uint64_t kIdleExitMillis = 10'000;

    Mode mode() const noexcept { return mode_; }
    bool inSettings() const noexcept { return mode_ == Mode::Settings; }

    /// The slot the cursor is on. Only meaningful in Settings mode.
    SettingSlot current() const noexcept { return current_; }

    /// Hide a setting the hardware cannot honour.
    ///
    /// Volume on a device with no speaker is the case this exists for: ADR 0013
    /// says absence should be visible, and the honest way to show it on a panel
    /// this size is not to offer the control at all rather than to offer one
    /// that does nothing.
    void setAvailable(SettingSlot slot, bool available) noexcept;
    bool available(SettingSlot slot) const noexcept;

    /// Enter settings, or leave if already there. Bound to the knob's long
    /// press, which is the one gesture no other mode needs.
    void toggleSettings(std::uint64_t nowMillis) noexcept;

    /// Leave settings. Idempotent, so "back" can be handled the same way
    /// wherever it arrives from.
    void exitSettings() noexcept;

    /// Move the cursor by `delta` slots, skipping unavailable ones and wrapping
    /// at both ends. Does nothing outside Settings mode.
    void moveCursor(int delta, std::uint64_t nowMillis) noexcept;

    /// Record that the user did something, restarting the idle timer.
    void noteActivity(std::uint64_t nowMillis) noexcept;

    /// Call once per frame. Returns true on the tick that idle actually expired,
    /// so the host can redraw exactly once rather than every frame afterwards.
    bool tick(std::uint64_t nowMillis) noexcept;

private:
    /// Step to the next available slot in `delta`'s direction, or leave the
    /// cursor alone if nothing is available.
    void settleOnAvailable(int direction) noexcept;

    Mode mode_ = Mode::Browsing;
    SettingSlot current_ = SettingSlot::Brightness;
    std::uint64_t lastActivityMillis_ = 0;

    bool available_[static_cast<int>(SettingSlot::Count)] = {true, true, true};
};

}  // namespace input
}  // namespace stipple
