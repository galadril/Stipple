// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/text/Text.h"

#include "stipple/graphics/Canvas.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::Rect;
using stipple::text::draw;
using stipple::text::drawLine;
using stipple::text::font5x7;
using stipple::text::HAlign;
using stipple::text::measure;
using stipple::text::measureLine;
using stipple::text::TextMetrics;
using stipple::text::TextStyle;
using stipple::text::VAlign;
namespace colors = stipple::colors;

namespace {

/// Bounding box of every non-black pixel, or an empty rect if nothing is lit.
/// Alignment is much clearer asserted as "where did the ink land" than as a
/// list of individual pixel coordinates.
Rect litBounds(const Framebuffer& framebuffer) {
    int minX = Framebuffer::kWidth;
    int minY = Framebuffer::kHeight;
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (framebuffer.at(x, y) != colors::kBlack) {
                if (x < minX) { minX = x; }
                if (y < minY) { minY = y; }
                if (x > maxX) { maxX = x; }
                if (y > maxY) { maxY = y; }
            }
        }
    }

    if (maxX < 0) {
        return Rect{};
    }
    return Rect{minX, minY, maxX - minX + 1, maxY - minY + 1};
}

TextStyle styleFor(HAlign hAlign, VAlign vAlign) {
    TextStyle style;
    style.font = &font5x7();
    style.color = colors::kWhite;
    style.hAlign = hAlign;
    style.vAlign = vAlign;
    return style;
}

}  // namespace

// --- measurement -------------------------------------------------------------

STIPPLE_TEST(Text, EmptyStringMeasuresZero) {
    STIPPLE_CHECK_EQ(measureLine("", font5x7()), 0);
}

STIPPLE_TEST(Text, SingleGlyphMeasuresItsOwnWidth) {
    STIPPLE_CHECK_EQ(measureLine("H", font5x7()), 5);
    STIPPLE_CHECK_EQ(measureLine("!", font5x7()), 1);
    STIPPLE_CHECK_EQ(measureLine(" ", font5x7()), 2);
}

STIPPLE_TEST(Text, SpacingGoesBetweenGlyphsNotAfterTheLast) {
    // Two 5-wide glyphs plus one gap. If spacing were appended after the last
    // glyph, centred text would sit half a pixel left and right-aligned text
    // would hang off the edge.
    STIPPLE_CHECK_EQ(measureLine("HH", font5x7()), 11);
    STIPPLE_CHECK_EQ(measureLine("HHH", font5x7()), 17);
}

STIPPLE_TEST(Text, LetterSpacingIsHonoured) {
    STIPPLE_CHECK_EQ(measureLine("HH", font5x7(), 0), 10);
    STIPPLE_CHECK_EQ(measureLine("HH", font5x7(), 3), 13);
}

STIPPLE_TEST(Text, ProportionalWidthsDiffer) {
    STIPPLE_CHECK(measureLine("i", font5x7()) < measureLine("H", font5x7()));
}

STIPPLE_TEST(Text, DegreeSignMeasuresAsItsOwnGlyph) {
    // "21.4°C" must not fall back to tofu for the degree sign.
    STIPPLE_CHECK_EQ(measureLine("\xC2\xB0", font5x7()), 3);
}

STIPPLE_TEST(Text, UnknownGlyphFallsBackToTofu) {
    // U+4E00 is not in a curated Latin pack; it renders as the box, width 5.
    STIPPLE_CHECK_EQ(measureLine("\xE4\xB8\x80", font5x7()), 5);
}

STIPPLE_TEST(Text, MeasuresMultipleLines) {
    const TextMetrics metrics = measure("HH\nH", font5x7());
    STIPPLE_CHECK_EQ(metrics.lineCount, 2);
    STIPPLE_CHECK_EQ(metrics.width, 11);          // the wider line
    STIPPLE_CHECK_EQ(metrics.height, 15);         // 7 + 1 + 7
}

STIPPLE_TEST(Text, CarriageReturnsAreIgnored) {
    STIPPLE_CHECK_EQ(measure("HH\r\nH", font5x7()).width, measure("HH\nH", font5x7()).width);
    STIPPLE_CHECK_EQ(measure("HH\r\nH", font5x7()).lineCount, 2);
}

STIPPLE_TEST(Text, EmptyInputHasNoLines) {
    const TextMetrics metrics = measure("", font5x7());
    STIPPLE_CHECK_EQ(metrics.lineCount, 1);  // one empty line
    STIPPLE_CHECK_EQ(metrics.width, 0);
}

// --- alignment ---------------------------------------------------------------

STIPPLE_TEST(Text, HorizontalAlignmentPlacesInk) {
    // 'H' lights both its outer columns, so the lit bounds are the glyph bounds.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    const Rect box = Framebuffer::bounds();

    draw(canvas, "H", box, styleFor(HAlign::Left, VAlign::Top));
    STIPPLE_CHECK_EQ(litBounds(framebuffer).x, 0);

    framebuffer.clear();
    draw(canvas, "H", box, styleFor(HAlign::Right, VAlign::Top));
    const Rect right = litBounds(framebuffer);
    STIPPLE_CHECK_EQ(right.right(), Framebuffer::kWidth);

    framebuffer.clear();
    draw(canvas, "H", box, styleFor(HAlign::Center, VAlign::Top));
    STIPPLE_CHECK_EQ(litBounds(framebuffer).x, (Framebuffer::kWidth - 5) / 2);
}

STIPPLE_TEST(Text, VerticalAlignmentPlacesInk) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    const Rect box = Framebuffer::bounds();

    draw(canvas, "H", box, styleFor(HAlign::Left, VAlign::Top));
    STIPPLE_CHECK_EQ(litBounds(framebuffer).y, 0);

    framebuffer.clear();
    draw(canvas, "H", box, styleFor(HAlign::Left, VAlign::Bottom));
    STIPPLE_CHECK_EQ(litBounds(framebuffer).bottom(), Framebuffer::kHeight);

    framebuffer.clear();
    draw(canvas, "H", box, styleFor(HAlign::Left, VAlign::Middle));
    STIPPLE_CHECK_EQ(litBounds(framebuffer).y, (Framebuffer::kHeight - 7) / 2);
}

STIPPLE_TEST(Text, AlignmentRespectsTheBoxNotThePanel) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const Rect box{20, 2, 20, 10};
    draw(canvas, "H", box, styleFor(HAlign::Right, VAlign::Bottom));

    const Rect lit = litBounds(framebuffer);
    STIPPLE_CHECK_EQ(lit.right(), box.right());
    STIPPLE_CHECK_EQ(lit.bottom(), box.bottom());
}

// --- clipping ----------------------------------------------------------------

STIPPLE_TEST(Text, TextIsClippedToItsBox) {
    // Two text elements must be able to sit side by side without one bleeding
    // into the other (blueprint §3.1).
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const Rect box{0, 0, 12, 7};
    draw(canvas, "HHHHHHHHHH", box, styleFor(HAlign::Left, VAlign::Top));

    const Rect lit = litBounds(framebuffer);
    STIPPLE_CHECK(lit.right() <= box.right());
    STIPPLE_CHECK(lit.bottom() <= box.bottom());
}

STIPPLE_TEST(Text, NegativeOriginClipsRatherThanWraps) {
    // Horizontal scrolling is implemented by drawing at a negative x, so this
    // has to clip cleanly instead of wrapping to the far edge.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    drawLine(canvas, "HHHH", -8, 0, font5x7(), colors::kWhite);

    const Rect lit = litBounds(framebuffer);
    STIPPLE_CHECK(lit.x >= 0);
    STIPPLE_CHECK(!lit.empty());
}

STIPPLE_TEST(Text, FullyOffscreenTextDrawsNothing) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    drawLine(canvas, "HHHH", -500, 0, font5x7(), colors::kWhite);
    drawLine(canvas, "HHHH", 500, 0, font5x7(), colors::kWhite);

    STIPPLE_CHECK(litBounds(framebuffer).empty());
}

STIPPLE_TEST(Text, DrawLineReturnsTheMeasuredWidth) {
    // The advance returned while drawing must agree with measureLine, or
    // scrolling and layout will disagree about where text ends.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const int advance = drawLine(canvas, "Hi!", 0, 0, font5x7(), colors::kWhite);
    STIPPLE_CHECK_EQ(advance, measureLine("Hi!", font5x7()));
}

// --- golden ------------------------------------------------------------------

STIPPLE_TEST(Text, ClockFaceMatchesGolden) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    TextStyle top = styleFor(HAlign::Center, VAlign::Top);
    top.color = stipple::rgb(0, 200, 255);
    draw(canvas, "STIPPLE", Rect({0, 0, 52, 7}), top);

    TextStyle bottom = styleFor(HAlign::Center, VAlign::Bottom);
    bottom.color = stipple::rgb(255, 170, 40);
    draw(canvas, "21.4\xC2\xB0" "C", Rect({0, 9, 52, 7}), bottom);

    STIPPLE_CHECK_GOLDEN("text-clock-face", framebuffer);
}

STIPPLE_TEST(Text, AlphabetMatchesGolden) {
    // A full sweep of the font, so any glyph regression shows up as a diff.
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);

    const TextStyle style = styleFor(HAlign::Left, VAlign::Top);
    draw(canvas, "ABCDEFGHIJ\nabcdefghij", Framebuffer::bounds(), style);

    STIPPLE_CHECK_GOLDEN("text-alphabet", framebuffer);
}
