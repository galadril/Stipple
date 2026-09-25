// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/script/ScriptHost.h"

#include <string>

#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "support/Golden.h"
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
