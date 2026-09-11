// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace notrix {
namespace text {

/// Tallest glyph a font may declare. Covers the blueprint §10 tiers up to 8x8.
/// Taller proportional faces (the "variable 8-12" tier) need out-of-line row
/// storage and will get their own decision record.
inline constexpr int kMaxGlyphRows = 8;

/// One character cell.
///
/// Rows are stored one byte per scanline, top first. Within a row the bit for
/// column x is `cellWidth - 1 - x`, so a 5-wide cell authored as `0b01110` reads
/// left-to-right exactly as it renders. Glyphs narrower than the cell are
/// authored *left-aligned* — '!' in a 5-wide cell is `0b10000`, not `0b00100` —
/// which is what lets `width` double as the proportional advance.
///
/// `codepoint` is 16-bit: the Basic Multilingual Plane covers every script a
/// curated pixel-clock glyph pack will realistically carry, and halving the
/// field keeps the table small in flash.
struct Glyph {
    std::uint16_t codepoint;
    std::uint8_t width;
    std::uint8_t rows[kMaxGlyphRows];
};

/// An immutable bitmap font backed by a static, codepoint-sorted glyph table.
///
/// Holds only pointers to constant data, so a font costs nothing to copy and
/// lives entirely in flash on the device.
class BitmapFont {
public:
    constexpr BitmapFont(const char* name,
                         int cellWidth,
                         int height,
                         const Glyph* glyphs,
                         std::size_t glyphCount) noexcept
        : name_(name), cellWidth_(cellWidth), height_(height), glyphs_(glyphs),
          glyphCount_(glyphCount) {}

    constexpr const char* name() const noexcept { return name_; }
    constexpr int cellWidth() const noexcept { return cellWidth_; }
    constexpr int height() const noexcept { return height_; }

    /// Baseline-to-baseline distance for multi-line text: one blank row.
    constexpr int lineHeight() const noexcept { return height_ + 1; }

    constexpr std::size_t glyphCount() const noexcept { return glyphCount_; }

    /// Exact lookup. Returns nullptr when the font has no such glyph.
    const Glyph* find(char32_t codepoint) const noexcept;

    /// Lookup with fallback: the font's U+FFFD glyph if present, else the first
    /// glyph. Never returns nullptr for a non-empty font, so callers rendering
    /// arbitrary text never need a null check.
    const Glyph& glyphFor(char32_t codepoint) const noexcept;

    /// Is the pixel at (x, y) within this glyph's cell set?
    constexpr bool pixel(const Glyph& glyph, int x, int y) const noexcept {
        if (x < 0 || y < 0 || x >= glyph.width || y >= height_) {
            return false;
        }
        const int bit = cellWidth_ - 1 - x;
        return ((glyph.rows[y] >> bit) & 1u) != 0u;
    }

private:
    const char* name_;
    int cellWidth_;
    int height_;
    const Glyph* glyphs_;
    std::size_t glyphCount_;
};

/// The default 5x7 proportional face. ASCII, the degree sign, and a small set of
/// accented Latin lowercase. See Font5x7.cpp for the coverage caveats.
const BitmapFont& font5x7() noexcept;

}  // namespace text
}  // namespace notrix
