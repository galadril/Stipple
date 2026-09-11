// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "notrix/core/Geometry.h"
#include "notrix/core/Rgb.h"
#include "notrix/text/Font.h"

namespace notrix {

class Canvas;

namespace text {

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

struct TextStyle {
    const BitmapFont* font = nullptr;
    Rgb color = colors::kWhite;
    HAlign hAlign = HAlign::Left;
    VAlign vAlign = VAlign::Top;
    int letterSpacing = 1;
    int lineSpacing = 1;
};

struct TextMetrics {
    int width = 0;      ///< widest line
    int height = 0;     ///< total, including spacing between lines
    int lineCount = 0;
};

/// Advance width of one line, in pixels. Newlines are not interpreted.
///
/// Spacing goes *between* glyphs only, never after the last one, so a measured
/// string drawn at x=0 occupies exactly [0, width). Getting this wrong by one
/// pixel is what makes centred text drift and scrolling text clip — hence the
/// tests that pin it.
int measureLine(std::string_view utf8, const BitmapFont& font, int letterSpacing = 1) noexcept;

/// Measure a block, splitting on '\n' (a trailing '\r' per line is ignored).
TextMetrics measure(std::string_view utf8,
                    const BitmapFont& font,
                    int letterSpacing = 1,
                    int lineSpacing = 1) noexcept;

/// Draw one line with the top-left of the first cell at (x, y). Returns the
/// advance width consumed. Clipped by the canvas, so `x` may be negative — which
/// is exactly how horizontal scrolling is implemented.
int drawLine(Canvas& canvas,
             std::string_view utf8,
             int x,
             int y,
             const BitmapFont& font,
             Rgb color,
             int letterSpacing = 1);

/// Draw aligned inside `box`, clipped to it. Multi-line aware.
///
/// Text larger than the box is clipped rather than shrunk or wrapped; deciding
/// what to do about overflow (scroll, ellipsis, wrap) belongs to the scene
/// layer, not here.
void draw(Canvas& canvas, std::string_view utf8, const Rect& box, const TextStyle& style);

}  // namespace text
}  // namespace notrix
