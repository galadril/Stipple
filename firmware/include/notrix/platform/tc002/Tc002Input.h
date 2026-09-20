// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/platform/Input.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// The TC002's controls, read from evdev.
///
/// Two nodes, both measured on hardware rather than inferred from the device
/// tree (see docs/research/tc002-platform-findings.md):
///
///   /dev/input/event67  soc:gpio_keys_1  EV_KEY - three buttons and the knob press
///   /dev/input/event68  knob_key         EV_ABS - rotation
///
/// The key codes are meaningless arrow keys the device tree happened to assign
/// to four GPIOs. Nothing here should ever read intent from the name `KEY_UP`;
/// only the mapping matters, and it is:
///
///   108 KEY_DOWN  -> KeyMinus        105 KEY_LEFT  -> KeyMiddle
///   106 KEY_RIGHT -> KeyPlus         103 KEY_UP    -> RotaryPress
class Tc002Input final : public IInputDevice {
public:
    /// Bounded, per blueprint §38. Sized for a burst of presses between polls;
    /// the application loop drains this every tick, so reaching the limit means
    /// something upstream has stalled rather than that a user typed quickly.
    static constexpr int kQueueCapacity = 32;

    Tc002Input() = default;
    ~Tc002Input() override;

    Tc002Input(const Tc002Input&) = delete;
    Tc002Input& operator=(const Tc002Input&) = delete;

    /// Opens both evdev nodes non-blocking. Returns false if either is
    /// unavailable — a clock that silently loses its buttons is worse than one
    /// that says so at startup.
    bool open(const char* keysPath = "/dev/input/event67",
              const char* knobPath = "/dev/input/event68");
    bool isOpen() const noexcept { return keysFd_ >= 0 && knobFd_ >= 0; }
    void close() noexcept;

    /// Drains both descriptors into the queue, then hands back the oldest
    /// event. Reading happens here rather than on a thread so input stays on
    /// the application loop, as IInputDevice requires.
    bool poll(InputEvent& event) override;

    std::uint32_t droppedEventCount() const override { return dropped_; }

private:
    void drain() noexcept;
    void drainKeys() noexcept;
    void drainKnob() noexcept;
    void push(RawInput source, ButtonPhase phase) noexcept;

    int keysFd_ = -1;
    int knobFd_ = -1;

    InputEvent queue_[kQueueCapacity] = {};
    int head_ = 0;
    int count_ = 0;
    std::uint32_t dropped_ = 0;
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
