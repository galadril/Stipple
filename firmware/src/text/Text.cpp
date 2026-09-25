// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/text/Text.h"

#include "stipple/graphics/Canvas.h"
#include "stipple/text/Utf8.h"

namespace stipple {
namespace text {
namespace {

/// Strip one trailing '\r' so CRLF input measures the same as LF input.
std::string_view trimCarriageReturn(std::string_view line) noexcept {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

/// Next '\n'-delimited line starting at `offset`. `offset` is advanced past the
/// separator. Returns false once the input is exhausted.
bool nextLine(std::string_view text, std::size_t& offset, std::string_view& line) noexcept {
    if (offset > text.size()) {
        return false;
    }
    const std::size_t newline = text.find('\n', offset);
    if (newline == std::string_view::npos) {
        line = trimCarriageReturn(text.substr(offset));
        offset = text.size() + 1;  // past the end: stop on the next call
        return true;
    }
    line = trimCarriageReturn(text.substr(offset, newline - offset));
    offset = newline + 1;
    return true;
}

}  // namespace

int measureLine(std::string_view utf8, const BitmapFont& font, int letterSpacing) noexcept {
    int width = 0;
    bool first = true;

    std::size_t offset = 0;
    while (offset < utf8.size()) {
        const DecodedChar decoded = decodeUtf8(utf8, offset);
        offset += decoded.size;

        const Glyph& glyph = font.glyphFor(decoded.codepoint);
        if (!first) {
            width += letterSpacing;
        }
        width += glyph.width;
        first = false;
    }
    return width;
}

TextMetrics measure(std::string_view utf8,
                    const BitmapFont& font,
                    int letterSpacing,
                    int lineSpacing) noexcept {
    TextMetrics metrics;

    std::size_t offset = 0;
    std::string_view line;
    while (nextLine(utf8, offset, line)) {
        const int lineWidth = measureLine(line, font, letterSpacing);
        if (lineWidth > metrics.width) {
            metrics.width = lineWidth;
        }
        ++metrics.lineCount;
    }

    if (metrics.lineCount > 0) {
        metrics.height =
            metrics.lineCount * font.height() + (metrics.lineCount - 1) * lineSpacing;
    }
    return metrics;
}

int drawLine(Canvas& canvas,
             std::string_view utf8,
             int x,
             int y,
             const BitmapFont& font,
             Rgb color,
             int letterSpacing) {
    const int startX = x;
    bool first = true;

    std::size_t offset = 0;
    while (offset < utf8.size()) {
        const DecodedChar decoded = decodeUtf8(utf8, offset);
        offset += decoded.size;

        const Glyph& glyph = font.glyphFor(decoded.codepoint);
        if (!first) {
            x += letterSpacing;
        }
        first = false;

        for (int row = 0; row < font.height(); ++row) {
            for (int column = 0; column < glyph.width; ++column) {
                if (font.pixel(glyph, column, row)) {
                    canvas.pixel(x + column, y + row, color);
                }
            }
        }
        x += glyph.width;
    }

    return x - startX;
}

void draw(Canvas& canvas, std::string_view utf8, const Rect& box, const TextStyle& style) {
    const BitmapFont& font = style.font != nullptr ? *style.font : font5x7();

    const TextMetrics metrics = measure(utf8, font, style.letterSpacing, style.lineSpacing);
    if (metrics.lineCount == 0) {
        return;
    }

    int y = box.y;
    switch (style.vAlign) {
        case VAlign::Top:
            break;
        case VAlign::Middle:
            y = box.y + (box.h - metrics.height) / 2;
            break;
        case VAlign::Bottom:
            y = box.y + box.h - metrics.height;
            break;
    }

    // Nothing drawn here can escape the box, however the text is aligned or
    // however long a line turns out to be.
    ClipScope scope(canvas, box);

    std::size_t offset = 0;
    std::string_view line;
    while (nextLine(utf8, offset, line)) {
        const int lineWidth = measureLine(line, font, style.letterSpacing);

        int x = box.x;
        switch (style.hAlign) {
            case HAlign::Left:
                break;
            case HAlign::Center:
                x = box.x + (box.w - lineWidth) / 2;
                break;
            case HAlign::Right:
                x = box.x + box.w - lineWidth;
                break;
        }

        drawLine(canvas, line, x, y, font, style.color, style.letterSpacing);
        y += font.height() + style.lineSpacing;
    }
}

}  // namespace text
}  // namespace stipple
