// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every script in scripts/ is compiled and run here.
//
// The directory is a shop: the site lists it, and people are invited to add to
// it by pull request. A shop that ships a script which does not compile is
// worse than a shop with nothing in it - somebody pastes it into their device,
// gets a red dot and a compiler error, and has no way to know whether the
// fault is theirs. So every script is run against the real engine, on the real
// panel size, before it can be merged.
//
// This is also the only test that would catch a builtin being renamed out from
// under the examples.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stipple/graphics/Canvas.h"
#include "stipple/imageio/Png.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/script/ScriptHost.h"
#include "stipple/script/ScriptStore.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::script::ScriptStore;
namespace colors = stipple::colors;

namespace {

#ifndef STIPPLE_SCRIPTS_DIR
#define STIPPLE_SCRIPTS_DIR "scripts"
#endif

struct Example {
    std::string name;
    std::string source;
};

std::string readFile(const std::string& path) {
    std::string out;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return out;
    }
    char buffer[4096];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        out.append(buffer, got);
    }
    std::fclose(file);
    return out;
}

/// The names in scripts/, listed here rather than enumerated from disk.
///
/// A directory scan would quietly pass if the directory were empty or if the
/// test binary ran from somewhere unexpected - "no scripts found, nothing to
/// check, all good" is exactly the shape of a test that has stopped testing.
/// A fixed list fails loudly when a file goes missing, and adding a script
/// means adding a line here, which is the moment to think about whether it
/// belongs in the shop.
const char* kExampleFiles[] = {
    "aquarium.be",
    "battery.be",
    "big-clock.be",
    "binary-clock.be",
    "day-progress.be",
    "fireplace.be",
    "flappy.be",
    "starfield.be",
};

std::vector<Example> loadExamples() {
    std::vector<Example> examples;
    for (const char* name : kExampleFiles) {
        Example example;
        example.name = name;
        example.source = readFile(std::string(STIPPLE_SCRIPTS_DIR) + "/" + name);
        examples.push_back(std::move(example));
    }
    return examples;
}

int countLit(const Framebuffer& framebuffer) {
    int lit = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (framebuffer.at(x, y) != colors::kBlack) { ++lit; }
        }
    }
    return lit;
}

}  // namespace

STIPPLE_TEST(ShopScripts, EveryPublishedScriptCompilesAndDraws) {
    const std::vector<Example> examples = loadExamples();
    STIPPLE_REQUIRE(!examples.empty());

    for (const Example& example : examples) {
        // A missing file is a failure, not a skip.
        STIPPLE_REQUIRE(!example.source.empty());

        ScriptStore store;
        stipple::script::ScriptEnvironment environment;
        environment.timeKnown = true;
        environment.hour = 14;
        environment.minute = 37;
        environment.second = 22;
        environment.day = 9;
        environment.month = 11;
        environment.year = 2025;
        environment.weekday = 0;
        environment.batteryKnown = true;
        environment.batteryPercent = 64;
        environment.charging = true;
        store.setEnvironment(environment);

        const auto result = store.put("shop", example.name, example.source);
        if (result != stipple::script::ScriptPutResult::Added) {
            std::printf("    [shop] %s: %s\n", example.name.c_str(),
                        store.find("shop") != nullptr
                            ? store.find("shop")->problem.c_str()
                            : "would not store");
        }
        STIPPLE_REQUIRE(result == stipple::script::ScriptPutResult::Added);

        // Ninety frames, three seconds at thirty. Long enough for an
        // animation to wrap, a pipe to cross the panel and a blink to blink -
        // a script that only fails on its second cycle is one the shop would
        // otherwise ship.
        Framebuffer framebuffer;
        Canvas canvas(framebuffer);
        int litSomething = 0;
        for (int frame = 0; frame < 90; ++frame) {
            const bool drew =
                store.draw("shop", canvas, static_cast<std::uint64_t>(frame) * 33u);
            if (!drew) {
                std::printf("    [shop] %s failed on frame %d: %s\n", example.name.c_str(),
                            frame, store.find("shop")->problem.c_str());
            }
            STIPPLE_REQUIRE(drew);
            litSomething += countLit(framebuffer) > 0 ? 1 : 0;

            // Press the button every so often, so a script with an
            // on_button is exercised in play rather than watched falling to
            // its death. It also means the handler's own instruction budget
            // and stack balance are covered for every published script, not
            // only for the ones in the unit tests.
            if (frame % 11 == 0) {
                store.button("shop", "action");
            }
        }

        // And it actually drew. A script that compiles, runs and leaves the
        // panel black passes every check above and is still broken.
        STIPPLE_CHECK(litSomething > 0);
    }
}

STIPPLE_TEST(ShopScripts, NoneOfThemLeak) {
    // Same reasoning as the builtin leak guard, applied to the code people
    // will actually copy. A published script that grows its live set every
    // frame takes the device down after a day, and the person who installed
    // it has no reason to suspect the script.
    for (const Example& example : loadExamples()) {
        STIPPLE_REQUIRE(!example.source.empty());

        ScriptStore store;
        STIPPLE_REQUIRE(store.put("shop", example.name, example.source) ==
                        stipple::script::ScriptPutResult::Added);

        Framebuffer framebuffer;
        Canvas canvas(framebuffer);
        for (int frame = 0; frame < 120; ++frame) {
            store.draw("shop", canvas, static_cast<std::uint64_t>(frame) * 33u);
        }
        store.collectGarbage("shop");
        const std::size_t before = store.find("shop")->memoryBytes;

        for (int frame = 0; frame < 600; ++frame) {
            store.draw("shop", canvas, static_cast<std::uint64_t>(frame) * 33u);
        }
        store.collectGarbage("shop");
        const std::size_t after = store.find("shop")->memoryBytes;

        if (after > before) {
            std::printf("    [shop] %s grew %zu -> %zu over 600 frames\n",
                        example.name.c_str(), before, after);
        }
        // Some slack: a script that legitimately holds a list of stars will
        // settle at a size, and the settling can straddle the first
        // collection. Growth of kilobytes is a leak; a few hundred bytes once
        // is a data structure finding its shape.
        STIPPLE_CHECK(after <= before + 512u);
    }
}

STIPPLE_TEST(ShopScripts, EachOneSaysWhenTheDeviceCannotTellItTheTime) {
    // The examples are what people copy, so they are where the honesty about
    // absence has to be demonstrated rather than described. A clock script
    // that draws 00:00 on a device with no time has invented it, and anybody
    // starting from that script inherits the bug. ADR 0013.
    const char* needTime[] = {"big-clock.be", "binary-clock.be", "day-progress.be"};

    for (const char* name : needTime) {
        const std::string source = readFile(std::string(STIPPLE_SCRIPTS_DIR) + "/" + name);
        STIPPLE_REQUIRE(!source.empty());

        ScriptStore store;
        // Default environment: the clock has never been set.
        STIPPLE_REQUIRE(store.put("shop", name, source) ==
                        stipple::script::ScriptPutResult::Added);

        Framebuffer framebuffer;
        Canvas canvas(framebuffer);
        STIPPLE_REQUIRE(store.draw("shop", canvas, 0));

        // It says something rather than drawing a time it does not have.
        const int lit = countLit(framebuffer);
        if (lit == 0) {
            std::printf("    [shop] %s draws nothing when the time is unknown\n", name);
        }
        STIPPLE_CHECK(lit > 0);
    }
}

// Not a test so much as a way to look at them.
//
// "Does this look right on a 52x16 panel" cannot be asserted, only seen. Set
// STIPPLE_SHOP_FRAMES=<directory> and this writes one raw RGB stream per
// script - every frame, back to back, 52*16*3 bytes each - which
// tooling/site/build-previews.py turns into the animations on the shop page.
//
// Raw rather than PNG, because the consumer is a script that wants pixels and
// putting them through an image format only to take them out again would be
// two conversions to get back where it started.
//
// The frames come from the real engine at the real size. A preview drawn any
// other way would be a picture of something that does not exist.
STIPPLE_TEST(ShopScripts, WriteFramesOnRequest) {
    const char* directory = std::getenv("STIPPLE_SHOP_FRAMES");
    if (directory == nullptr) {
        return;
    }

    // Three seconds at thirty frames. Long enough for the animations here to
    // show what they do and short enough that the page is not carrying
    // megabytes of them.
    constexpr int kFrames = 90;
    constexpr std::uint64_t kFrameMillis = 33;

    stipple::script::ScriptEnvironment environment;
    environment.timeKnown = true;
    environment.hour = 14;
    environment.minute = 37;
    environment.second = 22;
    environment.day = 9;
    environment.month = 11;
    environment.year = 2025;
    environment.weekday = 0;
    environment.batteryKnown = true;
    environment.batteryPercent = 64;
    environment.charging = true;

    for (const Example& example : loadExamples()) {
        if (example.source.empty()) { continue; }

        ScriptStore store;
        store.setEnvironment(environment);
        if (store.put("shop", example.name, example.source) !=
            stipple::script::ScriptPutResult::Added) {
            continue;
        }

        const std::string stem = example.name.substr(0, example.name.find('.'));
        const std::string path = std::string(directory) + "/" + stem + ".rgb";
        std::FILE* out = std::fopen(path.c_str(), "wb");
        if (out == nullptr) { continue; }

        Framebuffer framebuffer;
        Canvas canvas(framebuffer);
        for (int frame = 0; frame < kFrames; ++frame) {
            // A script with an on_button is played rather than watched. A
            // preview of a game showing its game-over screen is a preview of
            // nothing.
            if (frame % 12 == 0) {
                store.button("shop", "select");
            }
            store.draw("shop", canvas, static_cast<std::uint64_t>(frame) * kFrameMillis);

            for (int y = 0; y < Framebuffer::kHeight; ++y) {
                for (int x = 0; x < Framebuffer::kWidth; ++x) {
                    const stipple::Rgb pixel = framebuffer.at(x, y);
                    const unsigned char triple[3] = {pixel.r, pixel.g, pixel.b};
                    std::fwrite(triple, 1, 3, out);
                }
            }
        }
        std::fclose(out);
        std::printf("    [frames] %s: %d frames\n", example.name.c_str(), kFrames);
    }
}

// Not a test so much as a way to look at them.
//
// "Does this look right on a 52x16 panel" cannot be asserted, only seen. Set
// STIPPLE_SHOP_PREVIEW=<directory> and this writes one PNG per script per
// sampled frame; without it, it does nothing and costs nothing.
STIPPLE_TEST(ShopScripts, PreviewsOnRequest) {
    const char* directory = std::getenv("STIPPLE_SHOP_PREVIEW");
    if (directory == nullptr) {
        return;
    }

    stipple::script::ScriptEnvironment environment;
    environment.timeKnown = true;
    environment.hour = 14;
    environment.minute = 37;
    environment.second = 22;
    environment.day = 9;
    environment.month = 11;
    environment.year = 2025;
    environment.batteryKnown = true;
    environment.batteryPercent = 64;
    environment.charging = true;

    for (const Example& example : loadExamples()) {
        if (example.source.empty()) { continue; }

        ScriptStore store;
        store.setEnvironment(environment);
        if (store.put("shop", example.name, example.source) !=
            stipple::script::ScriptPutResult::Added) {
            continue;
        }

        Framebuffer framebuffer;
        Canvas canvas(framebuffer);
        const int kFrames[] = {0, 30, 90, 200};
        int previous = 0;
        for (const int target : kFrames) {
            for (int frame = previous; frame < target; ++frame) {
                store.draw("shop", canvas, static_cast<std::uint64_t>(frame) * 33u);
                // Play it, rather than watch it fall. A preview of a game
                // showing its game-over screen is a preview of nothing.
                if (frame % 12 == 0) {
                    store.button("shop", "action");
                }
            }
            previous = target;
            store.draw("shop", canvas, static_cast<std::uint64_t>(target) * 33u);

            std::string stem = example.name.substr(0, example.name.find('.'));
            const std::string path = std::string(directory) + "/" + stem + "-" +
                                     std::to_string(target) + ".png";
            stipple::imageio::writePng(path, framebuffer, 8);
        }
    }
}

// What each script actually costs per frame, against the 200,000 budget.
//
// Set STIPPLE_SCRIPT_COST=1. A number, rather than "it fits" or "it does not":
// a script at 180,000 passes today and fails the first time somebody adds a
// line to it, and the author should be able to see that coming.
STIPPLE_TEST(ShopScripts, InstructionCostProbe) {
    if (std::getenv("STIPPLE_SCRIPT_COST") == nullptr) {
        return;
    }
    for (const Example& example : loadExamples()) {
        if (example.source.empty()) { continue; }

        ScriptStore store;
        stipple::script::ScriptEnvironment environment;
        environment.timeKnown = true;
        environment.hour = 14;
        store.setEnvironment(environment);
        if (store.put("shop", example.name, example.source) !=
            stipple::script::ScriptPutResult::Added) {
            continue;
        }

        Framebuffer framebuffer;
        Canvas canvas(framebuffer);
        // Averaged over many frames, not taken from one.
        //
        // lastInstructions only moves in steps of 65,536 - that is the
        // heartbeat the budget is enforced on - so any single frame can only
        // say "somewhere in this 64K band". Summing over 300 frames and
        // dividing gives about 200 instructions of resolution, which is the
        // difference between optimising and guessing at it.
        constexpr int kFrames = 300;
        std::uint64_t total = 0;
        std::uint32_t worst = 0;
        for (int frame = 0; frame < kFrames; ++frame) {
            store.draw("shop", canvas, static_cast<std::uint64_t>(frame) * 33u);
            const stipple::script::Script* entry = store.find("shop");
            if (entry != nullptr) {
                total += entry->lastInstructions;
                if (entry->lastInstructions > worst) { worst = entry->lastInstructions; }
            }
        }
        std::printf("    [cost] %-18s mean %8llu  worst band %7u  budget %u\n",
                    example.name.c_str(),
                    static_cast<unsigned long long>(total / kFrames), worst,
                    stipple::script::ScriptHost::kInstructionBudget);
    }
}
