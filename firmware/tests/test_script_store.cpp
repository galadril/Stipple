// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/script/ScriptStore.h"

#include <string>

#include "stipple/app/AppRegistry.h"
#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/host/ApplicationHost.h"
#include "stipple/platform/simulator/SimulatorPlatform.h"
#include "stipple/script/ScriptHost.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::host::ApplicationHost;
using stipple::host::HostConfig;
using stipple::platform::simulator::SimulatorPlatform;
using stipple::script::ScriptStore;
namespace colors = stipple::colors;

namespace {

const char* kRedPixel =
    "class App\n"
    "  def draw()\n"
    "    pixel(0, 0, rgb(255, 0, 0))\n"
    "  end\n"
    "end\n"
    "return App()\n";

int countLit(const Framebuffer& framebuffer) {
    int lit = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (framebuffer.at(x, y) != colors::kBlack) { ++lit; }
        }
    }
    return lit;
}

HostConfig quietConfig() {
    HostConfig config;
    config.splashMillis = 0;
    return config;
}

}  // namespace

// --- the store ---------------------------------------------------------------

STIPPLE_TEST(ScriptStore, StoresAndDraws) {
    ScriptStore store;
    STIPPLE_CHECK(store.put("hello", "Hello", kRedPixel) == ScriptStore::PutResult::Added);
    STIPPLE_CHECK_EQ(store.count(), 1);

    const stipple::script::Script* script = store.find("hello");
    STIPPLE_REQUIRE(script != nullptr);
    STIPPLE_CHECK(script->ok);
    STIPPLE_CHECK(script->problem.empty());

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(store.draw("hello", canvas, 0));
    STIPPLE_CHECK(framebuffer.at(0, 0) == stipple::rgb(255, 0, 0));
}

STIPPLE_TEST(ScriptStore, ABrokenScriptIsKeptSoItCanBeFixed) {
    // Deleting somebody's work because they left out an `end` would be the
    // worst possible response to a typo. The source survives, it just does
    // not run, and the reason is attached to it.
    ScriptStore store;
    const auto result = store.put("broken", "Broken", "class App\n  def draw()\n");

    STIPPLE_CHECK(result == ScriptStore::PutResult::DidNotCompile);
    const stipple::script::Script* script = store.find("broken");
    STIPPLE_REQUIRE(script != nullptr);
    STIPPLE_CHECK(!script->ok);
    STIPPLE_CHECK(!script->problem.empty());
    STIPPLE_CHECK(script->source == "class App\n  def draw()\n");

    // And fixing it in place works, without having to delete and re-add.
    STIPPLE_CHECK(store.put("broken", "Broken", kRedPixel) == ScriptStore::PutResult::Replaced);
    STIPPLE_REQUIRE(store.find("broken") != nullptr);
    STIPPLE_CHECK(store.find("broken")->ok);
    STIPPLE_CHECK(store.find("broken")->problem.empty());
}

STIPPLE_TEST(ScriptStore, ReplacingKeepsItsPlace) {
    // Saving an edit must not shuffle the carousel under the person who made
    // it - they are watching the panel while they type.
    ScriptStore store;
    store.put("one", "One", kRedPixel);
    store.put("two", "Two", kRedPixel);
    store.put("three", "Three", kRedPixel);

    store.put("one", "One, edited", kRedPixel);

    STIPPLE_CHECK_EQ(store.count(), 3);
    STIPPLE_REQUIRE(store.at(0) != nullptr);
    STIPPLE_CHECK(store.at(0)->id == "one");
    STIPPLE_CHECK(store.at(0)->name == "One, edited");
    STIPPLE_CHECK(store.at(2)->id == "three");
}

STIPPLE_TEST(ScriptStore, AnEditGetsAFreshInterpreter) {
    // Reusing one would leave the previous script's state behind, so the new
    // source would run against variables it never created - and would appear
    // to work right up until the device restarted.
    ScriptStore store;
    STIPPLE_REQUIRE(store.put("s", "S",
                              "leftover = 42\n"
                              "class App\n  def draw()\n  end\nend\n"
                              "return App()\n") == ScriptStore::PutResult::Added);

    // The replacement reads a global the first script set. A reused
    // interpreter would still have it, and this would compile and run.
    //
    // It does not even get as far as running: Berry resolves globals when it
    // compiles, so a fresh interpreter rejects the name outright. That is a
    // stronger answer than this test was written to look for.
    STIPPLE_CHECK(store.put("s", "S",
                            "class App\n"
                            "  def draw()\n"
                            "    pixel(leftover, 0, rgb(1, 2, 3))\n"
                            "  end\n"
                            "end\n"
                            "return App()\n") == ScriptStore::PutResult::DidNotCompile);

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(!store.draw("s", canvas, 0));
    STIPPLE_REQUIRE(store.find("s") != nullptr);
    STIPPLE_CHECK(store.find("s")->problem.find("leftover") != std::string::npos);
}

STIPPLE_TEST(ScriptStore, IdsAreRestricted) {
    // A script id becomes part of an API path and a config key. The set of
    // characters safe in both is smaller than the set that looks harmless.
    ScriptStore store;
    for (const char* bad : {"", "Capitals", "with space", "dots.here", "slash/es",
                            "..", "semi;colon", "quote'", "percent%20"}) {
        STIPPLE_CHECK(store.put(bad, "x", kRedPixel) == ScriptStore::PutResult::InvalidId);
    }
    for (const char* good : {"a", "my-script", "my_script", "flappy2"}) {
        STIPPLE_CHECK(store.put(good, "x", kRedPixel) != ScriptStore::PutResult::InvalidId);
    }
}

STIPPLE_TEST(ScriptStore, IsBounded) {
    ScriptStore store;
    for (int i = 0; i < ScriptStore::kMaxScripts; ++i) {
        const std::string id = "s" + std::to_string(i);
        STIPPLE_REQUIRE(store.put(id, id, kRedPixel) == ScriptStore::PutResult::Added);
    }
    STIPPLE_CHECK(store.put("one-too-many", "x", kRedPixel) ==
                  ScriptStore::PutResult::TooManyScripts);

    // But replacing an existing one still works when full - otherwise a full
    // device would be one you could not fix a typo on.
    STIPPLE_CHECK(store.put("s0", "s0", kRedPixel) == ScriptStore::PutResult::Replaced);

    const std::string huge(stipple::script::ScriptHost::kMaxSourceBytes + 1, ' ');
    STIPPLE_CHECK(store.put("huge", "x", huge) == ScriptStore::PutResult::SourceTooLarge);
}

STIPPLE_TEST(ScriptStore, RemovingWorksAndFreesTheInterpreter) {
    ScriptStore store;
    store.put("gone", "Gone", kRedPixel);
    const std::size_t withOne = store.memoryBytes();
    STIPPLE_CHECK(withOne > 0);

    STIPPLE_CHECK(store.remove("gone"));
    STIPPLE_CHECK(!store.remove("gone"));
    STIPPLE_CHECK(store.empty());
    STIPPLE_CHECK_EQ(store.memoryBytes(), std::size_t{0});

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(!store.draw("gone", canvas, 0));
}

// --- on the panel ------------------------------------------------------------

STIPPLE_TEST(ScriptStore, AScriptAppRendersThroughTheHost) {
    // The whole chain: an app in the carousel, a script in the store, and
    // pixels on the panel.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());

    ScriptStore store;
    STIPPLE_REQUIRE(store.put("green", "Green",
                              "class App\n"
                              "  def draw()\n"
                              "    rect_fill(0, 0, width(), height(), rgb(0, 255, 0))\n"
                              "  end\n"
                              "end\n"
                              "return App()\n") == ScriptStore::PutResult::Added);
    host.setScriptRunner(&store);

    stipple::app::App entry;
    entry.id = "green";
    entry.name = "Green";
    entry.builtin = stipple::app::Builtin::Script;
    STIPPLE_REQUIRE(host.apps().put(entry) == stipple::app::AppRegistry::PutResult::Added);
    STIPPLE_REQUIRE(host.carousel().pin("green", 1000));

    host.tick(1000);

    const Framebuffer& panel = platform.simulatedDisplay().lastFrame();
    STIPPLE_CHECK_EQ(countLit(panel), Framebuffer::kWidth * Framebuffer::kHeight);
    // Green, at whatever the brightness setting scales it to. Asserting the
    // exact value would be asserting the brightness, which is a different
    // test and one that would break the moment the default changed.
    const stipple::Rgb sample = panel.at(10, 8);
    STIPPLE_CHECK_EQ(static_cast<int>(sample.r), 0);
    STIPPLE_CHECK_EQ(static_cast<int>(sample.b), 0);
    STIPPLE_CHECK(sample.g > 0);
}

STIPPLE_TEST(ScriptStore, AHostWithNoScriptingSaysSoRatherThanGoingBlank) {
    // ADR 0013. A black panel is indistinguishable from a working app that
    // drew nothing, from a crashed device and from a dead row of LEDs, and
    // the person in front of it cannot tell which.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    STIPPLE_CHECK(host.scriptRunner() == nullptr);

    stipple::app::App entry;
    entry.id = "orphan";
    entry.name = "Orphan";
    entry.builtin = stipple::app::Builtin::Script;
    STIPPLE_REQUIRE(host.apps().put(entry) == stipple::app::AppRegistry::PutResult::Added);
    STIPPLE_REQUIRE(host.carousel().pin("orphan", 1000));

    host.tick(1000);

    STIPPLE_CHECK(countLit(platform.simulatedDisplay().lastFrame()) > 0);
}

STIPPLE_TEST(ScriptStore, ABrokenScriptSaysSoOnThePanelToo) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());

    ScriptStore store;
    store.put("bad", "Bad",
              "class App\n"
              "  def draw()\n"
              "    raise 'nope'\n"
              "  end\n"
              "end\n"
              "return App()\n");
    host.setScriptRunner(&store);

    stipple::app::App entry;
    entry.id = "bad";
    entry.name = "Bad";
    entry.builtin = stipple::app::Builtin::Script;
    host.apps().put(entry);
    STIPPLE_REQUIRE(host.carousel().pin("bad", 1000));

    host.tick(1000);
    STIPPLE_CHECK(countLit(platform.simulatedDisplay().lastFrame()) > 0);

    // And the detail the panel could not fit is still available to whoever
    // can actually fix it.
    STIPPLE_REQUIRE(store.find("bad") != nullptr);
    STIPPLE_CHECK(!store.find("bad")->problem.empty());
}

STIPPLE_TEST(ScriptStore, ARunawayScriptDoesNotStopTheDevice) {
    // The failure this whole design exists to survive. A script that loops for
    // ever must cost its own app and nothing else - the panel keeps drawing,
    // the carousel keeps moving, and the API keeps answering.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());

    ScriptStore store;
    store.put("spin", "Spin",
              "class App\n"
              "  def draw()\n"
              "    while true\n"
              "    end\n"
              "  end\n"
              "end\n"
              "return App()\n");
    host.setScriptRunner(&store);

    stipple::app::App entry;
    entry.id = "spin";
    entry.name = "Spin";
    entry.builtin = stipple::app::Builtin::Script;
    host.apps().put(entry);
    STIPPLE_REQUIRE(host.carousel().pin("spin", 1000));

    // If the budget did not work this would never return.
    for (int frame = 0; frame < 30; ++frame) {
        host.tick(1000 + static_cast<std::uint64_t>(frame) * 33u);
    }

    STIPPLE_REQUIRE(store.find("spin") != nullptr);
    STIPPLE_CHECK(!store.find("spin")->ok);

    // The clock still works, which is the part that matters.
    STIPPLE_REQUIRE(host.carousel().pin("clock", 2000));
    host.tick(3000);
    STIPPLE_CHECK(countLit(platform.simulatedDisplay().lastFrame()) > 0);
}
