// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/apps/Unavailable.h"

#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/text/Text.h"

namespace stipple {
namespace apps {

void renderUnavailable(Canvas& canvas, std::string_view top, std::string_view bottom, Rgb color) {
    text::TextStyle style;
    style.font = &text::font5x7();
    style.color = color;
    style.hAlign = text::HAlign::Center;
    style.vAlign = text::VAlign::Middle;

    // Two bands of eight, for a font seven tall. The single pixel of slack per
    // line is what keeps the two words from touching.
    const int half = Framebuffer::kHeight / 2;
    text::draw(canvas, top, Rect{0, 0, Framebuffer::kWidth, half}, style);
    text::draw(canvas, bottom, Rect{0, half, Framebuffer::kWidth, half}, style);
}

}  // namespace apps
}  // namespace stipple
