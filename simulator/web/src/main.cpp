// SPDX-License-Identifier: GPL-3.0-or-later
//
// Emscripten entry point for the browser emulator.
//
// This file owns no application logic at all. It creates a simulator platform,
// hands it to ApplicationHost, and forwards the browser's clock and input. The
// boot sequence, splash, carousel, scene rendering, notifications, input mapping
// and frame scheduling all happen inside the core, exactly as they will on the
// device — where the entry point will be the same handful of calls against a
// TC002 platform adapter instead.

#include <emscripten/emscripten.h>

#include <cstdint>
#include <memory>
#include <string>

#include "notrix/host/ApplicationHost.h"
#include "notrix/platform/simulator/SimulatorPlatform.h"

namespace {

using notrix::app::App;
using notrix::app::AppSource;
using notrix::app::Builtin;
using notrix::Framebuffer;

/// Demo apps, in the public scene format. Nothing here is privileged: anything
/// the HTTP API accepts could produce the same screens.
struct DemoApp {
    const char* id;
    const char* name;
    int durationSeconds;
    const char* sceneJson;
};

constexpr DemoApp kDemoApps[] = {
    {"weather", "Weather", 8,
     R"({"name":"weather","elements":[
        {"type":"rect","rect":[1,4,7,7],"color":"#ff5000","fill":true},
        {"type":"text","rect":[11,0,40,7],"text":"21.4°C","align":"left","color":"#ffaa28"},
        {"type":"text","rect":[11,9,40,7],"text":"Living room · 48% humidity",
         "align":"left","color":"#00c8ff","scroll":"auto"}
     ]})"},

    {"dashboard", "Dashboard", 6,
     R"({"name":"dashboard","elements":[
        {"type":"text","rect":[0,0,26,7],"text":"CPU","align":"left","color":"#ffffff"},
        {"type":"progress","rect":[0,8,26,3],"value":72,"color":"#00ff00","background":"#003300"},
        {"type":"graph","rect":[28,1,24,14],
         "values":[3,5,4,8,6,9,7,12,10,14,11,15],"color":"#00aaff"},
        {"type":"line","x1":27,"y1":0,"x2":27,"y2":15,"color":"#303030"}
     ]})"},

    {"palette", "Palette", 5,
     R"({"name":"palette","elements":[
        {"type":"group","rect":[0,0,52,8],"elements":[
           {"type":"rect","rect":[0,0,13,8],"color":"#ff0000","fill":true},
           {"type":"rect","rect":[13,0,13,8],"color":"#00ff00","fill":true},
           {"type":"rect","rect":[26,0,13,8],"color":"#0000ff","fill":true},
           {"type":"rect","rect":[39,0,13,8],"color":"#ffff00","fill":true}
        ]},
        {"type":"text","rect":[0,9,52,7],"text":"GROUP","align":"center","color":"#ffffff"}
     ]})"},
};

struct Emulator {
    notrix::platform::simulator::SimulatorPlatform platform;
    /// Rebuilt by notrix_init, so initialising twice gives a genuinely fresh
    /// device rather than a half-reset one. ApplicationHost has no reset of its
    /// own by design — a device reboots, it does not re-initialise in place.
    std::unique_ptr<notrix::host::ApplicationHost> host;
    int notifySequence = 0;
    std::string logLine;

    notrix::host::ApplicationHost& device() { return *host; }
};

Emulator& emulator() {
    static Emulator instance;
    return instance;
}

/// Keep the simulated monotonic clock in step with the browser's.
void syncClock(Emulator& state, std::uint64_t nowMillis) {
    const std::uint64_t current = state.platform.simulatedClock().monotonicMillis();
    if (nowMillis > current) {
        state.platform.simulatedClock().advance(nowMillis - current);
    }
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void notrix_init() {
    Emulator& state = emulator();

    // Power-cycle the simulated device: clear persistent storage and any queued
    // input, then build a new host. Without the clear, the boot record would
    // carry over and repeated inits would eventually trip safe mode.
    state.platform.simulatedStorage().clear();
    state.platform.simulatedInput().clear();
    state.notifySequence = 0;
    state.host = std::make_unique<notrix::host::ApplicationHost>(state.platform);

    // A simulated address, so the boot splash has something to show. It is
    // clearly fictional: the browser has no network interface to report.
    notrix::platform::NetworkStatus network;
    network.connected = true;
    network.ipv4 = "192.168.1.42";
    network.hostname = "notrix-a1b2.local";
    network.rssiDbm = -52;
    state.platform.simulatedNetwork().setStatus(network);

    state.device().initialize();

    for (const DemoApp& demo : kDemoApps) {
        App app;
        app.id = demo.id;
        app.name = demo.name;
        app.sceneJson = demo.sceneJson;
        app.durationSeconds = demo.durationSeconds;
        app.source = AppSource::System;
        state.device().apps().put(std::move(app));
    }

    // The bring-up pattern stays available: it is the screen Phase 7 will use to
    // confirm a real panel is wired correctly.
    App pattern;
    pattern.id = "testpattern";
    pattern.name = "Test pattern";
    pattern.durationSeconds = 6;
    pattern.source = AppSource::System;
    pattern.builtin = Builtin::TestPattern;
    state.device().apps().put(std::move(pattern));
}

/// Hand the host a real wall-clock time so the built-in clock shows something
/// meaningful. Without this it correctly renders "--:--".
EMSCRIPTEN_KEEPALIVE void notrix_set_wall_clock(double unixSeconds, int utcOffsetSeconds) {
    emulator().platform.simulatedClock().setWallClock(static_cast<std::int64_t>(unixSeconds),
                                                      utcOffsetSeconds);
}

EMSCRIPTEN_KEEPALIVE void notrix_set_brightness(int value) {
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    Emulator& state = emulator();
    state.device().settings().display.brightness = static_cast<std::uint8_t>(value);
    state.platform.display().setBrightness(static_cast<std::uint8_t>(value));
    state.device().scheduler().invalidate();
}

EMSCRIPTEN_KEEPALIVE void notrix_render(int nowMillis) {
    Emulator& state = emulator();
    const std::uint64_t now = nowMillis < 0 ? 0u : static_cast<std::uint64_t>(nowMillis);
    syncClock(state, now);
    state.device().tick(now);
}

/// Feed one raw hardware event; the core decides what it means.
EMSCRIPTEN_KEEPALIVE void notrix_input(int source, int phase, int nowMillis) {
    using notrix::platform::ButtonPhase;
    using notrix::platform::InputEvent;
    using notrix::platform::RawInput;

    if (source < 0 || source > static_cast<int>(RawInput::RotaryRight)) {
        return;
    }
    if (phase < 0 || phase > static_cast<int>(ButtonPhase::Tick)) {
        return;
    }

    Emulator& state = emulator();
    const std::uint64_t now = nowMillis < 0 ? 0u : static_cast<std::uint64_t>(nowMillis);
    syncClock(state, now);

    InputEvent event;
    event.source = static_cast<RawInput>(source);
    event.phase = static_cast<ButtonPhase>(phase);
    event.timestampMillis = now;

    // Through the platform queue, so the path exercised is poll-map-act — the
    // same one the firmware takes.
    state.platform.simulatedInput().push(event);
    state.device().tick(now);
}

EMSCRIPTEN_KEEPALIVE void notrix_notify(int priority, int durationSeconds, int nowMillis) {
    Emulator& state = emulator();
    const std::uint64_t now = nowMillis < 0 ? 0u : static_cast<std::uint64_t>(nowMillis);

    notrix::notify::Notification notification;
    notification.priority = notrix::notify::priorityFromInt(priority);
    notification.durationSeconds = durationSeconds > 0 ? durationSeconds : 4;
    notification.id = "demo" + std::to_string(++state.notifySequence);

    switch (notification.priority) {
        case notrix::notify::Priority::Urgent:
            notification.text = "URGENT · door open";
            notification.color = notrix::rgb(255, 60, 40);
            break;
        case notrix::notify::Priority::Important:
            notification.text = "Washing machine finished";
            notification.color = notrix::rgb(255, 170, 40);
            break;
        default:
            notification.text = "Doorbell at the front door";
            notification.color = notrix::rgb(120, 200, 255);
            break;
    }

    state.device().notifications().push(std::move(notification), now);
    state.device().scheduler().invalidate();
}

EMSCRIPTEN_KEEPALIVE const unsigned char* notrix_framebuffer() {
    return emulator().platform.simulatedDisplay().lastFrame().bytes();
}

EMSCRIPTEN_KEEPALIVE int notrix_width() { return Framebuffer::kWidth; }
EMSCRIPTEN_KEEPALIVE int notrix_height() { return Framebuffer::kHeight; }

EMSCRIPTEN_KEEPALIVE int notrix_app_count() { return emulator().device().apps().count(); }

EMSCRIPTEN_KEEPALIVE const char* notrix_active_name() {
    Emulator& state = emulator();
    if (state.device().showingSplash()) {
        return "Starting";
    }
    const App* active = state.device().carousel().active();
    return active != nullptr ? active->name.c_str() : "";
}

EMSCRIPTEN_KEEPALIVE int notrix_active_duration_seconds() {
    return emulator().device().carousel().activeDurationSeconds();
}

EMSCRIPTEN_KEEPALIVE int notrix_dwell_millis(int nowMillis) {
    const std::uint64_t now = nowMillis < 0 ? 0u : static_cast<std::uint64_t>(nowMillis);
    return static_cast<int>(emulator().device().carousel().dwellMillis(now));
}

EMSCRIPTEN_KEEPALIVE int notrix_is_paused() {
    return emulator().device().carousel().paused() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int notrix_showing_splash() {
    return emulator().device().showingSplash() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int notrix_is_healthy() {
    return emulator().device().healthy() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int notrix_notification_count() {
    return emulator().device().notifications().size();
}

EMSCRIPTEN_KEEPALIVE int notrix_notification_pending() {
    return emulator().device().notifications().pending();
}

// Frame scheduler counters, which make dirty rendering visible: a static screen
// should skip far more frames than it draws.
EMSCRIPTEN_KEEPALIVE int notrix_frames_rendered() {
    return static_cast<int>(emulator().device().frameStats().rendered);
}

EMSCRIPTEN_KEEPALIVE int notrix_frames_skipped() {
    return static_cast<int>(emulator().device().frameStats().skipped);
}

// --- clock themes ----------------------------------------------------------

EMSCRIPTEN_KEEPALIVE int notrix_clock_theme_count() {
    return notrix::apps::kClockThemeCount;
}

EMSCRIPTEN_KEEPALIVE const char* notrix_clock_theme_name(int index) {
    return notrix::apps::clockThemeName(notrix::apps::clockThemeAt(index));
}

EMSCRIPTEN_KEEPALIVE int notrix_clock_theme() {
    Emulator& state = emulator();
    const notrix::apps::ClockTheme theme =
        notrix::apps::clockThemeFromName(state.device().settings().clock.theme);
    for (int i = 0; i < notrix::apps::kClockThemeCount; ++i) {
        if (notrix::apps::clockThemeAt(i) == theme) {
            return i;
        }
    }
    return 0;
}

/// Set the clock face and jump to the clock app so the change is visible at
/// once, rather than whenever the carousel next comes round.
EMSCRIPTEN_KEEPALIVE void notrix_set_clock_theme(int index, int nowMillis) {
    Emulator& state = emulator();
    const std::uint64_t now = nowMillis < 0 ? 0u : static_cast<std::uint64_t>(nowMillis);

    state.device().settings().clock.theme =
        notrix::apps::clockThemeName(notrix::apps::clockThemeAt(index));
    state.device().carousel().activate("clock", now);
    state.device().scheduler().invalidate();
}

EMSCRIPTEN_KEEPALIVE int notrix_log_count() {
    return emulator().device().logger().count();
}

/// Returns a pointer valid until the next call.
EMSCRIPTEN_KEEPALIVE const char* notrix_log_line(int index) {
    Emulator& state = emulator();
    const notrix::log::RingLog::Entry& entry = state.device().logger().at(index);
    state.logLine = std::string(notrix::log::levelName(entry.level)) + "  " + entry.message;
    return state.logLine.c_str();
}

}  // extern "C"
