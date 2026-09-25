// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "stipple/app/AppRegistry.h"

namespace stipple {
namespace app {

struct CarouselConfig {
    /// Used for apps that do not specify their own duration.
    int defaultDurationSeconds = 8;
};

/// App rotation (blueprint §14).
///
/// The active app is tracked by **id**, not by index. The registry can be
/// mutated from the API, MQTT or an integration between any two ticks, and an
/// index would silently start pointing at a different app when something ahead
/// of it is deleted. Tracking the id means a removed app is detected rather than
/// impersonated.
///
/// Time is passed in rather than read from a clock, so every rotation rule is
/// testable exactly and instantly.
class Carousel {
public:
    explicit Carousel(AppRegistry& registry, const CarouselConfig& config = CarouselConfig{})
        : registry_(registry), config_(config) {}

    Carousel(const Carousel&) = delete;
    Carousel& operator=(const Carousel&) = delete;

    /// Advance the clock. Returns true when the active app changed.
    ///
    /// Also expires Temporary apps, so a notification-style app disappears on
    /// schedule without anything else having to poll for it.
    bool tick(std::uint64_t nowMillis);

    /// Manual rotation. Works while paused — an explicit button press is user
    /// intent and should not be swallowed — and clears any pin for the same
    /// reason.
    bool next(std::uint64_t nowMillis);
    bool previous(std::uint64_t nowMillis);

    /// Adopt new timing, e.g. once settings have been loaded at boot.
    void setConfig(const CarouselConfig& config) noexcept { config_ = config; }
    const CarouselConfig& config() const noexcept { return config_; }

    void setPaused(bool paused) noexcept { paused_ = paused; }
    bool paused() const noexcept { return paused_; }

    /// Forget the active app, its dwell timer, any pin, and the paused state.
    /// The next tick re-selects from the beginning.
    ///
    /// Needed because the dwell timer is an absolute timestamp: re-initialising
    /// without clearing it leaves the carousel comparing new times against an
    /// old start point, and it can sit frozen until the clock catches up.
    void reset(std::uint64_t nowMillis) noexcept;

    /// Jump straight to an app. Rotation continues from there — use pin() to
    /// hold it. Fails if the id is unknown or the app is disabled.
    bool activate(std::string_view id, std::uint64_t nowMillis);

    /// Hold one app on screen. Fails if the id is unknown or disabled.
    bool pin(std::string_view id, std::uint64_t nowMillis);
    void unpin() noexcept { pinnedId_.clear(); }
    bool isPinned() const noexcept { return !pinnedId_.empty(); }
    std::string_view pinnedId() const noexcept { return pinnedId_; }

    const App* active() const noexcept;
    std::string_view activeId() const noexcept { return activeId_; }

    /// How long the active app has been showing.
    std::uint64_t dwellMillis(std::uint64_t nowMillis) const noexcept;

    /// Treat the current app as having just become active, without changing
    /// which one it is.
    ///
    /// For time the user spent somewhere else entirely - in settings, say.
    /// Simply not calling tick() is not enough: the dwell is measured from a
    /// timestamp, so a minute spent in a menu is a minute the app is deemed to
    /// have been on screen, and leaving the menu advances the carousel
    /// instantly. Holding this still means someone comes back to the app they
    /// left, with its full turn ahead of it.
    void restartDwell(std::uint64_t nowMillis) noexcept { activeSinceMillis_ = nowMillis; }

    /// Effective duration of the active app, defaults applied. Zero when
    /// nothing is active.
    int activeDurationSeconds() const noexcept;

private:
    void activateIndex(int index, std::uint64_t nowMillis);
    std::uint64_t durationMillis(const App& app) const noexcept;

    AppRegistry& registry_;
    CarouselConfig config_;

    std::string activeId_;
    /// Where the active app was last seen. If it is deleted, rotation resumes
    /// from that position instead of jumping back to the start.
    int lastIndex_ = -1;
    std::uint64_t activeSinceMillis_ = 0;
    bool paused_ = false;
    std::string pinnedId_;
};

}  // namespace app
}  // namespace stipple
