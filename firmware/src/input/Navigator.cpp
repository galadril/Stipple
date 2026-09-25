// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/input/Navigator.h"

namespace stipple {
namespace input {
namespace {

constexpr int kSlotCount = static_cast<int>(SettingSlot::Count);

int indexOf(SettingSlot slot) noexcept { return static_cast<int>(slot); }

SettingSlot slotAt(int index) noexcept { return static_cast<SettingSlot>(index); }

}  // namespace

const char* settingLabel(SettingSlot slot) noexcept {
    switch (slot) {
        // Full words. They fit because the settings screen gives the label a
        // line of its own - an earlier layout shared one row with the value
        // and showed "BRIGH", and the wrong fix would have been to rename the
        // setting to suit the row.
        case SettingSlot::Brightness: return "BRIGHT";
        case SettingSlot::Overlay:    return "OVERLAY";
        case SettingSlot::Volume:     return "VOLUME";
        case SettingSlot::Count:      break;
    }
    return "";
}

void Navigator::setAvailable(SettingSlot slot, bool available) noexcept {
    const int index = indexOf(slot);
    if (index < 0 || index >= kSlotCount) {
        return;
    }
    available_[index] = available;

    // Hiding whatever the cursor is sitting on would otherwise leave it
    // pointing at something the user cannot see, and the next turn of the knob
    // would appear to skip a step.
    if (!available && current_ == slot) {
        settleOnAvailable(1);
    }
}

bool Navigator::available(SettingSlot slot) const noexcept {
    const int index = indexOf(slot);
    return index >= 0 && index < kSlotCount && available_[index];
}

void Navigator::settleOnAvailable(int direction) noexcept {
    const int step = direction >= 0 ? 1 : -1;
    int index = indexOf(current_);
    for (int tried = 0; tried < kSlotCount; ++tried) {
        index = (index + step + kSlotCount) % kSlotCount;
        if (available_[index]) {
            current_ = slotAt(index);
            return;
        }
    }
    // Every slot hidden. Leaving the cursor where it is costs nothing, because
    // a settings mode with nothing in it is not reachable in the first place.
}

void Navigator::toggleSettings(std::uint64_t nowMillis) noexcept {
    if (mode_ == Mode::Settings) {
        exitSettings();
        return;
    }

    // Entering always starts at the top rather than resuming where the last
    // visit left off. Resuming sounds helpful and is not: the panel shows one
    // setting at a time, so a device that opens on whatever was touched an hour
    // ago gives no clue that there is anything above it.
    mode_ = Mode::Settings;
    current_ = SettingSlot::Brightness;
    if (!available(current_)) {
        settleOnAvailable(1);
    }
    lastActivityMillis_ = nowMillis;
}

void Navigator::exitSettings() noexcept { mode_ = Mode::Browsing; }

void Navigator::moveCursor(int delta, std::uint64_t nowMillis) noexcept {
    if (mode_ != Mode::Settings || delta == 0) {
        return;
    }
    const int direction = delta > 0 ? 1 : -1;
    for (int i = 0; i < (delta > 0 ? delta : -delta); ++i) {
        settleOnAvailable(direction);
    }
    lastActivityMillis_ = nowMillis;
}

void Navigator::noteActivity(std::uint64_t nowMillis) noexcept {
    lastActivityMillis_ = nowMillis;
}

bool Navigator::tick(std::uint64_t nowMillis) noexcept {
    if (mode_ != Mode::Settings) {
        return false;
    }
    // A clock that steps backwards - NTP correcting, or a test rewinding - must
    // not be able to hold settings mode open forever, and must not slam it shut
    // either. Treating "now is before the last activity" as fresh activity does
    // neither.
    if (nowMillis < lastActivityMillis_) {
        lastActivityMillis_ = nowMillis;
        return false;
    }
    if (nowMillis - lastActivityMillis_ < kIdleExitMillis) {
        return false;
    }
    exitSettings();
    return true;
}

}  // namespace input
}  // namespace stipple
