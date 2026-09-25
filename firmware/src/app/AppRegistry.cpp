// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/app/AppRegistry.h"

namespace stipple {
namespace app {

const char* appSourceName(AppSource source) noexcept {
    switch (source) {
        case AppSource::System: return "system";
        case AppSource::Local: return "local";
        case AppSource::Remote: return "remote";
        case AppSource::Integration: return "integration";
        case AppSource::Temporary: return "temporary";
    }
    return "unknown";
}

AppRegistry::PutResult AppRegistry::put(App app) {
    if (app.id.empty() || app.id.size() > kMaxIdBytes) {
        return PutResult::InvalidId;
    }
    if (app.sceneJson.size() > kMaxSceneBytes) {
        return PutResult::SceneTooLarge;
    }

    const int existing = indexOf(app.id);
    if (existing >= 0) {
        // Replace in place. An integration refreshing its app must not move it.
        apps_[static_cast<std::size_t>(existing)] = std::move(app);
        ++revision_;
        return PutResult::Replaced;
    }

    if (count() >= kMaxApps) {
        return PutResult::Full;
    }

    apps_.push_back(std::move(app));
    ++revision_;
    return PutResult::Added;
}

bool AppRegistry::remove(std::string_view id) {
    const int index = indexOf(id);
    if (index < 0) {
        return false;
    }
    apps_.erase(apps_.begin() + index);
    ++revision_;
    return true;
}

void AppRegistry::clear() {
    std::vector<App> kept;
    for (App& app : apps_) {
        if (app.source == AppSource::System) {
            kept.push_back(std::move(app));
        }
    }
    apps_ = std::move(kept);
    ++revision_;
}

const App* AppRegistry::at(int index) const noexcept {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return &apps_[static_cast<std::size_t>(index)];
}

App* AppRegistry::at(int index) noexcept {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return &apps_[static_cast<std::size_t>(index)];
}

int AppRegistry::indexOf(std::string_view id) const noexcept {
    for (int i = 0; i < count(); ++i) {
        if (apps_[static_cast<std::size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

const App* AppRegistry::find(std::string_view id) const noexcept {
    return at(indexOf(id));
}

App* AppRegistry::find(std::string_view id) noexcept {
    return at(indexOf(id));
}

bool AppRegistry::move(std::string_view id, int newIndex) {
    const int current = indexOf(id);
    if (current < 0 || newIndex < 0 || newIndex >= count()) {
        return false;
    }
    if (current == newIndex) {
        return true;
    }

    App moved = std::move(apps_[static_cast<std::size_t>(current)]);
    apps_.erase(apps_.begin() + current);
    apps_.insert(apps_.begin() + newIndex, std::move(moved));
    ++revision_;
    return true;
}

bool AppRegistry::setEnabled(std::string_view id, bool enabled) {
    App* app = find(id);
    if (app == nullptr) {
        return false;
    }
    if (app->enabled != enabled) {
        app->enabled = enabled;
        ++revision_;
    }
    return true;
}

int AppRegistry::enabledCount() const noexcept {
    int total = 0;
    for (const App& app : apps_) {
        if (app.enabled) {
            ++total;
        }
    }
    return total;
}

int AppRegistry::firstEnabled() const noexcept {
    for (int i = 0; i < count(); ++i) {
        if (apps_[static_cast<std::size_t>(i)].enabled) {
            return i;
        }
    }
    return -1;
}

int AppRegistry::nextEnabled(int fromIndex) const noexcept {
    const int total = count();
    if (total == 0) {
        return -1;
    }
    // Walk the whole ring once so a single enabled app returns itself and a
    // fully disabled registry returns -1 rather than looping forever.
    for (int step = 1; step <= total; ++step) {
        const int candidate = ((fromIndex + step) % total + total) % total;
        if (apps_[static_cast<std::size_t>(candidate)].enabled) {
            return candidate;
        }
    }
    return -1;
}

int AppRegistry::previousEnabled(int fromIndex) const noexcept {
    const int total = count();
    if (total == 0) {
        return -1;
    }
    for (int step = 1; step <= total; ++step) {
        const int candidate = ((fromIndex - step) % total + total) % total;
        if (apps_[static_cast<std::size_t>(candidate)].enabled) {
            return candidate;
        }
    }
    return -1;
}

int AppRegistry::expire(std::uint64_t nowMillis) {
    int removed = 0;
    for (std::size_t i = apps_.size(); i > 0; --i) {
        const App& app = apps_[i - 1];
        if (app.source == AppSource::Temporary && app.expires() &&
            nowMillis >= app.expiresAtMillis) {
            apps_.erase(apps_.begin() + static_cast<std::ptrdiff_t>(i - 1));
            ++removed;
        }
    }
    if (removed > 0) {
        ++revision_;
    }
    return removed;
}

}  // namespace app
}  // namespace stipple
