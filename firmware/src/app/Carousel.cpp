// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/app/Carousel.h"

namespace notrix {
namespace app {

std::uint64_t Carousel::durationMillis(const App& app) const noexcept {
    int seconds = app.durationSeconds > 0 ? app.durationSeconds : config_.defaultDurationSeconds;
    if (seconds <= 0) {
        seconds = 1;  // a zero default would spin the carousel every frame
    }
    return static_cast<std::uint64_t>(seconds) * 1000u;
}

void Carousel::activate(int index, std::uint64_t nowMillis) {
    const App* app = registry_.at(index);
    if (app == nullptr) {
        activeId_.clear();
        lastIndex_ = -1;
        activeSinceMillis_ = nowMillis;
        return;
    }
    activeId_ = app->id;
    lastIndex_ = index;
    activeSinceMillis_ = nowMillis;
}

void Carousel::reset(std::uint64_t nowMillis) noexcept {
    activeId_.clear();
    lastIndex_ = -1;
    activeSinceMillis_ = nowMillis;
    paused_ = false;
    pinnedId_.clear();
}

const App* Carousel::active() const noexcept {
    if (activeId_.empty()) {
        return nullptr;
    }
    return registry_.find(activeId_);
}

std::uint64_t Carousel::dwellMillis(std::uint64_t nowMillis) const noexcept {
    if (nowMillis < activeSinceMillis_) {
        return 0;  // clock stepped backwards
    }
    return nowMillis - activeSinceMillis_;
}

int Carousel::activeDurationSeconds() const noexcept {
    const App* app = active();
    if (app == nullptr) {
        return 0;
    }
    return static_cast<int>(durationMillis(*app) / 1000u);
}

bool Carousel::pin(std::string_view id, std::uint64_t nowMillis) {
    const App* app = registry_.find(id);
    if (app == nullptr || !app->enabled) {
        return false;
    }
    pinnedId_ = std::string(id);
    activate(registry_.indexOf(id), nowMillis);
    return true;
}

bool Carousel::tick(std::uint64_t nowMillis) {
    const std::string previousActive = activeId_;

    registry_.expire(nowMillis);

    if (!pinnedId_.empty()) {
        const App* pinnedApp = registry_.find(pinnedId_);
        if (pinnedApp != nullptr && pinnedApp->enabled) {
            if (activeId_ != pinnedId_) {
                activate(registry_.indexOf(pinnedId_), nowMillis);
            }
            return activeId_ != previousActive;
        }
        // The pinned app was deleted or disabled. Drop the pin and rotate again
        // rather than freezing on a screen that no longer exists.
        pinnedId_.clear();
    }

    const int currentIndex = activeId_.empty() ? -1 : registry_.indexOf(activeId_);
    const App* current = currentIndex >= 0 ? registry_.at(currentIndex) : nullptr;

    if (current == nullptr || !current->enabled) {
        // Resume from where the vanished app used to sit, so deleting an app
        // advances to its neighbour instead of restarting the rotation.
        const int resumeFrom = lastIndex_ >= 0 ? lastIndex_ - 1 : -1;
        activate(registry_.nextEnabled(resumeFrom), nowMillis);
        return activeId_ != previousActive;
    }

    if (paused_) {
        return false;
    }

    if (dwellMillis(nowMillis) >= durationMillis(*current)) {
        activate(registry_.nextEnabled(currentIndex), nowMillis);
    }

    return activeId_ != previousActive;
}

bool Carousel::next(std::uint64_t nowMillis) {
    pinnedId_.clear();

    const std::string previousActive = activeId_;
    const int from = activeId_.empty() ? -1 : registry_.indexOf(activeId_);
    activate(registry_.nextEnabled(from >= 0 ? from : -1), nowMillis);
    return activeId_ != previousActive;
}

bool Carousel::previous(std::uint64_t nowMillis) {
    pinnedId_.clear();

    const std::string previousActive = activeId_;
    const int from = activeId_.empty() ? -1 : registry_.indexOf(activeId_);
    activate(registry_.previousEnabled(from >= 0 ? from : 0), nowMillis);
    return activeId_ != previousActive;
}

}  // namespace app
}  // namespace notrix
