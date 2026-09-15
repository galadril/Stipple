// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "notrix/api/ApiServer.h"
#include "notrix/app/Carousel.h"
#include "notrix/asset/IconStore.h"
#include "notrix/apps/ClockApp.h"
#include "notrix/apps/SplashScreen.h"
#include "notrix/config/Config.h"
#include "notrix/core/Log.h"
#include "notrix/graphics/Framebuffer.h"
#include "notrix/input/InputMapper.h"
#include "notrix/json/Json.h"
#include "notrix/notify/Notifications.h"
#include "notrix/platform/HttpServer.h"
#include "notrix/platform/PlatformServices.h"
#include "notrix/render/FrameScheduler.h"
#include "notrix/scene/Scene.h"

namespace notrix {
namespace host {

enum class BootMode : std::uint8_t {
    Normal,
    /// Reached after repeated failed boots. Defaults only, no stored apps or
    /// settings, and a screen that says so.
    SafeMode,
};

const char* bootModeName(BootMode mode) noexcept;

/// Persisted across reboots to detect a device that cannot get through startup.
struct BootRecord {
    std::uint32_t consecutiveFailures = 0;
    /// False while a boot is in progress. If it is still false on the next
    /// startup, the previous attempt never finished.
    bool lastBootCompleted = true;
};

struct HostConfig {
    /// Consecutive unfinished boots before falling back to safe mode.
    int safeModeThreshold = 3;

    /// Frames that must render before a boot counts as healthy. More than one,
    /// so a crash on the second frame still counts as a failure.
    int healthyAfterFrames = 3;

    /// ...or this long spent ticking without incident, whichever comes first.
    ///
    /// A frame count alone is not enough once dirty rendering exists: a static
    /// clock face legitimately renders once and then nothing. Without this, such
    /// a device would never record a healthy boot, every restart would count as
    /// a failure, and it would fall into safe mode permanently — the opposite of
    /// what the anti-brick mechanism is for.
    std::uint32_t healthyAfterMillis = 3000;

    render::FrameScheduler::Config frame;
    api::ApiOptions api;
    apps::ClockStyle clock;
    apps::SplashStyle splash;

    /// How long the boot splash stays up. Long enough for a scrolling IP
    /// address to finish at least once; zero disables it. Any button press
    /// dismisses it early.
    std::uint32_t splashMillis = 5000;

    /// Register the built-in clock so a fresh device shows something.
    bool installClockApp = true;
};

/// Owns startup, the main loop and shutdown (blueprint §8.1).
///
/// The loop is `tick(now)` rather than a blocking `run()`. Core has no business
/// sleeping or owning a thread — the browser drives this from
/// requestAnimationFrame and the device will drive it from its own loop, with
/// `nextDueMillis()` telling each how long it may wait. Same code, no threading
/// primitives anywhere near the renderer.
///
/// The anti-brick guarantee (§8.1, §21) is the part that matters most before
/// hardware exists: a boot marker is written before startup and only cleared
/// once frames are actually rendering. Repeated failures bring the device up in
/// safe mode with default settings and no stored apps, so a bad configuration or
/// a poisonous app cannot leave a clock that has to be opened up to recover.
class ApplicationHost : public platform::IHttpRequestHandler {
public:
    static constexpr std::string_view kBootStateKey = "boot";
    static constexpr std::string_view kClockAppId = "clock";
    static constexpr std::string_view kIconStateKey = "icons";
    static constexpr int kSceneTokens = 512;

    ApplicationHost(platform::IPlatformServices& platform, HostConfig config = HostConfig{});

    ApplicationHost(const ApplicationHost&) = delete;
    ApplicationHost& operator=(const ApplicationHost&) = delete;

    /// Run the boot sequence. Returns false only if the platform is unusable.
    bool initialize();

    /// One iteration: input, scheduling, and a frame if one is due. Returns
    /// false once shutdown has been requested.
    bool tick(std::uint64_t nowMillis);

    /// Earliest time `tick` needs calling again. The caller sleeps until then.
    std::uint64_t nextDueMillis(std::uint64_t nowMillis) const;

    void shutdown();

    // platform::IHttpRequestHandler
    api::Response handle(const api::Request& request) override;

    BootMode bootMode() const noexcept { return bootMode_; }
    bool initialized() const noexcept { return initialized_; }

    /// True once enough frames have rendered for the boot to be recorded as
    /// successful.
    bool healthy() const noexcept { return healthy_; }

    const BootRecord& bootRecord() const noexcept { return bootRecord_; }

    log::RingLog& logger() noexcept { return logger_; }
    const log::RingLog& logger() const noexcept { return logger_; }

    app::AppRegistry& apps() noexcept { return registry_; }
    app::Carousel& carousel() noexcept { return carousel_; }
    notify::NotificationQueue& notifications() noexcept { return notifications_; }
    asset::IconStore& icons() noexcept { return icons_; }
    config::Config& settings() noexcept { return settings_; }
    render::FrameScheduler& scheduler() noexcept { return scheduler_; }
    const render::FrameStats& frameStats() const noexcept { return scheduler_.stats(); }

    /// The most recently rendered frame.
    const Framebuffer& frame() const noexcept { return framebuffer_; }

    /// Feed a raw hardware event. Normally the platform's input queue supplies
    /// these; exposed so a host can inject them directly.
    void handleInput(const platform::InputEvent& event);

    /// True while the boot splash is still showing.
    bool showingSplash() const noexcept { return splashActive_; }

    /// Cut the splash short — any button press does this.
    void dismissSplash() noexcept;

private:
    api::ApiContext makeContext() noexcept;

    BootRecord readBootRecord();
    void writeBootRecord(const BootRecord& record);
    void markHealthy();

    void installBuiltins();
    void loadIcons();
    /// Writes the icon set if it has changed since the last save. Called from
    /// tick(), so every mutation path is covered rather than just the API.
    void persistIconsIfChanged();
    void pumpInput(std::uint64_t nowMillis);
    void renderFrame(std::uint64_t nowMillis);
    void renderSafeMode();
    bool refreshActiveScene();

    /// Clock style with the user's stored preferences applied over the host
    /// defaults, so a settings change takes effect on the next frame without
    /// any extra wiring.
    apps::ClockStyle currentClockStyle() const noexcept;
    bool splashElapsed(std::uint64_t nowMillis) const noexcept;

    platform::IPlatformServices& platform_;
    HostConfig config_;

    log::RingLog logger_;
    config::ConfigStore configStore_;
    config::Config settings_;

    app::AppRegistry registry_;
    app::Carousel carousel_;
    notify::NotificationQueue notifications_;
    asset::IconStore icons_;
    input::InputMapper mapper_;
    render::FrameScheduler scheduler_;

    Framebuffer framebuffer_;
    json::Token sceneTokens_[kSceneTokens];
    scene::Scene scene_;

    std::string parsedAppId_;
    std::uint32_t parsedRevision_ = 0;
    bool sceneReady_ = false;

    BootMode bootMode_ = BootMode::Normal;
    BootRecord bootRecord_;
    bool initialized_ = false;
    bool healthy_ = false;
    bool shutdownRequested_ = false;
    int framesSinceBoot_ = 0;
    std::uint64_t lastTickMillis_ = 0;
    std::uint64_t lastClockMillis_ = 0;
    std::uint32_t persistedIconRevision_ = 0;

    bool splashActive_ = false;
    bool ticking_ = false;
    /// When tick() was first called; the splash and the health timer both
    /// measure from here.
    std::uint64_t firstTickMillis_ = 0;
    /// Built once at boot; rendering it per frame would allocate.
    std::string splashDetail_;

    // Declared last: its context holds pointers to the members above, which must
    // already be constructed when it is built.
    api::ApiServer apiServer_;
};

}  // namespace host
}  // namespace notrix
