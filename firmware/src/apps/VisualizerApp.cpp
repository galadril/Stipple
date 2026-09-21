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
///
/// Measured against *levels*, which are amplitudes with the noise floor taken
/// off - so this is much smaller than the raw reading of a quiet room.
constexpr int kMinCeiling = 300;

/// How fast the noise floor is allowed to climb, in units per sample.
///
/// The TC002's microphone reports a few hundred in a silent room: a DC offset
/// and self-noise, not sound. Scaling that against the gain window made an
/// empty room animate constantly, and the first handclap - which threw the
/// window up where it belonged - looked like the visualiser "resetting" when
/// it was actually the only moment it had been right.
///
/// The floor drops instantly to any quieter sample and rises only at this
/// crawl, so it settles on the quietest thing recently heard rather than
/// chasing the signal. Rising at all matters for a room that genuinely gets
/// noisier - a fan switched on should become the new nothing, eventually.
constexpr int kFloorRisePerSample = 1;

/// How fast the window closes back down, in thousandths per sample. Slow
/// enough that the tail of a loud passage stays comparable to its start;
/// fast enough that a room returning to quiet becomes legible within a few
/// seconds rather than a minute.
constexpr int kDecayPermille = 988;

/// How far the window moves toward a louder sample, in thousandths. Not all
/// the way: a single finger snap that set the scale outright would make the
/// next few seconds of ordinary sound read as silence, which is exactly the
/// complaint this replaced.
constexpr int kAttackPermille = 250;

/// How fast the meter's peak marker falls, in thousandths per sample.
///
/// Slow enough to see where a sound reached after it has gone, fast enough
/// that the marker follows the room rather than recording its record.
constexpr int kPeakFallPermille = 18;

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

VisualizerStyleKind visualizerStyleFromName(std::string_view name) noexcept {
    if (name == "trace") {
        return VisualizerStyleKind::Trace;
    }
    return VisualizerStyleKind::Meter;
}

const char* visualizerStyleName(VisualizerStyleKind kind) noexcept {
    return kind == VisualizerStyleKind::Trace ? "trace" : "meter";
}

void Visualizer::push(int amplitude) noexcept {
    if (amplitude < 0) {
        amplitude = 0;
    }
    if (amplitude > 32767) {
        amplitude = 32767;
    }

    // Track the quietest thing recently heard, and measure everything from
    // there. floor_ starts at the top of the range so the very first sample
    // defines it and the panel opens silent rather than opening excited.
    if (amplitude < floor_) {
        floor_ = amplitude;
    } else if (floor_ < amplitude) {
        floor_ += kFloorRisePerSample;
        if (floor_ > amplitude) {
            floor_ = amplitude;
        }
    }

    const int level = amplitude - floor_;

    // Scaled against the window as it stands *before* this sample moves it.
    //
    // Two consequences, both wanted. A sudden loud sound is drawn at full
    // height on the frame it arrives, which is what "it should show when it
    // hears something" means. And because the result is stored rather than
    // recomputed later, raising the window afterwards cannot reach back and
    // shrink it.
    int permille = ceiling_ > 0 ? (level * 1000) / ceiling_ : 0;
    if (permille > 1000) {
        permille = 1000;
    }

    if (permille > peakHold_) {
        peakHold_ = permille;
    } else {
        peakHold_ -= kPeakFallPermille;
        if (peakHold_ < permille) {
            peakHold_ = permille;
        }
    }

    levels_[head_] = static_cast<std::uint16_t>(permille);
    head_ = (head_ + 1) % kColumns;
    if (filled_ < kColumns) {
        ++filled_;
    }

    if (level > ceiling_) {
        // Toward the peak rather than onto it. Landing on it exactly would let
        // a single snap set the scale for the next several seconds, so
        // everything after it reads as silence.
        ceiling_ += ((level - ceiling_) * kAttackPermille) / 1000;
    } else {
        ceiling_ = (ceiling_ * kDecayPermille) / 1000;
        if (ceiling_ < kMinCeiling) {
            ceiling_ = kMinCeiling;
        }
    }
    if (ceiling_ > 32767) {
        ceiling_ = 32767;
    }
}

int Visualizer::currentPermille() const noexcept {
    if (filled_ == 0) {
        return 0;
    }
    const int newest = ((head_ - 1) % kColumns + kColumns) % kColumns;
    return levels_[newest];
}

/// The meter: a block rising from the bottom, with a peak marker above it.
///
/// Asked for by the person living with the device, and right for the room it
/// sits in - a clock on a shelf should be still when the room is still. The
/// trace below is in motion whenever there is any sound at all, which is
/// legible and tiring.
///
/// Still one number honestly drawn. The width carries no information, which is
/// why the block is solid rather than split into bands: bands would be a
/// picture of a spectrum this hardware cannot measure.
namespace {

void renderMeter(Canvas& canvas, int permille, int peak, const VisualizerStyle& style) {
    constexpr int kHeight = Framebuffer::kHeight;

    // A floor line, so a silent room reads as silent rather than as a dead
    // panel - the same job the trace's centre line does.
    canvas.fillRect(Rect{0, kHeight - 1, Framebuffer::kWidth, 1}, style.baseline);

    int height = (permille * kHeight) / 1000;
    if (height == 0 && permille > 0) {
        height = 1;
    }
    if (height > kHeight) {
        height = kHeight;
    }

    for (int i = 0; i < height; ++i) {
        const int y = kHeight - 1 - i;
        // Coloured by how high the row is, not by the level as a whole, so the
        // top of a loud sound is red while its base stays blue - the gradient
        // is the scale, and it does not move.
        const int rowPermille = ((i + 1) * 1000) / kHeight;
        const Rgb colour = rowPermille >= style.peakPermille
                               ? style.peak
                               : mix(style.quiet, style.loud,
                                     (rowPermille * 1000) / style.peakPermille);
        canvas.fillRect(Rect{0, y, Framebuffer::kWidth, 1}, colour);
    }

    // The marker, only where it is not already part of the block.
    int peakRow = (peak * kHeight) / 1000;
    if (peakRow > kHeight) {
        peakRow = kHeight;
    }
    if (peakRow > height) {
        canvas.fillRect(Rect{0, kHeight - peakRow, Framebuffer::kWidth, 1}, style.peak);
    }
}

}  // namespace

void Visualizer::render(Canvas& canvas, const VisualizerStyle& style) const {
    if (style.kind == VisualizerStyleKind::Meter) {
        renderMeter(canvas, currentPermille(), peakHold_, style);
        return;
    }

    const int centre = kHalf - 1;

    // Drawn first and always, so silence looks like silence rather than like a
    // dead panel.
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
        const int permille = levels_[index];

        int height = (permille * kHalf) / 1000;

        // Any sound at all lights something. A column that rounded to zero
        // would be indistinguishable from silence, and at this size that is
        // most of the quiet end of the range.
        if (height == 0 && permille > 0) {
            height = 1;
        }
        if (height > kHalf) {
            height = kHalf;
        }

        // Coloured by this column's own level, so the shape of a sound stays
        // readable as it scrolls away rather than being recoloured by whatever
        // is happening now.
        const Rgb colour =
            permille >= style.peakPermille
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
