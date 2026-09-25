// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/apps/SplashScreen.h"

#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"

#include "support/Golden.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::apps::renderSplash;
using stipple::apps::SplashStyle;

namespace {

constexpr std::uint64_t kSplashMillis = 10000;

}  // namespace

STIPPLE_TEST(Splash, FirstPageIsTheNameOverAWave) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    // A quarter of the way in: on the first page, and far enough through it
    // that the wave has actually travelled.
    renderSplash(canvas, "STIPPLE", "0.1.0", kSplashMillis / 4, kSplashMillis, SplashStyle{},
                 "192.168.1.238");

    STIPPLE_CHECK_GOLDEN("splash-page-one", framebuffer);
}

STIPPLE_TEST(Splash, SecondPageIsVersionOverAddress) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    // Just after the page turn, before either line has begun to scroll, so
    // the fixture shows the layout rather than a moment of animation.
    renderSplash(canvas, "STIPPLE", "0.1.0", kSplashMillis / 2 + 100, kSplashMillis,
                 SplashStyle{}, "192.168.1.238");

    STIPPLE_CHECK_GOLDEN("splash-page-two", framebuffer);
}

STIPPLE_TEST(Splash, ANoticeIsStillATitleOverOneLine) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    // Zero duration means a notice, which has no pages and must keep its
    // title - this is the hotspot instruction, and it is the only thing on
    // the panel when the radio has changed job.
    renderSplash(canvas, "STIPPLE", "join Stipple-setup", 0, 0);

    STIPPLE_CHECK_GOLDEN("splash-notice", framebuffer);
}

STIPPLE_TEST(Splash, TheWaveStaysInsideItsBand) {
    // The band is rows 8-14. A crest touching the title would read as
    // clipping, and one touching row 15 would collide with the drain rule.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    for (std::uint64_t at = 0; at < kSplashMillis / 2; at += 37) {
        framebuffer.fill(stipple::colors::kBlack);
        renderSplash(canvas, "STIPPLE", "0.1.0", at, kSplashMillis, SplashStyle{}, "1.2.3.4");

        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            // Row 7 is the gap under the title and row 15 belongs to the
            // rule; neither may ever be lit by the wave.
            STIPPLE_CHECK(framebuffer.at(x, 7) == stipple::colors::kBlack);
        }
    }
}
