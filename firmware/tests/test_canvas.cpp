// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/graphics/Canvas.h"

#include <array>

#include "support/TestFramework.h"

using stipple::BitmapView;
using stipple::Canvas;
using stipple::ClipScope;
using stipple::Framebuffer;
using stipple::Rect;
using stipple::Rgb;
namespace colors = stipple::colors;

namespace {

int countNonBlack(const Framebuffer& framebuffer) {
    int count = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (framebuffer.at(x, y) != colors::kBlack) {
                ++count;
            }
        }
    }
    return count;
}

}  // namespace

// --- clipping: the guarantee the whole compositor rests on -------------------

STIPPLE_TEST(Canvas, ClipDefaultsToTheWholePanel) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    STIPPLE_CHECK_EQ(canvas.clip(), Framebuffer::bounds());
}

STIPPLE_TEST(Canvas, ClearFillsOnlyTheClipRegion) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.setClip(Rect({10, 4, 6, 3}));
    canvas.clear(colors::kWhite);

    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 18);
    STIPPLE_CHECK_EQ(framebuffer.at(10, 4), colors::kWhite);
    STIPPLE_CHECK_EQ(framebuffer.at(15, 6), colors::kWhite);
    STIPPLE_CHECK_EQ(framebuffer.at(16, 6), colors::kBlack);
    STIPPLE_CHECK_EQ(framebuffer.at(9, 4), colors::kBlack);
}

STIPPLE_TEST(Canvas, ClipCannotBeWidenedBeyondThePanel) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.setClip(Rect({-100, -100, 1000, 1000}));
    STIPPLE_CHECK_EQ(canvas.clip(), Framebuffer::bounds());
}

STIPPLE_TEST(Canvas, NestedClipScopeCannotEscapeItsParent) {
    // This is the §9.3 promise: an app handed a region cannot scribble outside
    // it, even by explicitly asking for a larger rect.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    ClipScope outer(canvas, Rect({10, 4, 10, 8}));
    {
        ClipScope inner(canvas, Rect({0, 0, 52, 16}));
        STIPPLE_CHECK_EQ(canvas.clip(), Rect({10, 4, 10, 8}));
        canvas.clear(colors::kWhite);
    }

    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 80);
    STIPPLE_CHECK_EQ(framebuffer.at(9, 4), colors::kBlack);
    STIPPLE_CHECK_EQ(framebuffer.at(20, 4), colors::kBlack);
}

STIPPLE_TEST(Canvas, ClipScopeRestoresPreviousClipOnExit) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.setClip(Rect({4, 4, 20, 8}));
    {
        ClipScope scope(canvas, Rect({5, 5, 2, 2}));
        STIPPLE_CHECK_EQ(canvas.clip(), Rect({5, 5, 2, 2}));
    }
    STIPPLE_CHECK_EQ(canvas.clip(), Rect({4, 4, 20, 8}));
}

STIPPLE_TEST(Canvas, DisjointClipScopeDrawsNothing) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    ClipScope outer(canvas, Rect({0, 0, 10, 8}));
    {
        ClipScope inner(canvas, Rect({30, 0, 10, 8}));
        canvas.clear(colors::kWhite);
        canvas.fillRect(Framebuffer::bounds(), colors::kRed);
    }
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 0);
}

// --- primitives --------------------------------------------------------------

STIPPLE_TEST(Canvas, PixelOutsideThePanelIsIgnored) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.pixel(-1, 0, colors::kWhite);
    canvas.pixel(0, -1, colors::kWhite);
    canvas.pixel(Framebuffer::kWidth, 0, colors::kWhite);
    canvas.pixel(0, Framebuffer::kHeight, colors::kWhite);
    canvas.pixel(10000, 10000, colors::kWhite);

    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 0);
}

STIPPLE_TEST(Canvas, PixelDrawsAtPanelCorners) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.pixel(0, 0, colors::kRed);
    canvas.pixel(Framebuffer::kWidth - 1, Framebuffer::kHeight - 1, colors::kBlue);

    STIPPLE_CHECK_EQ(framebuffer.at(0, 0), colors::kRed);
    STIPPLE_CHECK_EQ(framebuffer.at(51, 15), colors::kBlue);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 2);
}

STIPPLE_TEST(Canvas, FillRectIsClippedToThePanel) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.fillRect(Rect({-5, -5, 10, 10}), colors::kWhite);

    // Only the 5x5 block that actually lands on the panel.
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 25);
    STIPPLE_CHECK_EQ(framebuffer.at(4, 4), colors::kWhite);
    STIPPLE_CHECK_EQ(framebuffer.at(5, 4), colors::kBlack);
}

STIPPLE_TEST(Canvas, DegenerateRectsDrawNothing) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.fillRect(Rect({5, 5, 0, 10}), colors::kWhite);
    canvas.fillRect(Rect({5, 5, 10, 0}), colors::kWhite);
    canvas.fillRect(Rect({5, 5, -4, -4}), colors::kWhite);
    canvas.rect(Rect({5, 5, 0, 0}), colors::kWhite);
    canvas.hLine(0, 0, 0, colors::kWhite);
    canvas.vLine(0, 0, -3, colors::kWhite);

    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 0);
}

STIPPLE_TEST(Canvas, RectDrawsOutlineOnly) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.rect(Rect({2, 2, 6, 5}), colors::kWhite);

    // Perimeter of a 6x5 rect: 2*6 + 2*(5-2) = 18 pixels.
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 18);
    STIPPLE_CHECK_EQ(framebuffer.at(2, 2), colors::kWhite);
    STIPPLE_CHECK_EQ(framebuffer.at(7, 6), colors::kWhite);
    STIPPLE_CHECK_EQ(framebuffer.at(3, 3), colors::kBlack);  // hollow interior
}

STIPPLE_TEST(Canvas, SinglePixelAndSingleLineRectsDegradeCleanly) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.rect(Rect({0, 0, 1, 1}), colors::kWhite);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 1);

    framebuffer.clear();
    canvas.rect(Rect({0, 0, 5, 1}), colors::kWhite);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 5);

    framebuffer.clear();
    canvas.rect(Rect({0, 0, 1, 5}), colors::kWhite);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 5);
}

STIPPLE_TEST(Canvas, LineHitsBothEndpoints) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.line(3, 2, 20, 11, colors::kWhite);

    STIPPLE_CHECK_EQ(framebuffer.at(3, 2), colors::kWhite);
    STIPPLE_CHECK_EQ(framebuffer.at(20, 11), colors::kWhite);
}

STIPPLE_TEST(Canvas, HorizontalAndVerticalLinesAreExact) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.line(0, 8, 51, 8, colors::kWhite);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 52);

    framebuffer.clear();
    canvas.line(25, 0, 25, 15, colors::kWhite);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 16);
}

STIPPLE_TEST(Canvas, SinglePointLineDrawsOnePixel) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.line(7, 7, 7, 7, colors::kWhite);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 1);
}

STIPPLE_TEST(Canvas, FullyOffscreenLineDrawsNothing) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.line(-50, -50, -10, -10, colors::kWhite);
    canvas.line(100, 100, 200, 200, colors::kWhite);

    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 0);
}

STIPPLE_TEST(Canvas, PartiallyOffscreenLineDrawsOnlyTheVisiblePart) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.line(-20, 8, 20, 8, colors::kWhite);

    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 21);
    STIPPLE_CHECK_EQ(framebuffer.at(0, 8), colors::kWhite);
    STIPPLE_CHECK_EQ(framebuffer.at(20, 8), colors::kWhite);
    STIPPLE_CHECK_EQ(framebuffer.at(21, 8), colors::kBlack);
}

// --- blitting ----------------------------------------------------------------

STIPPLE_TEST(Canvas, BlitCopiesBitmapPixels) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const std::array<Rgb, 4> pixels{colors::kRed, colors::kGreen, colors::kBlue, colors::kYellow};
    const BitmapView bitmap{pixels.data(), 2, 2};

    canvas.blit(5, 5, bitmap);

    STIPPLE_CHECK_EQ(framebuffer.at(5, 5), colors::kRed);
    STIPPLE_CHECK_EQ(framebuffer.at(6, 5), colors::kGreen);
    STIPPLE_CHECK_EQ(framebuffer.at(5, 6), colors::kBlue);
    STIPPLE_CHECK_EQ(framebuffer.at(6, 6), colors::kYellow);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 4);
}

STIPPLE_TEST(Canvas, BlitIsClippedAtThePanelEdge) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const std::array<Rgb, 4> pixels{colors::kRed, colors::kGreen, colors::kBlue, colors::kYellow};
    const BitmapView bitmap{pixels.data(), 2, 2};

    // Bottom-right corner: only the top-left source pixel lands on the panel.
    canvas.blit(Framebuffer::kWidth - 1, Framebuffer::kHeight - 1, bitmap);

    STIPPLE_CHECK_EQ(framebuffer.at(51, 15), colors::kRed);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 1);
}

STIPPLE_TEST(Canvas, BlitWithNegativeOriginTakesTheCorrectSourceRegion) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const std::array<Rgb, 4> pixels{colors::kRed, colors::kGreen, colors::kBlue, colors::kYellow};
    const BitmapView bitmap{pixels.data(), 2, 2};

    canvas.blit(-1, -1, bitmap);

    // Only the bottom-right source pixel is visible, at the panel origin.
    STIPPLE_CHECK_EQ(framebuffer.at(0, 0), colors::kYellow);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 1);
}

STIPPLE_TEST(Canvas, BlitKeyedSkipsTransparentPixels) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const std::array<Rgb, 4> pixels{colors::kRed, colors::kBlack, colors::kBlack, colors::kYellow};
    const BitmapView bitmap{pixels.data(), 2, 2};

    canvas.blitKeyed(5, 5, bitmap, colors::kBlack);

    STIPPLE_CHECK_EQ(framebuffer.at(5, 5), colors::kRed);
    STIPPLE_CHECK_EQ(framebuffer.at(6, 6), colors::kYellow);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 2);
}

STIPPLE_TEST(Canvas, InvalidBitmapIsIgnored) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    canvas.blit(0, 0, BitmapView({nullptr, 4, 4}));
    canvas.blit(0, 0, BitmapView({nullptr, 0, 0}));

    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 0);
}

STIPPLE_TEST(Canvas, BlitRespectsAnActiveClip) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const std::array<Rgb, 4> pixels{colors::kRed, colors::kGreen, colors::kBlue, colors::kYellow};
    const BitmapView bitmap{pixels.data(), 2, 2};

    ClipScope scope(canvas, Rect({5, 5, 1, 1}));
    canvas.blit(5, 5, bitmap);

    STIPPLE_CHECK_EQ(framebuffer.at(5, 5), colors::kRed);
    STIPPLE_CHECK_EQ(countNonBlack(framebuffer), 1);
}
