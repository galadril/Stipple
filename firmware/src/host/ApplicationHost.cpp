// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/host/ApplicationHost.h"

#include "notrix/api/JsonWriter.h"
#include "notrix/core/Version.h"
#include "notrix/demo/TestPattern.h"
#include "notrix/graphics/Canvas.h"
#include "notrix/text/Text.h"

namespace notrix {
namespace host {

const char* bootModeName(BootMode mode) noexcept {
    switch (mode) {
        case BootMode::Normal: return "normal";
        case BootMode::SafeMode: return "safe";
    }
    return "unknown";
}

ApplicationHost::ApplicationHost(platform::IPlatformServices& platform, HostConfig config)
    : platform_(platform),
      config_(std::move(config)),
      configStore_(platform.storage()),
      carousel_(registry_),
      scene_(sceneTokens_, kSceneTokens),
      apiServer_(makeContext(), config_.api) {}

api::ApiContext ApplicationHost::makeContext() noexcept {
    api::ApiContext context;
    context.apps = &registry_;
    context.carousel = &carousel_;
    context.notifications = &notifications_;
    context.config = &settings_;
    context.configStore = &configStore_;
    context.platform = &platform_;
    return context;
}

// --- boot state --------------------------------------------------------------

BootRecord ApplicationHost::readBootRecord() {
    BootRecord record;

    std::string stored;
    if (!platform_.storage().read(kBootStateKey, stored)) {
        return record;  // first ever boot
    }

    json::Token tokens[16];
    json::Document document(tokens, 16);
    if (document.parse(stored) != json::Error::None) {
        // Unreadable boot state is itself suspicious, but it must not be what
        // stops the device starting.
        logger_.warn(0, "boot record unreadable; treating as first boot");
        return record;
    }

    const json::Value root = document.root();
    const std::int64_t failures = root["consecutiveFailures"].toInt(0);
    record.consecutiveFailures =
        failures < 0 ? 0u : static_cast<std::uint32_t>(failures > 100 ? 100 : failures);
    record.lastBootCompleted = root["lastBootCompleted"].toBool(true);
    return record;
}

void ApplicationHost::writeBootRecord(const BootRecord& record) {
    api::JsonWriter writer;
    writer.beginObject()
        .member("consecutiveFailures", static_cast<std::int64_t>(record.consecutiveFailures))
        .member("lastBootCompleted", record.lastBootCompleted)
        .endObject();

    if (!platform_.storage().write(kBootStateKey, writer.str())) {
        // Losing the marker costs the anti-brick guarantee but not this boot.
        logger_.error(lastTickMillis_, "could not persist boot record");
    }
}

void ApplicationHost::markHealthy() {
    healthy_ = true;

    BootRecord record;
    record.consecutiveFailures = 0;
    record.lastBootCompleted = true;
    writeBootRecord(record);
    bootRecord_ = record;

    logger_.info(lastTickMillis_, "boot healthy");
}

// --- startup -----------------------------------------------------------------

bool ApplicationHost::initialize() {
    // 1. Logging first, so everything that follows can be recorded.
    logger_.info(0, "NOTRIX starting");

    // 2. Boot state, before anything that could crash.
    BootRecord record = readBootRecord();
    if (!record.lastBootCompleted) {
        ++record.consecutiveFailures;
        logger_.warn(0, "previous boot did not complete");
    }

    bootMode_ = record.consecutiveFailures >= static_cast<std::uint32_t>(config_.safeModeThreshold)
                    ? BootMode::SafeMode
                    : BootMode::Normal;
    if (bootMode_ == BootMode::SafeMode) {
        logger_.error(0, "repeated boot failures; starting in safe mode");
    }

    record.lastBootCompleted = false;
    writeBootRecord(record);
    bootRecord_ = record;

    // 3. Display. The panel's own floor beats any configured target rate.
    scheduler_.setMinimumInterval(platform_.display().minimumFrameIntervalMillis());

    // 4. Settings. Safe mode deliberately ignores stored configuration, since a
    //    bad value in it is one of the things that could have caused the
    //    failures that got us here.
    if (bootMode_ == BootMode::SafeMode) {
        settings_ = config::Config{};
        logger_.warn(0, "safe mode: using default settings");
    } else {
        const config::LoadReport report = configStore_.load(settings_);
        logger_.info(0, config::describe(report.status));
    }
    platform_.display().setBrightness(settings_.display.brightness);

    app::CarouselConfig carousel;
    carousel.defaultDurationSeconds = settings_.apps.defaultDurationSeconds;
    carousel_.setConfig(carousel);

    // 5. Apps.
    if (bootMode_ == BootMode::Normal) {
        installBuiltins();
    } else {
        logger_.warn(0, "safe mode: no apps installed");
    }

    // 6. Capabilities this build does not have. Logged rather than silently
    //    absent, so a device that cannot be reached says why.
    if (platform_.network() == nullptr) {
        logger_.info(0, "no network interface on this platform");
    }
    if (platform_.httpServer() == nullptr) {
        logger_.info(0, "no HTTP transport; API is reachable in-process only");
    }

    splashDetail_ = apps::splashDetail(kVersion, platform_.network());
    splashActive_ = config_.splashMillis > 0;

    initialized_ = true;
    logger_.info(0, "startup complete");
    return true;
}

void ApplicationHost::installBuiltins() {
    if (!config_.installClockApp) {
        return;
    }
    app::App clock;
    clock.id = std::string(kClockAppId);
    clock.name = "Clock";
    clock.source = app::AppSource::System;
    clock.builtin = app::Builtin::Clock;
    clock.durationSeconds = 0;  // uses the carousel default
    registry_.put(std::move(clock));
}

void ApplicationHost::shutdown() {
    if (shutdownRequested_) {
        return;
    }
    shutdownRequested_ = true;

    // Only record a clean shutdown if the boot actually succeeded; otherwise the
    // failure counter must survive to trigger safe mode next time.
    if (healthy_) {
        BootRecord record;
        record.consecutiveFailures = 0;
        record.lastBootCompleted = true;
        writeBootRecord(record);
    }
    logger_.info(lastTickMillis_, "shutdown");
}

// --- input -------------------------------------------------------------------

void ApplicationHost::dismissSplash() noexcept {
    if (splashActive_) {
        splashActive_ = false;
        scheduler_.invalidate();
    }
}

void ApplicationHost::handleInput(const platform::InputEvent& event) {
    // Any interaction means the user is looking at the device and wants to get
    // on with it. The press is consumed rather than also performing its normal
    // action: someone tapping a button to skip the splash does not expect to
    // silently pause the carousel at the same time.
    if (splashActive_) {
        dismissSplash();
        mapper_.reset();  // no half-finished press survives the transition
        return;
    }

    input::ActionEvent action;
    if (!mapper_.handle(event, action)) {
        return;
    }

    switch (action.action) {
        case input::Action::AppNext:
            for (int i = 0; i < action.repeat; ++i) {
                carousel_.next(lastTickMillis_);
            }
            break;
        case input::Action::AppPrevious:
            for (int i = 0; i < action.repeat; ++i) {
                carousel_.previous(lastTickMillis_);
            }
            break;
        case input::Action::AppAction:
            carousel_.setPaused(!carousel_.paused());
            break;
        case input::Action::NotificationDismiss:
            if (!notifications_.dismissActive(lastTickMillis_)) {
                carousel_.setPaused(!carousel_.paused());
            }
            break;
        case input::Action::BrightnessUp:
        case input::Action::BrightnessDown: {
            const int delta = action.action == input::Action::BrightnessUp ? 16 : -16;
            int level = static_cast<int>(settings_.display.brightness) + delta * action.repeat;
            level = level < 0 ? 0 : (level > 255 ? 255 : level);
            settings_.display.brightness = static_cast<std::uint8_t>(level);
            platform_.display().setBrightness(settings_.display.brightness);
            break;
        }
        default:
            break;
    }

    scheduler_.invalidate();
}

void ApplicationHost::pumpInput(std::uint64_t nowMillis) {
    (void)nowMillis;
    platform::InputEvent event;
    while (platform_.input().poll(event)) {
        handleInput(event);
    }
}

// --- the loop ----------------------------------------------------------------

bool ApplicationHost::splashElapsed(std::uint64_t nowMillis) const noexcept {
    if (!ticking_) {
        return false;
    }
    if (nowMillis < firstTickMillis_) {
        return true;  // clock stepped back; do not strand the splash on screen
    }
    return (nowMillis - firstTickMillis_) >= config_.splashMillis;
}

bool ApplicationHost::tick(std::uint64_t nowMillis) {
    if (!initialized_ || shutdownRequested_) {
        return false;
    }
    lastTickMillis_ = nowMillis;

    if (!ticking_) {
        ticking_ = true;
        firstTickMillis_ = nowMillis;
    }

    pumpInput(nowMillis);

    if (splashActive_) {
        if (splashElapsed(nowMillis)) {
            splashActive_ = false;
            logger_.info(nowMillis, "splash finished");
            scheduler_.invalidate();
        } else {
            // The detail line scrolls, so every frame differs.
            scheduler_.invalidate();
        }
    }

    if (!splashActive_ && bootMode_ == BootMode::Normal) {
        if (carousel_.tick(nowMillis)) {
            scheduler_.invalidate();
        }
        if (notifications_.tick(nowMillis)) {
            scheduler_.invalidate();
        }

        // Anything time-varying has to say so, or dirty tracking would leave it
        // frozen between content changes.
        if (notifications_.active() != nullptr) {
            scheduler_.invalidate();
        } else if (const app::App* active = carousel_.active()) {
            if (active->builtin == app::Builtin::TestPattern) {
                scheduler_.invalidate();
            } else if (active->builtin == app::Builtin::Clock) {
                if (apps::clockChanged(platform_.clock(), currentClockStyle(), lastClockMillis_,
                                       nowMillis)) {
                    scheduler_.invalidate();
                }
            } else if (refreshActiveScene() && scene_.animates()) {
                scheduler_.invalidate();
            }
        }
    }

    if (scheduler_.beginFrame(nowMillis)) {
        const std::uint64_t startedAt = platform_.clock().monotonicMillis();

        renderFrame(nowMillis);
        platform_.display().present(framebuffer_);

        const std::uint64_t finishedAt = platform_.clock().monotonicMillis();
        scheduler_.endFrame(nowMillis, static_cast<std::uint32_t>(finishedAt - startedAt));
        lastClockMillis_ = nowMillis;

        ++framesSinceBoot_;
    }

    // Health is a frame count OR an elapsed time, whichever lands first. With
    // dirty rendering a static screen may draw exactly once and then legitimately
    // stop, so waiting on frames alone would never mark it healthy.
    if (!healthy_ && framesSinceBoot_ > 0) {
        const bool enoughFrames = framesSinceBoot_ >= config_.healthyAfterFrames;
        const bool enoughTime = nowMillis >= firstTickMillis_ &&
                                (nowMillis - firstTickMillis_) >= config_.healthyAfterMillis;
        if (enoughFrames || enoughTime) {
            markHealthy();
        }
    }

    return true;
}

std::uint64_t ApplicationHost::nextDueMillis(std::uint64_t nowMillis) const {
    return scheduler_.nextDueMillis(nowMillis);
}

// --- rendering ---------------------------------------------------------------

apps::ClockStyle ApplicationHost::currentClockStyle() const noexcept {
    apps::ClockStyle style = config_.clock;
    style.theme = apps::clockThemeFromName(settings_.clock.theme);
    style.twentyFourHour = settings_.clock.twentyFourHour;
    return style;
}

bool ApplicationHost::refreshActiveScene() {
    const app::App* active = carousel_.active();
    if (active == nullptr || active->builtin != app::Builtin::None) {
        return false;
    }

    // Re-parse only when the app or the registry changed. The Scene holds views
    // into the app's JSON, so both must match before it is safe to render.
    if (parsedAppId_ != active->id || parsedRevision_ != registry_.revision()) {
        sceneReady_ = scene_.load(active->sceneJson);
        parsedAppId_ = active->id;
        parsedRevision_ = registry_.revision();

        if (!sceneReady_) {
            logger_.warn(lastTickMillis_, "active app has an unusable scene");
        }
    }
    return sceneReady_;
}

void ApplicationHost::renderSafeMode() {
    Canvas canvas(framebuffer_);
    canvas.clear();

    text::TextStyle style;
    style.font = &text::font5x7();
    style.color = rgb(255, 80, 40);
    style.hAlign = text::HAlign::Center;
    style.vAlign = text::VAlign::Top;
    text::draw(canvas, "SAFE", Rect{0, 0, Framebuffer::kWidth, 7}, style);

    style.color = rgb(120, 120, 120);
    text::draw(canvas, "MODE", Rect{0, 9, Framebuffer::kWidth, 7}, style);
}

void ApplicationHost::renderFrame(std::uint64_t nowMillis) {
    if (bootMode_ == BootMode::SafeMode) {
        renderSafeMode();
        return;
    }

    Canvas canvas(framebuffer_);
    canvas.clear();

    if (splashActive_) {
        apps::renderSplash(canvas, "NOTRIX", splashDetail_, nowMillis - firstTickMillis_,
                           config_.splash);
        return;
    }

    // Notifications take the whole panel: they interrupt rather than share.
    if (const notify::Notification* alert = notifications_.active()) {
        notify::render(canvas, *alert, Framebuffer::bounds(),
                       notifications_.activeElapsedMillis(nowMillis));
        return;
    }

    const app::App* active = carousel_.active();
    if (active == nullptr) {
        return;  // nothing enabled; a blank panel is the honest result
    }

    switch (active->builtin) {
        case app::Builtin::Clock:
            apps::renderClock(canvas, platform_.clock(), currentClockStyle());
            return;
        case app::Builtin::TestPattern:
            demo::drawTestPattern(canvas, static_cast<int>(nowMillis / 33u));
            return;
        case app::Builtin::None:
            break;
    }

    if (refreshActiveScene()) {
        // Scroll position is measured from when the app appeared, so each app
        // starts reading from the beginning of its text.
        scene_.render(canvas, carousel_.dwellMillis(nowMillis));
    }
}

// --- API ---------------------------------------------------------------------

api::Response ApplicationHost::handle(const api::Request& request) {
    const api::Response response = apiServer_.handle(request, lastTickMillis_);

    // Anything that mutates state may have changed what should be on screen.
    if (request.method != api::Method::Get && response.status < 400) {
        scheduler_.invalidate();
    }
    return response;
}

}  // namespace host
}  // namespace notrix
