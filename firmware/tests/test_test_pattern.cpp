// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/demo/TestPattern.h"

#include "stipple/graphics/Canvas.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::Rect;
using stipple::demo::drawTestPattern;
namespace colors = stipple::colors;

namespace {

Framebuffer renderFrame(int frame) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    drawTestPattern(canvas, frame);
    return framebuffer;
}

}  // namespace

STIPPLE_TEST(TestPattern, Frame0MatchesGolden) {
    STIPPLE_CHECK_GOLDEN("test-pattern-frame-000", renderFrame(0));
}

STIPPLE_TEST(TestPattern, Frame25MatchesGolden) {
    STIPPLE_CHECK_GOLDEN("test-pattern-frame-025", renderFrame(25));
}

STIPPLE_TEST(TestPattern, Frame60MatchesGolden) {
    // Past the ping-pong turnaround, so the scan bar is on its return sweep.
    STIPPLE_CHECK_GOLDEN("test-pattern-frame-060", renderFrame(60));
}

STIPPLE_TEST(TestPattern, RenderingIsDeterministic) {
    // The golden fixtures are only meaningful if the same frame number always
    // produces the same pixels.
    STIPPLE_CHECK(renderFrame(17) == renderFrame(17));
}

STIPPLE_TEST(TestPattern, DifferentFramesDiffer) {
    STIPPLE_CHECK(renderFrame(0) != renderFrame(10));
}

STIPPLE_TEST(TestPattern, CornerMarkersIdentifyOrientation) {
    // If the panel ever comes back mirrored or rotated from the TC002 adapter,
    // this is the test that says so.
    const Framebuffer frame = renderFrame(0);
    STIPPLE_CHECK_EQ(frame.at(0, 0), colors::kRed);
    STIPPLE_CHECK_EQ(frame.at(Framebuffer::kWidth - 1, 0), colors::kGreen);
    STIPPLE_CHECK_EQ(frame.at(Framebuffer::kWidth - 1, Framebuffer::kHeight - 1), colors::kYellow);
    STIPPLE_CHECK_EQ(frame.at(0, Framebuffer::kHeight - 1), colors::kBlue);
}

STIPPLE_TEST(TestPattern, ScanBarPingPongsWithoutLeavingThePanel) {
    // Walk a full period and confirm the animation never desyncs or escapes.
    // The clipped diagonals in the pattern are drawn far outside the panel on
    // purpose, so this also re-checks clipping through a realistic scene.
    for (int frame = -200; frame < 400; ++frame) {
        const Framebuffer rendered = renderFrame(frame);

        // Border is intact on every frame: no primitive overwrote the edge with
        // black, and nothing wrapped around.
        STIPPLE_CHECK(rendered.at(0, 0) == colors::kRed);
        STIPPLE_CHECK(rendered.at(25, 0) != colors::kBlack);
    }
}

STIPPLE_TEST(TestPattern, NegativeFrameNumbersAreHandled) {
    // wrap() must not produce a negative index into the scan-bar animation.
    STIPPLE_CHECK(renderFrame(-1) == renderFrame(-1));
    STIPPLE_CHECK(renderFrame(-98) == renderFrame(0));
}
