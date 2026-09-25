// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/script/ScriptHost.h"

#include <cstdio>
#include <string>

#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::script::ScriptHost;
namespace colors = stipple::colors;

namespace {

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

STIPPLE_TEST(Script, AnInterpreterCostsAKnownAmountOfMemory) {
    // How much RAM a script costs decides how many the device can run, and
    // that is not a question to answer by guessing. This is Berry's own
    // accounting, so a change to the sandbox that doubles the footprint
    // fails here rather than on a device with 36 MB and no swap.
    ScriptHost empty;
    const std::size_t baseline = empty.memoryBytes();

    // A bare interpreter, before any script. The bound is generous - the
    // point is to catch a regression of the order that matters, not to pin
    // an exact number that shifts with every Berry release.
    STIPPLE_CHECK(baseline > 0);
    STIPPLE_CHECK(baseline < 64u * 1024u);

    ScriptHost host;
    std::string problem;
    STIPPLE_REQUIRE(host.load(
        "class App\n"
        "  var frame\n"
        "  def init()\n"
        "    self.frame = 0\n"
        "  end\n"
        "  def draw()\n"
        "    self.frame += 1\n"
        "    clear(rgb(0, 0, 0))\n"
        "    text(0, 0, 'hello', rgb(255, 255, 255))\n"
        "  end\n"
        "end\n"
        "return App()\n",
        problem));

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    for (int frame = 0; frame < 120; ++frame) {
        STIPPLE_REQUIRE(host.draw(canvas, static_cast<std::uint64_t>(frame) * 33u, problem));
    }

    // Berry is garbage collected, so memoryBytes() sawtooths - it climbs
    // with every temporary a frame makes and drops when a collection runs.
    // Measuring it at two arbitrary moments compares two points on a sawtooth
    // and says nothing. What matters is the floor, so collect first.
    const std::size_t liveAfter120 = host.collectGarbage();
    STIPPLE_CHECK(liveAfter120 >= baseline);

    for (int frame = 0; frame < 600; ++frame) {
        STIPPLE_REQUIRE(host.draw(canvas, static_cast<std::uint64_t>(frame) * 33u, problem));
    }
    const std::size_t liveAfter720 = host.collectGarbage();

    // Five times the frames, and the live set must not have moved. A script
    // that holds on to a little every frame is one that takes the device down
    // after a day - the failure that a short test never sees unless it looks
    // at the floor rather than the peak.
    STIPPLE_CHECK(liveAfter720 <= liveAfter120 + 512u);

    std::printf("    [memory] bare interpreter %zu bytes; live with a script:"
                " %zu after 120 frames, %zu after 720; uncollected peak %zu\n",
                baseline, liveAfter120, liveAfter720, host.memoryBytes());
}

STIPPLE_TEST(Script, AScriptCanDrawAPixel) {
    // The whole point, reduced to its smallest form.
    ScriptHost host;
    std::string problem;

    const char* source =
        "class App\n"
        "  def draw()\n"
        "    pixel(3, 4, rgb(255, 0, 0))\n"
        "  end\n"
        "end\n"
        "return App()\n";

    STIPPLE_REQUIRE(host.load(source, problem));
    STIPPLE_CHECK(problem.empty());

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(host.draw(canvas, 0, problem));

    STIPPLE_CHECK(framebuffer.at(3, 4) == stipple::rgb(255, 0, 0));
    STIPPLE_CHECK_EQ(countLit(framebuffer), 1);
}

STIPPLE_TEST(Script, TheApiNamesMatchTheOnesScriptsAreWrittenAgainst) {
    // Compatibility is the reason Berry was chosen at all: a script written
    // for AWTRIX NG should run here unchanged. These are the names the
    // author's own scripts use.
    ScriptHost host;
    std::string problem;

    const char* source =
        "class App\n"
        "  def draw()\n"
        "    clear(rgb(0, 0, 0))\n"
        "    var w = width()\n"
        "    var h = height()\n"
        "    rect_fill(0, 0, 4, 4, rgb(0, 255, 0))\n"
        "    rect(6, 0, 4, 4, rgb(0, 0, 255))\n"
        "    line(0, 15, w - 1, 15, rgb(255, 255, 0))\n"
        "    var adv = text(12, 0, '1', rgb(255, 255, 255))\n"
        "    var tw = text_width('12')\n"
        "    pixel(w - 1, h - 1, rgb(255, 0, 255))\n"
        "  end\n"
        "end\n"
        "return App()\n";

    STIPPLE_REQUIRE(host.load(source, problem));

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(host.draw(canvas, 0, problem));
    STIPPLE_CHECK(problem.empty());

    // Each primitive left something behind.
    STIPPLE_CHECK(framebuffer.at(0, 0) == stipple::rgb(0, 255, 0));
    STIPPLE_CHECK(framebuffer.at(6, 0) == stipple::rgb(0, 0, 255));
    STIPPLE_CHECK(framebuffer.at(0, 15) == stipple::rgb(255, 255, 0));
    STIPPLE_CHECK(framebuffer.at(Framebuffer::kWidth - 1, Framebuffer::kHeight - 1) ==
                  stipple::rgb(255, 0, 255));
}

STIPPLE_TEST(Script, ARunawayLoopLosesItsFrameRatherThanThePanel) {
    // The test this whole sandbox exists for. Without the instruction budget
    // this call never returns, and the device stops rendering - on hardware
    // that is indistinguishable from a crash.
    ScriptHost host;
    std::string problem;

    const char* source =
        "class App\n"
        "  def draw()\n"
        "    while true\n"
        "    end\n"
        "  end\n"
        "end\n"
        "return App()\n";

    STIPPLE_REQUIRE(host.load(source, problem));

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(!host.draw(canvas, 0, problem));
    STIPPLE_CHECK(!problem.empty());

    // And it is disabled rather than left to do it again thirty times a
    // second.
    STIPPLE_CHECK(!host.ready());
    STIPPLE_CHECK(host.lastInstructions() > 0);
}

STIPPLE_TEST(Script, ACompileErrorSaysWhereItIs) {
    // A compile error with no line number is useless in the editor that just
    // showed the author their mistake.
    ScriptHost host;
    std::string problem;

    const char* source =
        "class App\n"
        "  def draw()\n"
        "    pixel(1, 2,\n"   // unclosed
        "  end\n"
        "end\n";

    STIPPLE_CHECK(!host.load(source, problem));
    STIPPLE_CHECK(!problem.empty());
    // Berry reports "app:LINE: ..." - the chunk name proves the message came
    // from the compiler rather than being invented here.
    STIPPLE_CHECK(problem.find("app:") != std::string::npos);
}

STIPPLE_TEST(Script, AScriptThatReturnsNothingIsRefused) {
    ScriptHost host;
    std::string problem;
    STIPPLE_CHECK(!host.load("var x = 1\n", problem));
    STIPPLE_CHECK(!problem.empty());
}

STIPPLE_TEST(Script, AScriptWithoutDrawIsRefusedWhenItRuns) {
    ScriptHost host;
    std::string problem;
    STIPPLE_REQUIRE(host.load("class App\nend\nreturn App()\n", problem));

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(!host.draw(canvas, 0, problem));
    STIPPLE_CHECK(problem.find("draw") != std::string::npos);
}

STIPPLE_TEST(Script, AnErrorThrownWhileDrawingIsCaught) {
    ScriptHost host;
    std::string problem;
    const char* source =
        "class App\n"
        "  def draw()\n"
        "    raise 'deliberate'\n"
        "  end\n"
        "end\n"
        "return App()\n";
    STIPPLE_REQUIRE(host.load(source, problem));

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(!host.draw(canvas, 0, problem));
    STIPPLE_CHECK(!host.ready());
}

STIPPLE_TEST(Script, TheSandboxHasNoFilesystemAndNoLoader) {
    // These are not style choices. A script that can open a file can read the
    // Wi-Fi credentials, and one that can import a shared object is running
    // native code as root.
    ScriptHost host;
    std::string problem;

    for (const char* forbidden : {"import os\nreturn nil\n",
                                  "import sys\nreturn nil\n",
                                  "import debug\nreturn nil\n",
                                  "import introspect\nreturn nil\n",
                                  "import solidify\nreturn nil\n"}) {
        STIPPLE_CHECK(!host.load(forbidden, problem));
    }

    // And the ones that are allowed still work, so the test above is not
    // passing because every import fails.
    STIPPLE_CHECK(host.load("import math\nimport string\nimport json\n"
                            "class App\n  def draw()\n  end\nend\nreturn App()\n",
                            problem));
}

STIPPLE_TEST(Script, OpenIsRefusedRatherThanAbsent) {
    // The one that would have got through.
    //
    // `open` is in Berry's builtin table unconditionally - BE_USE_FILE_SYSTEM
    // does not gate it, it only stops Berry's own port from implementing
    // be_fopen. A port that supplies one, which is the normal thing to do
    // because the compiler wants it, hands open() back to every script with
    // the configuration still reading BE_USE_FILE_SYSTEM 0.
    //
    // So this asserts the outcome rather than the setting: calling it fails.
    ScriptHost host;
    std::string problem;

    const char* source =
        "class App\n"
        "  def draw()\n"
        "    var f = open('/data/stipple/config.json', 'r')\n"
        "  end\n"
        "end\n"
        "return App()\n";

    // It compiles - open() is a name that exists - and then fails when run.
    STIPPLE_REQUIRE(host.load(source, problem));

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(!host.draw(canvas, 0, problem));
    STIPPLE_CHECK(!problem.empty());
    STIPPLE_CHECK_EQ(countLit(framebuffer), 0);
}

STIPPLE_TEST(Script, SourceIsBounded) {
    ScriptHost host;
    std::string problem;
    const std::string huge(ScriptHost::kMaxSourceBytes + 1, ' ');
    STIPPLE_CHECK(!host.load(huge, problem));
    STIPPLE_CHECK(!problem.empty());
    STIPPLE_CHECK(!host.load("", problem));
}

STIPPLE_TEST(Script, CoordinatesFromAScriptAreClippedNotTrusted) {
    // A script is untrusted input that happens to be executable. Writing at
    // -5000 must clip like any other drawing, not corrupt memory.
    ScriptHost host;
    std::string problem;
    const char* source =
        "class App\n"
        "  def draw()\n"
        "    pixel(-5000, -5000, rgb(255, 0, 0))\n"
        "    pixel(99999, 99999, rgb(255, 0, 0))\n"
        "    rect_fill(-100, -100, 100000, 100000, rgb(0, 255, 0))\n"
        "  end\n"
        "end\n"
        "return App()\n";
    STIPPLE_REQUIRE(host.load(source, problem));

    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK(host.draw(canvas, 0, problem));
    // The huge rectangle covers the panel; the two stray pixels changed
    // nothing outside it, which is what not crashing looks like.
    STIPPLE_CHECK_EQ(countLit(framebuffer), Framebuffer::kWidth * Framebuffer::kHeight);
}

STIPPLE_TEST(Script, AnimationGetsTheElapsedTime) {
    ScriptHost host;
    std::string problem;
    const char* source =
        "class App\n"
        "  def draw()\n"
        "    var t = now_ms()\n"
        "    pixel(t % width(), 0, rgb(255, 255, 255))\n"
        "  end\n"
        "end\n"
        "return App()\n";
    STIPPLE_REQUIRE(host.load(source, problem));

    Framebuffer first;
    Canvas a(first);
    STIPPLE_CHECK(host.draw(a, 3, problem));

    Framebuffer second;
    Canvas b(second);
    STIPPLE_CHECK(host.draw(b, 9, problem));

    STIPPLE_CHECK(first.at(3, 0) != colors::kBlack);
    STIPPLE_CHECK(second.at(9, 0) != colors::kBlack);
}

STIPPLE_TEST(Script, NoBuiltinLeaksASlotPerFrame) {
    // Every builtin, one per script, 600 frames each, live set compared
    // before and after.
    //
    // This exists because the first version of draw() leaked exactly one
    // stack slot per call by popping a count that was one short. Sixteen
    // bytes a frame is invisible in any short test and fatal over a couple
    // of minutes, and nothing about the symptom pointed at the cause - the
    // scripts were fine, the builtins were fine, and the panel just stopped.
    // So each builtin is checked on the way in rather than trusted.
    struct Case {
        const char* label;
        const char* body;
    };
    const Case cases[] = {
        {"nothing at all", "    "},
        {"clear", "    clear(rgb(0,0,0))"},
        {"pixel", "    pixel(1,1,rgb(1,2,3))"},
        {"line", "    line(0,0,10,10,rgb(1,2,3))"},
        {"rect", "    rect(0,0,5,5,rgb(1,2,3))"},
        {"rect_fill", "    rect_fill(0,0,5,5,rgb(1,2,3))"},
        {"rgb", "    var c = rgb(1,2,3)"},
        {"text", "    text(0,0,'hello',rgb(255,255,255))"},
        {"text_width", "    var w = text_width('hello')"},
        {"now_ms", "    var t = now_ms()"},
        {"width and height", "    var w = width() + height()"},
        {"instance state", "    self.frame += 1"},
        // String building is where a script would most plausibly leak, since
        // every concatenation makes an object the collector has to reclaim.
        {"string building", "    var s = 'n=' + str(self.frame)"},
    };

    for (const Case& c : cases) {
        ScriptHost host;
        std::string problem;
        const std::string source =
            std::string("class App\n  var frame\n  def init()\n    self.frame = 0\n  end\n"
                        "  def draw()\n") + c.body + "\n  end\nend\nreturn App()\n";
        STIPPLE_REQUIRE(host.load(source, problem));

        Framebuffer framebuffer;
        Canvas canvas(framebuffer);
        for (int frame = 0; frame < 100; ++frame) {
            STIPPLE_REQUIRE(host.draw(canvas, static_cast<std::uint64_t>(frame), problem));
        }
        const std::size_t before = host.collectGarbage();

        for (int frame = 0; frame < 600; ++frame) {
            STIPPLE_REQUIRE(host.draw(canvas, static_cast<std::uint64_t>(frame), problem));
        }
        const std::size_t after = host.collectGarbage();

        if (after != before) {
            std::printf("    [leak] %s: %zu -> %zu over 600 frames\n", c.label, before, after);
        }
        // Exactly equal, not "close enough". The live set of a script drawing
        // the same frame over and over has no reason to move at all, and a
        // tolerance here is what would have let the original bug through.
        STIPPLE_CHECK_EQ(after, before);
    }
}
