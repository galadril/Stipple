// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/VisualizerApp.h"

#include "notrix/graphics/Canvas.h"
#include "notrix/text/Text.h"

namespace notrix {
namespace apps {
namespace {

/// Half the panel, less the centre row the baseline occupies.
constexpr int kHalf = Framebuffer::kHeight / 2;

/// Never let the window collapse. Without a floor, a genuinely silent room
/// drives the gain to nothing and the first quiet sound fills the panel.
constexpr int kMinCeiling = 400;

/// How fast the window closes back down, in thousandths per sample. Slow
/// enough that the tail of a loud passage stays comparable to its start;
/// fast enough that a room returning to quiet becomes legible within a few
/// seconds rather than a minute.
constexpr int kDecayPermille = 988;

/// Blend two colours by a 0-1000 weight. Integer only: this runs per column,
/// per frame, on a Cortex-A7.
Rgb mix(Rgb from, Rgb to, int permille) noexcept {
    if (permille < 0) { permille = 0; }
    if (permille > 1000) { permille = 1000; }
    const int inverse = 1000 - permille;
    return rgb((from.r * inverse + to.r * permille) / 1000,
               (from.g * inverse + to.g * permille) / 1000,
               (from.b * inverse + to.b * permille) / 1000);
}

}  // namespace

void Visualizer::push(int amplitude) noexcept {
    if (amplitude < 0) {
        amplitude = 0;
    }
    if (amplitude > 32767) {
        amplitude = 32767;
    }

    samples_[head_] = static_cast<std::uint16_t>(amplitude);
    head_ = (head_ + 1) % kColumns;
    if (filled_ < kColumns) {
        ++filled_;
    }

    // Rise instantly, fall slowly. A clap must not clip on the frame it
    // arrives, and the window must not slam shut the moment it ends.
    if (amplitude > ceiling_) {
        ceiling_ = amplitude;
    } else {
        ceiling_ = (ceiling_ * kDecayPermille) / 1000;
        if (ceiling_ < kMinCeiling) {
            ceiling_ = kMinCeiling;
        }
    }
}

void Visualizer::render(Canvas& canvas, const VisualizerStyle& style) const {
    const int centre = kHalf - 1;

    // The baseline is drawn first and always, so silence looks like silence
    // rather than like a dead panel.
    canvas.fillRect(Rect{0, centre, Framebuffer::kWidth, 2}, style.baseline);

    if (filled_ == 0) {
        return;
    }

    for (int column = 0; column < kColumns; ++column) {
        // Newest on the right: walk back from head_ so the history scrolls the
        // way reading does.
        const int age = kColumns - 1 - column;
        if (age >= filled_) {
            continue;
        }
        const int index = ((head_ - 1 - age) % kColumns + kColumns) % kColumns;
        const int amplitude = samples_[index];

        // Scaled against the decaying window rather than against 32767, which
        // is what makes a normal room fill the panel at all.
        int permille = ceiling_ > 0 ? (amplitude * 1000) / ceiling_ : 0;
        if (permille > 1000) {
            permille = 1000;
        }

        int height = (permille * kHalf) / 1000;

        // Any sound at all lights something. A column that rounded to zero
        // would be indistinguishable from silence, and at this size that is
        // most of the quiet end of the range.
        if (height == 0 && amplitude > 0) {
            height = 1;
        }
        if (height > kHalf) {
            height = kHalf;
        }

        // Coloured by this column's own level, so the shape of a sound stays
        // readable as it scrolls away rather than being recoloured by whatever
        // is happening now.
        const Rgb colour = permille >= style.peakPermille
                               ? style.peak
                               : mix(style.quiet, style.loud, (permille * 1000) / style.peakPermille);

        for (int i = 0; i < height; ++i) {
            canvas.pixel(column, centre - i, colour);
            canvas.pixel(column, centre + 1 + i, colour);
        }
    }
}

void renderNoMicrophone(Canvas& canvas, Rgb color) {
    text::TextStyle style;
    style.font = &text::font5x7();
    style.color = color;
    style.hAlign = text::HAlign::Center;
    style.vAlign = text::VAlign::Middle;
    text::draw(canvas, "NO MIC", Framebuffer::bounds(), style);
}

}  // namespace apps
}  // namespace notrix
