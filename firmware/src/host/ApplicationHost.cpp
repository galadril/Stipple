// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/host/ApplicationHost.h"

#include "notrix/apps/BatteryApp.h"
#include "notrix/render/Overlay.h"
#include "notrix/apps/VisualizerApp.h"

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
    context.icons = &icons_;
    context.config = &settings_;
    context.configStore = &configStore_;
    context.platform = &platform_;
    context.logger = &logger_;
    context.frame = &framebuffer_;
    context.input = this;
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
        logger_.warn(platform_.clock().monotonicMillis(),
                     "boot record unreadable; treating as first boot");
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
    // Startup used to log with a literal 0, so every boot line rendered as
    // 00:00:00 and sorted before everything else. The clock is available the
    // whole time; there was never a reason not to ask it.
    const std::uint64_t startedAt = platform_.clock().monotonicMillis();

    // 1. Logging first, so everything that follows can be recorded.
    logger_.info(startedAt, "NOTRIX starting");

    // 2. Boot state, before anything that could crash.
    BootRecord record = readBootRecord();
    if (!record.lastBootCompleted) {
        ++record.consecutiveFailures;
        logger_.warn(startedAt, "previous boot did not complete");
    }

    bootMode_ = record.consecutiveFailures >= static_cast<std::uint32_t>(config_.safeModeThreshold)
                    ? BootMode::SafeMode
                    : BootMode::Normal;
    if (bootMode_ == BootMode::SafeMode) {
        logger_.error(startedAt, "repeated boot failures; starting in safe mode");
    }

    record.lastBootCompleted = false;
    writeBootRecord(record);
    bootRecord_ = record;

    scene_.setIconStore(&icons_);

    // 3. Display. The panel's own floor beats any configured target rate.
    scheduler_.setMinimumInterval(platform_.display().minimumFrameIntervalMillis());

    // 4. Settings. Safe mode deliberately ignores stored configuration, since a
    //    bad value in it is one of the things that could have caused the
    //    failures that got us here.
    if (bootMode_ == BootMode::SafeMode) {
        settings_ = config::Config{};
        logger_.warn(startedAt, "safe mode: using default settings");
    } else {
        const config::LoadReport report = configStore_.load(settings_);
        logger_.info(startedAt, config::describe(report.status));
    }
    platform_.display().setBrightness(settings_.display.brightness);
    if (platform_.audio() != nullptr) {
        platform_.audio()->setVolume(config::volumeToByte(settings_.audio.volumePercent));
    }

    app::CarouselConfig carousel;
    carousel.defaultDurationSeconds = settings_.apps.defaultDurationSeconds;
    carousel_.setConfig(carousel);

    // 5. Apps and stored assets.
    if (bootMode_ == BootMode::Normal) {
        installBuiltins();
        loadIcons();
    } else {
        logger_.warn(startedAt, "safe mode: no apps or icons loaded");
    }
    persistedIconRevision_ = icons_.revision();

    // 6. Capabilities this build does not have. Logged rather than silently
    //    absent, so a device that cannot be reached says why.
    if (platform_.network() == nullptr) {
        logger_.info(startedAt, "no network interface on this platform");
    }
    if (platform_.httpServer() == nullptr) {
        logger_.info(startedAt, "no HTTP transport; API is reachable in-process only");
    }
    mqtt::ServiceContext mqttContext;
    mqttContext.client = platform_.mqtt();
    mqttContext.api = &apiServer_;
    mqttContext.settings = &settings_;
    mqttContext.logger = &logger_;
    mqtt_.setContext(mqttContext);
    if (bootMode_ == BootMode::Normal) {
        mqtt_.configure(startedAt);
    } else {
        // Safe mode stays off the network entirely. Whatever put the device here
        // might be reachable from a broker, and a boot loop that republishes
        // retained state each time is worse than a quiet one.
        logger_.warn(startedAt, "safe mode: MQTT not started");
    }

    if (platform_.audio() == nullptr) {
        // Said out loud because the default button mapping puts volume on the
        // − / + taps: without a speaker those presses do nothing, and a silent
        // no-op reads as broken hardware.
        logger_.info(startedAt, "no audio output; volume controls will do nothing");
    }

    splashDetail_ = apps::splashDetail(kVersion, platform_.network());
    splashActive_ = config_.splashMillis > 0;

    initialized_ = true;
    logger_.info(startedAt, "startup complete");
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

    // Battery is installed only where the platform can actually report one.
    // Registering it unconditionally would put a permanent "NO BATT" card in
    // the rotation of every mains-only panel, which is the carousel equivalent
    // of a switch that does nothing.
    // Same rule as the battery app: installed only where the hardware can
    // actually feed it. A visualiser permanently showing NO MIC is a card in
    // the rotation that exists to apologise.
    if (platform_.microphone() != nullptr) {
        app::App visualizer;
        visualizer.id = std::string(kVisualizerAppId);
        visualizer.name = "Visualizer";
        visualizer.source = app::AppSource::System;
        visualizer.builtin = app::Builtin::Visualizer;
        visualizer.durationSeconds = 0;
        registry_.put(std::move(visualizer));
    }

    if (platform_.power() != nullptr) {
        app::App battery;
        battery.id = std::string(kBatteryAppId);
        battery.name = "Battery";
        battery.source = app::AppSource::System;
        battery.builtin = app::Builtin::Battery;
        battery.durationSeconds = 0;
        registry_.put(std::move(battery));
    }
}

void ApplicationHost::loadIcons() {
    std::string blob;
    if (!platform_.storage().read(kIconStateKey, blob)) {
        return;  // nothing stored yet
    }

    if (!icons_.deserialize(blob)) {
        // Corrupt icon data must not stop the device starting; it just means no
        // icons. Dropping the key avoids re-reading the same broken blob every
        // boot and keeps the failure from looking intermittent.
        logger_.warn(platform_.clock().monotonicMillis(),
                     "stored icons unreadable; discarding them");
        platform_.storage().remove(kIconStateKey);
        return;
    }
    logger_.info(platform_.clock().monotonicMillis(), "icons loaded");
}

void ApplicationHost::persistIconsIfChanged() {
    if (icons_.revision() == persistedIconRevision_) {
        return;
    }
    persistedIconRevision_ = icons_.revision();

    if (icons_.count() == 0) {
        platform_.storage().remove(kIconStateKey);
        return;
    }
    if (!platform_.storage().write(kIconStateKey, icons_.serialize())) {
        logger_.error(lastTickMillis_, "could not persist icons");
    }
}

void ApplicationHost::shutdown() {
    if (shutdownRequested_) {
        return;
    }
    shutdownRequested_ = true;

    // Before the boot record, so availability flips even if writing that fails.
    mqtt_.shutdown();

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

    // Someone pressing a button has already decided; making them watch the
    // rest of an animation is the interface arguing (DESIGN.md section 6).
    transitionActive_ = false;

    input::ActionEvent action;
    if (!mapper_.handle(event, action)) {
        return;
    }

    switch (action.action) {
        // action.repeat is deliberately ignored for navigation.
        //
        // InputMapper accelerates detents that arrive within 120 ms, up to 5x,
        // which is right for a continuous value and wrong for a short list. On
        // a device with three apps, any ordinary turn of the knob jumped two to
        // five of them and landed somewhere that looked arbitrary - the carousel
        // "weirdly moving between apps".
        //
        // Nobody spins a knob to skip apps; they turn it to look at the next
        // one. One detent, one app, however fast the wrist. Acceleration stays
        // where it earns its place, on brightness and volume below.
        case input::Action::AppNext:
            transitionDirection_ = render::TransitionDirection::Forward;
            carousel_.next(lastTickMillis_);
            break;
        case input::Action::AppPrevious:
            transitionDirection_ = render::TransitionDirection::Backward;
            carousel_.previous(lastTickMillis_);
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
            const int step = mapper_.config().brightnessStep;
            const int delta = action.action == input::Action::BrightnessUp ? step : -step;
            int level = static_cast<int>(settings_.display.brightness) + delta * action.repeat;
            level = level < 0 ? 0 : (level > 255 ? 255 : level);
            settings_.display.brightness = static_cast<std::uint8_t>(level);
            platform_.display().setBrightness(settings_.display.brightness);

            // Turning the panel up is also the obvious way to ask for it back
            // after switching it off, and leaving it dark would look like the
            // button had failed.
            if (level > 0) {
                settings_.display.power = true;
            }
            break;
        }
        case input::Action::VolumeUp:
        case input::Action::VolumeDown: {
            // Silently ignored when the platform has no speaker: an absent
            // capability is reported at boot rather than faked here (ADR 0013).
            mqtt::ServiceContext mqttContext;
    mqttContext.client = platform_.mqtt();
    mqttContext.api = &apiServer_;
    mqttContext.settings = &settings_;
    mqttContext.logger = &logger_;
    mqtt_.setContext(mqttContext);
    if (bootMode_ == BootMode::Normal) {
        mqtt_.configure(lastTickMillis_);
    } else {
        // Safe mode stays off the network entirely. Whatever put the device here
        // might be reachable from a broker, and a boot loop that republishes
        // retained state each time is worse than a quiet one.
        logger_.warn(lastTickMillis_, "safe mode: MQTT not started");
    }

    if (platform_.audio() == nullptr) {
                break;
            }
            // Percent, because that is how a volume control reads to a person,
            // converted once at the edge where the hardware wants 0-255.
            const int step = mapper_.config().volumeStepPercent;
            const int delta = action.action == input::Action::VolumeUp ? step : -step;
            int percent = static_cast<int>(settings_.audio.volumePercent) + delta * action.repeat;
            percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
            settings_.audio.volumePercent = static_cast<std::uint8_t>(percent);
            platform_.audio()->setVolume(config::volumeToByte(settings_.audio.volumePercent));
            break;
        }
        case input::Action::None:
            break;
    }

    // Automations can react to the hardware even when the action itself is
    // local (§20). Published after handling, so a subscriber never sees an
    // event the device has not already acted on.
    mqtt_.publishButton(input::actionName(action.action), action.repeat, action.longPress);

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
    persistIconsIfChanged();

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
        // Settings are shared by pointer with the API, so the display can be
        // switched off between two ticks. Noticing it here rather than at the
        // call site means every future route to the setting — MQTT, buttons, a
        // schedule — gets the redraw for free.
        if (settings_.display.power != renderedWithPower_) {
            renderedWithPower_ = settings_.display.power;
            scheduler_.invalidate();
        }

        // Timekeeping continues while the panel is off — apps still rotate and
        // notifications still expire — so switching it back on shows the present
        // moment rather than a resumed backlog.
        // Sampled every tick regardless of which app is showing, so switching
        // to the visualiser mid-sound shows what just happened rather than
        // starting from an empty panel.
        if (platform::IMicrophone* microphone = platform_.microphone()) {
            const platform::SoundLevel sound = microphone->level();
            if (sound.known) {
                visualizer_.push(sound.amplitude);
            }
        }

        const bool carouselMoved = carousel_.tick(nowMillis);
        const bool notificationsMoved = notifications_.tick(nowMillis);

        // Only the redrawing stops. Without this a dark panel would re-render
        // black at the full frame rate, which is the one thing an off switch is
        // supposed to avoid.
        if (settings_.display.power) {
            if (carouselMoved || notificationsMoved) {
                scheduler_.invalidate();
            }

            // A rotation that happened on its own always reads as forward. A
            // knob turn sets the direction before calling next()/previous(),
            // and beginTransition keeps whichever was set most recently.
            if (carouselMoved && !splashActive_) {
                beginTransition(nowMillis, transitionDirection_);
            }

            // A transition is motion by definition, so it has to keep asking
            // for frames for as long as it runs - dirty tracking would
            // otherwise freeze it on its first step.
            if (transitionRunning(nowMillis)) {
                scheduler_.invalidate();
            }

            // Weather moves, so dirty tracking must not freeze it. Checked at
            // the frame interval rather than every tick, since that is the
            // fastest it could usefully change anyway.
            if (settings_.display.overlay != "none") {
                scheduler_.invalidate();
            }

            // Anything time-varying has to say so, or dirty tracking would leave
            // it frozen between content changes.
            if (notifications_.active() != nullptr) {
                scheduler_.invalidate();
            } else if (const app::App* active = carousel_.active()) {
                if (active->builtin == app::Builtin::TestPattern) {
                    scheduler_.invalidate();
                } else if (active->builtin == app::Builtin::Visualizer) {
                    // Sound does not wait for a redraw to be due.
                    scheduler_.invalidate();
                } else if (active->builtin == app::Builtin::Battery) {
                    // Once a second is ample for a value that moves a percent
                    // an hour, and still far more responsive than the panel
                    // needs. Without it the card would freeze at whatever the
                    // charge was when it first drew.
                    if ((nowMillis / 1000u) != (lastClockMillis_ / 1000u)) {
                        scheduler_.invalidate();
                    }
                } else if (active->builtin == app::Builtin::Clock) {
                    if (apps::clockChanged(platform_.clock(), clockStyle(),
                                           lastClockMillis_, nowMillis)) {
                        scheduler_.invalidate();
                    }
                } else if (refreshActiveScene() && scene_.animates()) {
                    scheduler_.invalidate();
                }
            }
        }
    }

    // MQTT runs off the same loop as everything else, so nothing arrives on a
    // thread the rest of the firmware does not know about.
    if (bootMode_ == BootMode::Normal) {
        mqtt::MqttService::DeviceState state;
        if (const app::App* active = carousel_.active()) {
            state.activeAppId = active->id;
        }
        state.healthy = healthy_;
        if (platform_.network() != nullptr) {
            const platform::NetworkStatus status = platform_.network()->status();
            state.rssiDbm = status.rssiDbm;
            state.hasRssi = status.connected;
        }
        mqtt_.setDeviceState(std::move(state));
        mqtt_.tick(nowMillis);
    }

    if (scheduler_.beginFrame(nowMillis)) {
        const std::uint64_t startedAt = platform_.clock().monotonicMillis();

        renderFrame(nowMillis);

        // Over the app, under the transition. Additive, so it only lights
        // pixels the app left dark - a raindrop passes behind the digits
        // rather than through them (DESIGN.md section 7).
        if (settings_.display.power && !splashActive_) {
            const render::Overlay overlay =
                render::overlayFromName(settings_.display.overlay);
            if (overlay != render::Overlay::None) {
                render::drawOverlay(framebuffer_, overlay, nowMillis);
            }
        }

        // Composited after rendering, never during it. renderFrame only ever
        // draws the app that is active now; the outgoing frame was captured
        // when the change happened, so nothing in the renderer knows a
        // transition exists.
        if (transitionRunning(nowMillis)) {
            transitionScratch_ = framebuffer_;
            const std::uint64_t elapsed = nowMillis - transitionStartMillis_;
            const int permille = static_cast<int>(
                (elapsed * 1000u) / render::kTransitionMillis);
            render::composite(framebuffer_, previousFrame_, transitionScratch_,
                              transitionStyle_, transitionDirection_, permille);
        } else {
            transitionActive_ = false;
        }

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

apps::ClockStyle ApplicationHost::clockStyle() const noexcept {
    // Starts from the build-time style so anything the user has not chosen keeps
    // whatever this build considers sensible, then applies stored settings over
    // the top. Names that are not recognised fall back inside the converters.
    apps::ClockStyle style = config_.clock;
    style.theme = apps::clockThemeFromName(settings_.clock.theme);
    style.twentyFourHour = settings_.clock.twentyFourHour;
    style.leadingZero = settings_.clock.leadingZero;
    style.showAmPm = settings_.clock.showAmPm;
    style.color = fromPacked(settings_.clock.color);
    style.accentColor = fromPacked(settings_.clock.accentColor);
    style.dateColor = fromPacked(settings_.clock.dateColor);
    style.dateOrder = apps::dateOrderFromName(settings_.clock.dateOrder);
    style.dateSeparator = apps::dateSeparatorFromName(settings_.clock.dateSeparator);
    style.dateYear = apps::dateYearFromName(settings_.clock.dateYear);
    style.blinkPeriodMillis = settings_.clock.blinkPeriodMillis;
    style.utcOffsetSeconds = settings_.clock.utcOffsetSeconds;
    return style;
}

bool ApplicationHost::refreshActiveScene() {
    const app::App* active = carousel_.active();
    if (active == nullptr || active->builtin != app::Builtin::None) {
        return false;
    }

    // Re-parse only when the app or the registry changed. The Scene holds views
    // into the app's JSON, so both must match before it is safe to render.
    // Icon changes matter too: a scene validated against a missing icon must be
    // revalidated once that icon exists.
    const std::uint32_t revision = registry_.revision() ^ (icons_.revision() << 16);
    if (parsedAppId_ != active->id || parsedRevision_ != revision) {
        sceneReady_ = scene_.load(active->sceneJson);
        parsedAppId_ = active->id;
        parsedRevision_ = revision;

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

    // Display off. The panel is cleared and still presented, so it goes properly
    // dark rather than freezing on whatever was last drawn. Safe mode is checked
    // first on purpose: a stored `power: false` must never be able to hide the
    // reason the device ended up in safe mode.
    if (!settings_.display.power) {
        return;
    }

    if (splashActive_) {
        apps::renderSplash(canvas, "NOTRIX", splashDetail_, nowMillis - firstTickMillis_,
                           config_.splashMillis,
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
            apps::renderClock(canvas, platform_.clock(), clockStyle());
            return;
        case app::Builtin::Visualizer: {
            // A present IMicrophone is not the same as a working one, and on
            // the TC002 the difference is the whole bug: the adapter offers
            // itself as a microphone as soon as the MCU serial port opens,
            // then never receives an audio frame. The pointer check passed,
            // no sample was ever pushed, and the app drew its baseline - a
            // flat line that reads as a silent room rather than as a device
            // that cannot hear.
            //
            // Asking whether anything has actually been heard covers both
            // cases honestly. ADR 0013 is about exactly this: absence should
            // be visible, not dressed up as a plausible value.
            if (platform_.microphone() == nullptr || !visualizer_.hasSamples()) {
                apps::renderNoMicrophone(canvas, colors::kWhite);
                return;
            }
            visualizer_.render(canvas);
            return;
        }
        case app::Builtin::Battery: {
            platform::BatteryStatus status;
            if (platform::IPowerSource* power = platform_.power()) {
                status = power->battery();
            }
            apps::renderBattery(canvas, status, apps::BatteryStyle{});
            return;
        }
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

void ApplicationHost::beginTransition(std::uint64_t nowMillis,
                                      render::TransitionDirection direction) {
    if (!settings_.apps.transitions) {
        transitionActive_ = false;
        return;
    }

    // The frame already on the panel becomes the outgoing one. Capturing it
    // here is what lets the renderer stay ignorant of transitions entirely: it
    // only ever draws the app that is active now.
    previousFrame_ = framebuffer_;
    transitionStartMillis_ = nowMillis;
    transitionDirection_ = direction;
    // Notifications arrive rather than rotate, so they fade; the carousel
    // slides (DESIGN.md section 6).
    transitionStyle_ = notifications_.active() != nullptr
                           ? render::TransitionStyle::Fade
                           : render::TransitionStyle::Slide;
    transitionActive_ = true;
}

bool ApplicationHost::transitionRunning(std::uint64_t nowMillis) const noexcept {
    if (!transitionActive_) {
        return false;
    }
    return nowMillis - transitionStartMillis_ < render::kTransitionMillis;
}

// --- API ---------------------------------------------------------------------

api::Response ApplicationHost::handle(const api::Request& request) {
    // The configuration UI is tried first, but only for paths the API does not
    // own. Ordering it this way means a future asset called "api" could never
    // shadow an endpoint, and an unknown /api/v1 path still gets the API's own
    // 404 rather than a confusing "no such page".
    if (request.path.rfind("/api/", 0) != 0) {
        api::Response staticResponse;
        if (staticFiles_.tryHandle(request, staticResponse)) {
            return staticResponse;
        }
    }

    const api::Response response = apiServer_.handle(request, lastTickMillis_);

    // Settings are shared by pointer with the API, so a PATCH may have pointed
    // MQTT at a different broker. Re-reading is cheap and does nothing when
    // nothing relevant changed.
    if (request.method != api::Method::Get && response.status < 400 &&
        bootMode_ == BootMode::Normal) {
        mqtt_.configure(lastTickMillis_);
        mqtt_.invalidateStatus();
    }

    // Log what changed the device and what failed, but not routine reads. A
    // dashboard polling /health every second would otherwise push everything
    // worth seeing out of a 24-entry ring within half a minute.
    const bool mutating = request.method != api::Method::Get;
    if (mutating || response.status >= 400) {
        std::string line = api::methodName(request.method);
        line += ' ';
        line += request.path;
        line += ' ';
        line += std::to_string(response.status);

        if (response.status >= 500) {
            logger_.error(lastTickMillis_, line);
        } else if (response.status >= 400) {
            logger_.warn(lastTickMillis_, line);
        } else {
            logger_.info(lastTickMillis_, line);
        }
    }

    if (mutating && response.status < 400) {
        // Anything that mutates state may have changed what should be on screen.
        scheduler_.invalidate();
    }
    return response;
}

}  // namespace host
}  // namespace notrix
