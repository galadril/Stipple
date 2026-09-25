// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/script/ScriptStore.h"

#include <string>

#include "stipple/app/AppRegistry.h"
#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/host/ApplicationHost.h"
#include "stipple/platform/simulator/SimulatorPlatform.h"
#include "stipple/api/Http.h"
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
    STIPPLE_CHECK(store.put("hello", "Hello", kRedPixel) == stipple::script::ScriptPutResult::Added);
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

    STIPPLE_CHECK(result == stipple::script::ScriptPutResult::DidNotCompile);
    const stipple::script::Script* script = store.find("broken");
    STIPPLE_REQUIRE(script != nullptr);
    STIPPLE_CHECK(!script->ok);
    STIPPLE_CHECK(!script->problem.empty());
    STIPPLE_CHECK(script->source == "class App\n  def draw()\n");

    // And fixing it in place works, without having to delete and re-add.
    STIPPLE_CHECK(store.put("broken", "Broken", kRedPixel) == stipple::script::ScriptPutResult::Replaced);
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
                              "return App()\n") == stipple::script::ScriptPutResult::Added);

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
                            "return App()\n") == stipple::script::ScriptPutResult::DidNotCompile);

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
        STIPPLE_CHECK(store.put(bad, "x", kRedPixel) == stipple::script::ScriptPutResult::InvalidId);
    }
    for (const char* good : {"a", "my-script", "my_script", "flappy2"}) {
        STIPPLE_CHECK(store.put(good, "x", kRedPixel) != stipple::script::ScriptPutResult::InvalidId);
    }
}

STIPPLE_TEST(ScriptStore, IsBounded) {
    ScriptStore store;
    for (int i = 0; i < ScriptStore::kMaxScripts; ++i) {
        const std::string id = "s" + std::to_string(i);
        STIPPLE_REQUIRE(store.put(id, id, kRedPixel) == stipple::script::ScriptPutResult::Added);
    }
    STIPPLE_CHECK(store.put("one-too-many", "x", kRedPixel) ==
                  stipple::script::ScriptPutResult::TooManyScripts);

    // But replacing an existing one still works when full - otherwise a full
    // device would be one you could not fix a typo on.
    STIPPLE_CHECK(store.put("s0", "s0", kRedPixel) == stipple::script::ScriptPutResult::Replaced);

    const std::string huge(stipple::script::ScriptHost::kMaxSourceBytes + 1, ' ');
    STIPPLE_CHECK(store.put("huge", "x", huge) == stipple::script::ScriptPutResult::SourceTooLarge);
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
                              "return App()\n") == stipple::script::ScriptPutResult::Added);
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

// --- over the API ------------------------------------------------------------

namespace {

stipple::api::Response call(ApplicationHost& host, stipple::api::Method method,
                            const std::string& path, const std::string& body = "") {
    stipple::api::Request request;
    request.method = method;
    request.path = path;
    request.body = body;
    return host.handle(request);
}

bool bodyHas(const stipple::api::Response& response, const std::string& needle) {
    return response.body.find(needle) != std::string::npos;
}

}  // namespace

STIPPLE_TEST(ScriptApi, WritesReadsAndDeletes) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    ScriptStore store;
    host.setScriptRunner(&store);

    auto response = call(host, stipple::api::Method::Post, "/api/v1/scripts",
                         R"({"id":"hello","name":"Hello",)"
                         R"("source":"class App\n def draw()\n  pixel(0,0,rgb(1,2,3))\n end\nend\nreturn App()\n"})");
    STIPPLE_CHECK_EQ(response.status, 201);
    STIPPLE_CHECK(bodyHas(response, "\"ok\":true"));

    // The collection does not carry source. Sixteen scripts at 16 KB each
    // would make a list request answer with a quarter of a megabyte, and the
    // web UI asks for the list every time the panel is opened.
    response = call(host, stipple::api::Method::Get, "/api/v1/scripts");
    STIPPLE_CHECK_EQ(response.status, 200);
    STIPPLE_CHECK(bodyHas(response, "\"id\":\"hello\""));
    STIPPLE_CHECK(bodyHas(response, "\"capacity\""));
    STIPPLE_CHECK(bodyHas(response, "\"maxSourceBytes\""));
    STIPPLE_CHECK(!bodyHas(response, "\"source\""));

    // The item does.
    response = call(host, stipple::api::Method::Get, "/api/v1/scripts/hello");
    STIPPLE_CHECK_EQ(response.status, 200);
    STIPPLE_CHECK(bodyHas(response, "\"source\""));
    STIPPLE_CHECK(bodyHas(response, "rgb(1,2,3)"));

    response = call(host, stipple::api::Method::Delete, "/api/v1/scripts/hello");
    STIPPLE_CHECK_EQ(response.status, 204);
    STIPPLE_CHECK_EQ(call(host, stipple::api::Method::Get, "/api/v1/scripts/hello").status, 404);
}

STIPPLE_TEST(ScriptApi, SavingSomethingBrokenSucceedsAndSaysWhy) {
    // An editor holds work in progress. A device that refuses to store code
    // until it compiles is a device you cannot edit on - you would lose the
    // half-finished function every time you saved.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    ScriptStore store;
    host.setScriptRunner(&store);

    const auto response = call(host, stipple::api::Method::Post, "/api/v1/scripts",
                               R"({"id":"wip","source":"class App\n def draw()\n"})");
    STIPPLE_CHECK_EQ(response.status, 201);
    STIPPLE_CHECK(bodyHas(response, "\"ok\":false"));
    STIPPLE_CHECK(bodyHas(response, "\"problem\""));
    STIPPLE_CHECK(!bodyHas(response, "\"problem\":\"\""));

    // And it really is stored, with the text intact.
    const auto read = call(host, stipple::api::Method::Get, "/api/v1/scripts/wip");
    STIPPLE_CHECK_EQ(read.status, 200);
    STIPPLE_CHECK(bodyHas(read, "def draw()"));
}

STIPPLE_TEST(ScriptApi, RejectsBadIdsAndOversizedSource) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    ScriptStore store;
    host.setScriptRunner(&store);

    STIPPLE_CHECK_EQ(call(host, stipple::api::Method::Post, "/api/v1/scripts",
                          R"({"id":"Not Valid","source":"return nil\n"})").status, 422);

    // A missing source is not an empty one.
    STIPPLE_CHECK_EQ(call(host, stipple::api::Method::Post, "/api/v1/scripts",
                          R"({"id":"nosource"})").status, 422);
}

STIPPLE_TEST(ScriptApi, ADeviceWithoutScriptingSaysSoRatherThanShowingNone) {
    // "you have no scripts" and "this device cannot run scripts" are very
    // different answers to somebody whose script is not showing up, and an
    // empty list would give the wrong one. ADR 0013.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    // No runner installed.

    const auto response = call(host, stipple::api::Method::Get, "/api/v1/scripts");
    STIPPLE_CHECK_EQ(response.status, 501);
    STIPPLE_CHECK(bodyHas(response, "without scripting"));
    STIPPLE_CHECK(!bodyHas(response, "\"scripts\":[]"));
}

STIPPLE_TEST(ScriptApi, TheLibraryIsBoundedOverTheApiToo) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    ScriptStore store;
    host.setScriptRunner(&store);

    for (int i = 0; i < ScriptStore::kMaxScripts; ++i) {
        const std::string body = R"({"id":"s)" + std::to_string(i) +
                                 R"(","source":"class App\n def draw()\n end\nend\nreturn App()\n"})";
        STIPPLE_REQUIRE(call(host, stipple::api::Method::Post, "/api/v1/scripts", body).status == 201);
    }
    const auto full = call(host, stipple::api::Method::Post, "/api/v1/scripts",
                           R"({"id":"overflow","source":"return nil\n"})");
    STIPPLE_CHECK_EQ(full.status, 409);
}

// --- surviving a reboot ------------------------------------------------------

STIPPLE_TEST(ScriptStore, RoundTripsThroughABlob) {
    ScriptStore before;
    before.put("one", "First", kRedPixel);
    before.put("two", "Second",
               "class App\n  def draw()\n    clear(rgb(9, 9, 9))\n  end\nend\nreturn App()\n");
    // Source with the characters a delimited format would choke on. The blob
    // is length-prefixed precisely so a script can contain anything.
    before.put("awkward", "Awkward \"quoted\"",
               "class App\n  def draw()\n    var s = 'a\nb\\\"c'\n  end\nend\nreturn App()\n");

    const std::string blob = before.serialize();

    ScriptStore after;
    STIPPLE_REQUIRE(after.deserialize(blob));
    STIPPLE_CHECK_EQ(after.count(), 3);

    for (int i = 0; i < before.count(); ++i) {
        STIPPLE_REQUIRE(after.at(i) != nullptr);
        STIPPLE_CHECK(after.at(i)->id == before.at(i)->id);
        STIPPLE_CHECK(after.at(i)->name == before.at(i)->name);
        STIPPLE_CHECK(after.at(i)->source == before.at(i)->source);
    }

    // And it runs, which is the part that actually matters - a round trip that
    // preserves the text but not the ability to execute it would pass every
    // comparison above and still leave a dead panel.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(after.draw("one", canvas, 0));
    STIPPLE_CHECK(framebuffer.at(0, 0) == stipple::rgb(255, 0, 0));
}

STIPPLE_TEST(ScriptStore, EveryTruncationOfAGoodBlobIsRefusedRatherThanRead) {
    // The failure storage actually produces. A write interrupted by a power
    // cut leaves a prefix of the blob, not random bytes, and reading past the
    // end of one is how a config file takes a device down.
    ScriptStore source;
    source.put("a", "A", kRedPixel);
    source.put("b", "B", kRedPixel);
    const std::string blob = source.serialize();

    for (std::size_t length = 0; length < blob.size(); ++length) {
        ScriptStore store;
        STIPPLE_CHECK(!store.deserialize(std::string_view(blob).substr(0, length)));
    }
    ScriptStore whole;
    STIPPLE_CHECK(whole.deserialize(blob));
}

STIPPLE_TEST(ScriptStore, RubbishIsRefused) {
    ScriptStore store;
    STIPPLE_CHECK(!store.deserialize(""));
    STIPPLE_CHECK(!store.deserialize("not a blob at all"));
    // A version this build does not know. Deliberately far ahead rather than
    // one step off: the point is that an unrecognised format is refused, and
    // a number adjacent to the current one turns into a passing test the day
    // somebody bumps the format.
    STIPPLE_CHECK(!store.deserialize(std::string("SBS") + '\x63' + '\x00'));

    // A good blob with a byte glued on the end is not the blob it claims to be.
    ScriptStore source;
    source.put("a", "A", kRedPixel);
    STIPPLE_CHECK(!store.deserialize(source.serialize() + "x"));
}

STIPPLE_TEST(ScriptStore, RevisionOnlyMovesWhenSomethingChanges) {
    // The host writes to flash when this moves, and flash wears out.
    ScriptStore store;
    STIPPLE_CHECK_EQ(store.revision(), std::uint32_t{0});

    store.put("a", "A", kRedPixel);
    const std::uint32_t afterAdd = store.revision();
    STIPPLE_CHECK(afterAdd > 0);

    // Reading changes nothing.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    store.draw("a", canvas, 0);
    store.find("a");
    store.count();
    STIPPLE_CHECK_EQ(store.revision(), afterAdd);

    // Removing something that is not there changes nothing either.
    STIPPLE_CHECK(!store.remove("absent"));
    STIPPLE_CHECK_EQ(store.revision(), afterAdd);

    store.clear();
    const std::uint32_t afterClear = store.revision();
    STIPPLE_CHECK(afterClear > afterAdd);
    store.clear();  // already empty
    STIPPLE_CHECK_EQ(store.revision(), afterClear);
}

STIPPLE_TEST(ScriptStore, ScriptsSurviveARestart) {
    SimulatorPlatform platform;

    {
        ApplicationHost host(platform, quietConfig());
        STIPPLE_REQUIRE(host.initialize());
        ScriptStore store;
        host.setScriptRunner(&store);
        STIPPLE_REQUIRE(call(host, stipple::api::Method::Post, "/api/v1/scripts",
                             R"({"id":"kept","name":"Kept",)"
                             R"("source":"class App\n def draw()\n  pixel(2,2,rgb(7,7,7))\n end\nend\nreturn App()\n"})")
                            .status == 201);
        host.tick(1000);  // persistence happens on the tick, not the write
    }

    // Same storage, new everything else.
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    ScriptStore store;
    host.setScriptRunner(&store);

    STIPPLE_REQUIRE(store.find("kept") != nullptr);
    STIPPLE_CHECK(store.find("kept")->name == "Kept");
    STIPPLE_CHECK(store.find("kept")->ok);

    const auto listed = call(host, stipple::api::Method::Get, "/api/v1/scripts");
    STIPPLE_CHECK(bodyHas(listed, "\"id\":\"kept\""));
}

STIPPLE_TEST(ScriptStore, CorruptStoredScriptsDoNotStopTheDeviceStarting) {
    SimulatorPlatform platform;
    STIPPLE_REQUIRE(platform.storage().write("scripts", "this is not a script blob"));

    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    ScriptStore store;
    host.setScriptRunner(&store);

    STIPPLE_CHECK(store.empty());

    // And the bad blob is gone, so the next boot is not the same failure
    // again - one bad write should not look like an intermittent fault.
    std::string leftover;
    STIPPLE_CHECK(!platform.storage().read("scripts", leftover));

    // And the device is up and drawing, which is the whole point - a bad
    // blob on storage must cost the scripts and nothing else.
    STIPPLE_CHECK(host.bootMode() == stipple::host::BootMode::Normal);
    STIPPLE_REQUIRE(host.carousel().pin("clock", 1000));
    for (int frame = 0; frame < 5; ++frame) {
        host.tick(1000 + static_cast<std::uint64_t>(frame) * 100u);
    }
    STIPPLE_CHECK(countLit(platform.simulatedDisplay().lastFrame()) > 0);
}

// --- scripts and the carousel ------------------------------------------------

STIPPLE_TEST(ScriptApi, WritingAScriptPutsItInTheCarousel) {
    // Two calls that must always be made together are better made as one. A
    // client that did not know the convention would leave its author with a
    // saved script that never appears on the panel and nothing saying why.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    ScriptStore store;
    host.setScriptRunner(&store);

    STIPPLE_REQUIRE(call(host, stipple::api::Method::Post, "/api/v1/scripts",
                         R"({"id":"clockface","name":"Clock face",)"
                         R"("source":"class App\n def draw()\n  pixel(1,1,rgb(4,5,6))\n end\nend\nreturn App()\n"})")
                        .status == 201);

    const stipple::app::App* entry = host.apps().find("clockface");
    STIPPLE_REQUIRE(entry != nullptr);
    STIPPLE_CHECK(entry->builtin == stipple::app::Builtin::Script);
    STIPPLE_CHECK(entry->name == "Clock face");
    STIPPLE_CHECK(entry->enabled);

    // And it draws.
    STIPPLE_REQUIRE(host.carousel().pin("clockface", 1000));
    host.tick(1000);
    STIPPLE_CHECK(countLit(platform.simulatedDisplay().lastFrame()) > 0);

    // Deleting takes the app with it. Leaving one behind would show SCRIPT ?
    // for ever, which is honest but is not what anybody deleting a script
    // meant to happen.
    STIPPLE_CHECK_EQ(call(host, stipple::api::Method::Delete, "/api/v1/scripts/clockface").status,
                     204);
    STIPPLE_CHECK(host.apps().find("clockface") == nullptr);
}

STIPPLE_TEST(ScriptApi, EditingAScriptLeavesTheCarouselArrangementAlone) {
    // Somebody who has turned a script off, moved it to the end and set it to
    // twelve seconds has said something. Saving a typo fix must not undo it.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    ScriptStore store;
    host.setScriptRunner(&store);

    const std::string body =
        R"({"id":"s","name":"S","source":"class App\n def draw()\n end\nend\nreturn App()\n"})";
    STIPPLE_REQUIRE(call(host, stipple::api::Method::Post, "/api/v1/scripts", body).status == 201);

    STIPPLE_REQUIRE(host.apps().setEnabled("s", false));
    STIPPLE_REQUIRE(host.apps().move("s", 0));
    stipple::app::App* entry = host.apps().find("s");
    STIPPLE_REQUIRE(entry != nullptr);
    entry->durationSeconds = 12;

    // Save again, with different source and a different name.
    STIPPLE_REQUIRE(call(host, stipple::api::Method::Post, "/api/v1/scripts",
                         R"({"id":"s","name":"Renamed",)"
                         R"("source":"class App\n def draw()\n  pixel(0,0,rgb(1,1,1))\n end\nend\nreturn App()\n"})")
                        .status == 200);

    const stipple::app::App* after = host.apps().find("s");
    STIPPLE_REQUIRE(after != nullptr);
    STIPPLE_CHECK_EQ(host.apps().indexOf("s"), 0);
    STIPPLE_CHECK(!after->enabled);
    STIPPLE_CHECK_EQ(after->durationSeconds, 12);

    // The source really did change, so the test is not passing because the
    // write did nothing.
    STIPPLE_REQUIRE(store.find("s") != nullptr);
    STIPPLE_CHECK(store.find("s")->source.find("pixel(0,0,rgb(1,1,1))") != std::string::npos);
}

// --- buttons -----------------------------------------------------------------

STIPPLE_TEST(ScriptStore, AScriptWithOnButtonGetsThePress) {
    ScriptStore store;
    STIPPLE_REQUIRE(store.put("game", "Game",
                              "class App\n"
                              "  var presses\n"
                              "  def init()\n"
                              "    self.presses = 0\n"
                              "  end\n"
                              "  def on_button(name)\n"
                              "    self.presses += 1\n"
                              "  end\n"
                              "  def draw()\n"
                              "    pixel(self.presses, 0, rgb(255, 255, 255))\n"
                              "  end\n"
                              "end\n"
                              "return App()\n") == stipple::script::ScriptPutResult::Added);

    STIPPLE_CHECK(store.button("game", "action"));
    STIPPLE_CHECK(store.button("game", "action"));

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(store.draw("game", canvas, 0));
    STIPPLE_CHECK(framebuffer.at(2, 0) != colors::kBlack);
}

STIPPLE_TEST(ScriptStore, AScriptWithoutOnButtonDoesNotSwallowThePress) {
    // A script that silently ate the only button would be an app you could
    // not pause, and would look like a device that had stopped responding.
    ScriptStore store;
    store.put("plain", "Plain", kRedPixel);
    STIPPLE_CHECK(!store.button("plain", "action"));
    STIPPLE_CHECK(!store.button("absent", "action"));
}

STIPPLE_TEST(ScriptStore, AButtonHandlerThatLoopsForEverIsStopped) {
    // A handler that spins is exactly as bad as a draw() that does, and it
    // arrives by a different route - so it needs its own budget, not an
    // assumption that draw() covers it.
    ScriptStore store;
    store.put("hostile", "Hostile",
              "class App\n"
              "  def on_button(name)\n"
              "    while true\n"
              "    end\n"
              "  end\n"
              "  def draw()\n"
              "  end\n"
              "end\n"
              "return App()\n");

    // Returns at all, which is the assertion.
    STIPPLE_CHECK(!store.button("hostile", "action"));
    STIPPLE_REQUIRE(store.find("hostile") != nullptr);
    STIPPLE_CHECK(!store.find("hostile")->ok);
    STIPPLE_CHECK(!store.find("hostile")->problem.empty());
}

STIPPLE_TEST(ScriptStore, ThePressReachesAScriptThroughTheWholeDevice) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());

    ScriptStore store;
    host.setScriptRunner(&store);
    STIPPLE_REQUIRE(call(host, stipple::api::Method::Post, "/api/v1/scripts",
                         R"({"id":"tally","name":"Tally",)"
                         R"("source":"class App\n var n\n def init()\n  self.n = 0\n end\n)"
                         R"( def on_button(name)\n  self.n += 1\n end\n)"
                         R"( def draw()\n  rect_fill(0, 0, self.n, 16, rgb(0, 255, 0))\n end\nend\nreturn App()\n"})")
                        .status == 201);

    STIPPLE_REQUIRE(host.carousel().pin("tally", 1000));
    host.tick(1000);
    const int before = countLit(platform.simulatedDisplay().lastFrame());

    // Through the real input path - a short knob press, which is what the
    // action button is on this hardware - rather than by calling the store.
    // A long press is the way into settings, so these are deliberately brief.
    platform.simulatedInput().pressAndRelease(
        stipple::platform::RawInput::RotaryPress, 1050, 80);
    host.tick(1200);
    platform.simulatedInput().pressAndRelease(
        stipple::platform::RawInput::RotaryPress, 1250, 80);
    host.tick(1400);

    STIPPLE_CHECK(countLit(platform.simulatedDisplay().lastFrame()) > before);

    // And the carousel was not paused by presses the script took.
    STIPPLE_CHECK(!host.carousel().paused());
}

// --- what a script can see ---------------------------------------------------

STIPPLE_TEST(ScriptStore, ScriptsSeeTheClockAndTheBattery) {
    ScriptStore store;
    stipple::script::ScriptEnvironment environment;
    environment.timeKnown = true;
    environment.hour = 13;
    environment.minute = 45;
    environment.second = 7;
    environment.weekday = 3;
    environment.batteryKnown = true;
    environment.batteryPercent = 62;
    store.setEnvironment(environment);

    STIPPLE_REQUIRE(store.put("probe", "Probe",
                              "class App\n"
                              "  def draw()\n"
                              "    pixel(hour(), 0, rgb(1, 1, 1))\n"
                              "    pixel(minute(), 1, rgb(1, 1, 1))\n"
                              "    pixel(second(), 2, rgb(1, 1, 1))\n"
                              "    pixel(weekday(), 3, rgb(1, 1, 1))\n"
                              "    if time_known()\n"
                              "      pixel(50, 4, rgb(1, 1, 1))\n"
                              "    end\n"
                              "    if battery_known()\n"
                              "      pixel(battery() / 2, 5, rgb(1, 1, 1))\n"
                              "    end\n"
                              "  end\n"
                              "end\n"
                              "return App()\n") == stipple::script::ScriptPutResult::Added);

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_REQUIRE(store.draw("probe", canvas, 0));

    STIPPLE_CHECK(framebuffer.at(13, 0) != colors::kBlack);
    STIPPLE_CHECK(framebuffer.at(45, 1) != colors::kBlack);
    STIPPLE_CHECK(framebuffer.at(7, 2) != colors::kBlack);
    STIPPLE_CHECK(framebuffer.at(3, 3) != colors::kBlack);
    STIPPLE_CHECK(framebuffer.at(50, 4) != colors::kBlack);
    STIPPLE_CHECK(framebuffer.at(31, 5) != colors::kBlack);
}

STIPPLE_TEST(ScriptStore, AbsentCapabilitiesAreVisibleNotPlausible) {
    // The whole reason each value has a companion flag. A device with no
    // battery reporting 0% and a device with a flat battery look identical to
    // a script, and one of those is a lie. ADR 0013.
    ScriptStore store;
    // Default environment: nothing is known.
    STIPPLE_REQUIRE(store.put("honest", "Honest",
                              "class App\n"
                              "  def draw()\n"
                              "    if !time_known()\n"
                              "      text(0, 0, 'no time', rgb(255, 0, 0))\n"
                              "    end\n"
                              "    if !battery_known()\n"
                              "      text(0, 8, 'no batt', rgb(255, 0, 0))\n"
                              "    end\n"
                              "  end\n"
                              "end\n"
                              "return App()\n") == stipple::script::ScriptPutResult::Added);

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_REQUIRE(store.draw("honest", canvas, 0));
    STIPPLE_CHECK(countLit(framebuffer) > 0);
}

STIPPLE_TEST(ScriptStore, AScriptSavedBetweenFramesDoesNotSee1970) {
    // The environment is held by the store as well as pushed to each host, so
    // a script created after the last frame starts with a current clock.
    ScriptStore store;
    stipple::script::ScriptEnvironment environment;
    environment.timeKnown = true;
    environment.hour = 9;
    store.setEnvironment(environment);

    store.put("late", "Late",
              "class App\n"
              "  def draw()\n"
              "    pixel(hour(), 0, rgb(1, 1, 1))\n"
              "  end\n"
              "end\n"
              "return App()\n");

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_REQUIRE(store.draw("late", canvas, 0));
    STIPPLE_CHECK(framebuffer.at(9, 0) != colors::kBlack);
    STIPPLE_CHECK(framebuffer.at(0, 0) == colors::kBlack);
}

STIPPLE_TEST(ScriptStore, TheHostPublishesTheEnvironmentEveryFrame) {
    SimulatorPlatform platform;
    // 2024-03-14 15:09:26 UTC, a Thursday.
    platform.simulatedClock().setWallClock(1710428966);

    ApplicationHost host(platform, quietConfig());
    STIPPLE_REQUIRE(host.initialize());
    ScriptStore store;
    host.setScriptRunner(&store);

    STIPPLE_REQUIRE(call(host, stipple::api::Method::Post, "/api/v1/scripts",
                         R"({"id":"seen","source":"class App\n def draw()\n)"
                         R"(  if time_known()\n   pixel(hour(), 0, rgb(0,255,0))\n  end\n)"
                         R"( end\nend\nreturn App()\n"})")
                        .status == 201);

    STIPPLE_REQUIRE(host.carousel().pin("seen", 1000));
    host.tick(1000);

    // Exactly one pixel, at whatever hour the device thinks it is - the point
    // is that the script saw a real clock rather than a default.
    STIPPLE_CHECK_EQ(countLit(platform.simulatedDisplay().lastFrame()), 1);
}
