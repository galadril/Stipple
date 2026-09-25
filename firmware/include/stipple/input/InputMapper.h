// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "stipple/platform/Input.h"

namespace stipple {
namespace input {

/// What the user meant, as opposed to which switch closed (blueprint §15).
///
/// Two kinds live here, and the difference is ADR 0017's whole point.
///
/// **Context-free** actions name a specific effect: BrightnessUp always changes
/// brightness, wherever it comes from. These are what the API and MQTT send,
/// because a caller that is not standing in front of the device has no context
/// to be relative to.
///
/// **Relative** actions — Adjust, Back, SettingsToggle — name a gesture and let
/// the Navigator decide what it applies to. They exist because a panel with
/// five controls cannot afford a separate button per setting, so the buttons
/// have to mean the same thing everywhere and point at different things.
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

    /// Change whatever is currently selected. Brightness while browsing, the
    /// chosen setting inside settings.
    AdjustUp,
    AdjustDown,

    /// Leave, cancel, dismiss. The only control that always goes backwards.
    Back,

    /// Enter settings, or leave them.
    SettingsToggle,
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

    /// One meaning per control, in every mode (ADR 0017): the knob moves
    /// between things and acts on them, − / + adjust the thing, and the middle
    /// button goes back.
    ///
    /// These replaced a set where − and + tapped volume on hardware that
    /// reported no audio output, so the two most obviously pressable buttons
    /// on the device did nothing at all. Volume is back on them now that there
    /// is a speaker behind it - see Tc002Audio - and it is the right thing
    /// there: on a device that makes noise, volume is what people reach for,
    /// and brightness is set once and left.
    ///
    /// Hold adjusts brightness instead. That is the one place a long press
    /// means something other than its short press, and it earns the exception:
    /// both are "turn this up", the direction is the same, and a slip of the
    /// thumb changes the other quantity by one step rather than doing something
    /// unrelated.
    ///
    /// A device with no speaker never reaches the volume branch - the host
    /// adjusts brightness instead, so the buttons stay useful rather than
    /// going dead again.
    ///
    /// Nothing here is binding: §15 puts mappings in configuration, and these
    /// are only what an unconfigured device does.
    ButtonBinding keyMinus{Action::AdjustDown, Action::BrightnessDown};
    /// The middle button. Back, and only back — it used to duplicate the knob's
    /// AppNext, which wasted the one control free to mean something else.
    ButtonBinding keyExtra{Action::Back, Action::Back};
    ButtonBinding keyPlus{Action::AdjustUp, Action::BrightnessUp};
    /// Press acts on what is selected; hold is the way in and out of settings.
    ButtonBinding rotaryPress{Action::AppAction, Action::SettingsToggle};
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
}  // namespace stipple
