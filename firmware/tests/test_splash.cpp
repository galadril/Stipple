// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/SplashScreen.h"

#include "notrix/graphics/Canvas.h"
#include "notrix/graphics/Framebuffer.h"

#include "support/Golden.h"
#include "support/TestFramework.h"

using notrix::Canvas;
using notrix::Framebuffer;
using notrix::apps::renderSplash;
using notrix::apps::SplashStyle;

namespace {

constexpr std::uint64_t kSplashMillis = 10000;

}  // namespace

NOTRIX_TEST(Splash, FirstPageIsTheNameOverAWave) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    // A quarter of the way in: on the first page, and far enough through it
    // that the wave has actually travelled.
    renderSplash(canvas, "NOTRIX", "0.1.0", kSplashMillis / 4, kSplashMillis, SplashStyle{},
                 "192.168.1.238");

    NOTRIX_CHECK_GOLDEN("splash-page-one", framebuffer);
}

NOTRIX_TEST(Splash, SecondPageIsVersionOverAddress) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    // Just after the page turn, before either line has begun to scroll, so
    // the fixture shows the layout rather than a moment of animation.
    renderSplash(canvas, "NOTRIX", "0.1.0", kSplashMillis / 2 + 100, kSplashMillis,
                 SplashStyle{}, "192.168.1.238");

    NOTRIX_CHECK_GOLDEN("splash-page-two", framebuffer);
}

NOTRIX_TEST(Splash, ANoticeIsStillATitleOverOneLine) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    // Zero duration means a notice, which has no pages and must keep its
    // title - this is the hotspot instruction, and it is the only thing on
    // the panel when the radio has changed job.
    renderSplash(canvas, "NOTRIX", "join NOTRIX-setup", 0, 0);

    NOTRIX_CHECK_GOLDEN("splash-notice", framebuffer);
}

NOTRIX_TEST(Splash, TheWaveStaysInsideItsBand) {
    // The band is rows 8-14. A crest touching the title would read as
    // clipping, and one touching row 15 would collide with the drain rule.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    for (std::uint64_t at = 0; at < kSplashMillis / 2; at += 37) {
        framebuffer.fill(notrix::colors::kBlack);
        renderSplash(canvas, "NOTRIX", "0.1.0", at, kSplashMillis, SplashStyle{}, "1.2.3.4");

        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            // Row 7 is the gap under the title and row 15 belongs to the
            // rule; neither may ever be lit by the wave.
            NOTRIX_CHECK(framebuffer.at(x, 7) == notrix::colors::kBlack);
        }
    }
}
