// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/platform/Input.h"

namespace notrix {
namespace input {

/// What the user meant, as opposed to which switch closed (blueprint §15).
enum class Action {
    None,
    AppPrevious,
    AppNext,
    AppAction,
    NotificationDismiss,
    BrightnessUp,
    BrightnessDown,
    VolumeUp,
    VolumeDown,
};

/// Stable name for an action, for logs and MQTT button events. Stable is the
/// point: these appear in topics and payloads that automations match on, so they
/// are API surface and must not be renamed casually.
const char* actionName(Action action) noexcept;

struct ActionEvent {
    Action action = Action::None;

    /// How many times to apply the action at once. Always 1 for buttons; for
    /// the rotary encoder this is the acceleration factor, so a fast spin moves
    /// several apps per detent instead of feeling sluggish.
    int repeat = 1;

    bool longPress = false;
};

/// Short and long press bindings for one physical control.
struct ButtonBinding {
    Action shortPress = Action::None;
    Action longPress = Action::None;
};

/// Everything user-configurable about input handling. Lives in configuration
/// (§15: "mappings belong in configuration"), so the defaults here are just
/// defaults, not assumptions baked into the code.
struct InputMapperConfig {
    /// A press held at least this long is a long press. Measured on release.
    std::uint32_t longPressMillis = 500;

    /// Consecutive rotary detents closer together than this accelerate.
    std::uint32_t rotaryAccelerationWindowMillis = 120;

    /// Ceiling on the acceleration factor, so a fast spin cannot skip an
    /// unbounded number of apps.
    int maxRotaryRepeat = 5;

    /// How far one press moves each quantity. Volume is a percentage; brightness
    /// is the panel's own 0-255, so the steps are not the same size by accident.
    int volumeStepPercent = 5;
    int brightnessStep = 16;

    /// Defaults follow the case: navigation on the knob, and the − / + buttons
    /// adjusting something. Tap changes volume, hold changes brightness — the
    /// two most-wanted adjustments on the only two labelled controls, with the
    /// commoner one on the shorter gesture.
    ///
    /// Nothing here is binding: §15 puts mappings in configuration, and these
    /// are only what an unconfigured device does.
    ButtonBinding keyMinus{Action::VolumeDown, Action::BrightnessDown};
    /// Useful if the third button is there, harmless if it is not.
    ButtonBinding keyExtra{Action::AppNext, Action::NotificationDismiss};
    ButtonBinding keyPlus{Action::VolumeUp, Action::BrightnessUp};
    ButtonBinding rotaryPress{Action::AppAction, Action::NotificationDismiss};
};

/// Turns raw hardware events into logical actions.
///
/// Pure logic with no platform dependency: it is fed timestamps rather than
/// reading a clock, which is what makes press durations and rotary acceleration
/// testable without waiting in real time.
///
/// Double-press is deliberately not implemented. Blueprint §15 admits it "only
/// if reliable", and detecting it means delaying every single press long enough
/// to see whether a second one arrives — which makes the common case feel
/// broken. Not worth it until there is a use that justifies the latency.
class InputMapper {
public:
    explicit InputMapper(const InputMapperConfig& config = InputMapperConfig{}) noexcept;

    /// Feed one raw event. Returns true and fills `out` when the event resolves
    /// to an action; returns false for events that do not (a key going down, or
    /// a press bound to Action::None).
    bool handle(const platform::InputEvent& event, ActionEvent& out) noexcept;

    /// Forget in-flight press state. Call when input focus changes, so a key
    /// held across the transition cannot emit a stale action on release.
    void reset() noexcept;

    const InputMapperConfig& config() const noexcept { return config_; }

private:
    static constexpr int kButtonCount = 4;  // minus, plus, extra, rotary press

    static int buttonIndex(platform::RawInput source) noexcept;
    const ButtonBinding& bindingFor(platform::RawInput source) const noexcept;

    InputMapperConfig config_;

    /// Press-start timestamps, or kNoPress when the button is up.
    static constexpr std::uint64_t kNoPress = ~std::uint64_t{0};
    std::uint64_t pressStart_[kButtonCount];

    std::uint64_t lastRotaryMillis_ = 0;
    bool hasRotaryHistory_ = false;
    bool lastRotaryWasRight_ = false;
    int rotaryRepeat_ = 1;
};

}  // namespace input
}  // namespace notrix
