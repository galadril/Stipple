// SPDX-License-Identifier: GPL-3.0-or-later
//
// Emscripten entry point for the browser emulator.
//
// Everything here is glue. The browser owns the frame clock and the pixels on
// screen; the core owns everything else — the app registry, the carousel, the
// scene parser, the renderer and the input mapping. Keeping this file boring is
// the point: it is the emulator's half of the §53 platform boundary, and its
// device counterpart in Phase 7 will be the same handful of functions talking
// to sendLedData and a real GPIO instead.

#include <emscripten/emscripten.h>

#include <cstdint>
#include <string>

#include "notrix/app/Carousel.h"
#include "notrix/demo/TestPattern.h"
#include "notrix/graphics/Canvas.h"
#include "notrix/input/InputMapper.h"
#include "notrix/json/Json.h"
#include "notrix/platform/simulator/SimulatorPlatform.h"
#include "notrix/scene/Scene.h"

namespace {

using notrix::app::App;
using notrix::app::AppSource;
using notrix::Canvas;
using notrix::Framebuffer;

/// Built-in demo apps. These are ordinary scenes in the public JSON format —
/// nothing here is privileged, and anything the HTTP API will accept in Phase 5
/// could produce the same screens.
struct DemoApp {
    const char* id;
    const char* name;
    int durationSeconds;
    const char* sceneJson;
};

constexpr DemoApp kDemoApps[] = {
    {"welcome", "Welcome", 5,
     R"({"name":"welcome","elements":[
        {"type":"text","rect":[0,1,52,7],"text":"NOTRIX","align":"center","color":"#00c8ff"},
        {"type":"text","rect":[0,9,52,7],"text":"0.1.0","align":"center","color":"#404040"}
     ]})"},

    {"weather", "Weather", 6,
     R"({"name":"weather","elements":[
        {"type":"rect","rect":[1,4,7,7],"color":"#ff5000","fill":true},
        {"type":"text","rect":[11,0,40,7],"text":"21.4°C","align":"left","color":"#ffaa28"},
        {"type":"text","rect":[11,9,40,7],"text":"Living rm","align":"left","color":"#00c8ff"}
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

/// The bring-up pattern is kept as a system app so there is always something
/// animated to look at, and because it is the screen Phase 7 will use to confirm
/// the real panel is wired correctly.
constexpr const char* kTestPatternId = "testpattern";

struct Emulator {
    notrix::platform::simulator::SimulatorPlatform platform;
    notrix::app::AppRegistry registry;
    notrix::app::Carousel carousel{registry};
    notrix::input::InputMapper mapper;

    notrix::json::Token tokens[768];
    notrix::scene::Scene scene{tokens, 768};

    Framebuffer framebuffer;

    /// Which app the parsed scene belongs to, and the registry revision it was
    /// parsed at. The Scene holds views into the app's JSON text, so both must
    /// match before it is safe to render.
    std::string parsedAppId;
    std::uint32_t parsedRevision = 0;
    bool sceneReady = false;

    std::uint8_t brightness = 255;
    std::uint64_t nowMillis = 0;
};

Emulator& emulator() {
    static Emulator instance;
    return instance;
}

void applyAction(Emulator& state, const notrix::input::ActionEvent& action) {
    using notrix::input::Action;

    switch (action.action) {
        case Action::AppNext:
            for (int i = 0; i < action.repeat; ++i) {
                state.carousel.next(state.nowMillis);
            }
            break;
        case Action::AppPrevious:
            for (int i = 0; i < action.repeat; ++i) {
                state.carousel.previous(state.nowMillis);
            }
            break;
        case Action::AppAction:
            state.carousel.setPaused(!state.carousel.paused());
            break;
        default:
            break;
    }
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void notrix_init() {
    Emulator& state = emulator();

    state.registry.clear();
    state.framebuffer.clear();
    state.brightness = 255;
    state.nowMillis = 0;
    state.parsedAppId.clear();
    state.sceneReady = false;
    state.mapper.reset();
    state.carousel.unpin();
    state.carousel.setPaused(false);

    for (const DemoApp& demo : kDemoApps) {
        App app;
        app.id = demo.id;
        app.name = demo.name;
        app.sceneJson = demo.sceneJson;
        app.durationSeconds = demo.durationSeconds;
        app.source = AppSource::System;
        state.registry.put(std::move(app));
    }

    App pattern;
    pattern.id = kTestPatternId;
    pattern.name = "Test pattern";
    pattern.durationSeconds = 6;
    pattern.source = AppSource::System;
    state.registry.put(std::move(pattern));

    state.carousel.tick(0);
}

/// Global brightness is a post-pass over the finished frame, which is how the
/// device applies it too: apps draw in true colour and never have to know the
/// current setting.
EMSCRIPTEN_KEEPALIVE void notrix_set_brightness(int value) {
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    emulator().brightness = static_cast<std::uint8_t>(value);
}

EMSCRIPTEN_KEEPALIVE void notrix_render(int nowMillis) {
    Emulator& state = emulator();
    state.nowMillis = nowMillis < 0 ? 0u : static_cast<std::uint64_t>(nowMillis);

    state.carousel.tick(state.nowMillis);

    Canvas canvas(state.framebuffer);
    canvas.clear();

    const App* active = state.carousel.active();
    if (active != nullptr) {
        if (active->id == kTestPatternId) {
            notrix::demo::drawTestPattern(canvas, static_cast<int>(state.nowMillis / 33u));
        } else {
            // Re-parse only when the active app or the registry changes, not
            // every frame.
            if (state.parsedAppId != active->id ||
                state.parsedRevision != state.registry.revision()) {
                state.sceneReady = state.scene.load(active->sceneJson);
                state.parsedAppId = active->id;
                state.parsedRevision = state.registry.revision();
            }
            if (state.sceneReady) {
                state.scene.render(canvas);
            }
        }
    }

    if (state.brightness != 255) {
        notrix::Rgb* pixels = state.framebuffer.data();
        for (int i = 0; i < Framebuffer::kPixelCount; ++i) {
            pixels[i] = notrix::scale(pixels[i], state.brightness);
        }
    }

    // Out through the platform boundary, exactly as the device will.
    state.platform.display().setBrightness(state.brightness);
    state.platform.display().present(state.framebuffer);
}

/// Feed one raw hardware event. `source` and `phase` match the RawInput and
/// ButtonPhase enums; the core decides what the press means.
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
    state.nowMillis = nowMillis < 0 ? 0u : static_cast<std::uint64_t>(nowMillis);

    InputEvent event;
    event.source = static_cast<RawInput>(source);
    event.phase = static_cast<ButtonPhase>(phase);
    event.timestampMillis = state.nowMillis;

    // Queue it on the simulated device too, so the path the real firmware takes
    // (poll the platform, map, act) is the one being exercised.
    state.platform.simulatedInput().push(event);

    InputEvent polled;
    while (state.platform.input().poll(polled)) {
        notrix::input::ActionEvent action;
        if (state.mapper.handle(polled, action)) {
            applyAction(state, action);
        }
    }
}

EMSCRIPTEN_KEEPALIVE const unsigned char* notrix_framebuffer() {
    return emulator().platform.simulatedDisplay().lastFrame().bytes();
}

EMSCRIPTEN_KEEPALIVE int notrix_width() {
    return Framebuffer::kWidth;
}

EMSCRIPTEN_KEEPALIVE int notrix_height() {
    return Framebuffer::kHeight;
}

EMSCRIPTEN_KEEPALIVE int notrix_app_count() {
    return emulator().registry.count();
}

EMSCRIPTEN_KEEPALIVE const char* notrix_active_name() {
    const App* active = emulator().carousel.active();
    return active != nullptr ? active->name.c_str() : "";
}

EMSCRIPTEN_KEEPALIVE int notrix_active_index() {
    Emulator& state = emulator();
    return state.registry.indexOf(state.carousel.activeId());
}

EMSCRIPTEN_KEEPALIVE int notrix_active_duration_seconds() {
    return emulator().carousel.activeDurationSeconds();
}

EMSCRIPTEN_KEEPALIVE int notrix_dwell_millis(int nowMillis) {
    const std::uint64_t now = nowMillis < 0 ? 0u : static_cast<std::uint64_t>(nowMillis);
    return static_cast<int>(emulator().carousel.dwellMillis(now));
}

EMSCRIPTEN_KEEPALIVE int notrix_is_paused() {
    return emulator().carousel.paused() ? 1 : 0;
}

}  // extern "C"
