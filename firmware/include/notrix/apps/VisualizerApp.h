// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/core/Rgb.h"
#include "notrix/graphics/Framebuffer.h"

namespace notrix {

class Canvas;

namespace apps {

struct VisualizerStyle {
    /// Quiet, loud, and peak. The bar is coloured by how loud *that column*
    /// was, not by where it sits, so a burst stays red as it scrolls away and
    /// you can read the shape of a sound after it has happened.
    Rgb quiet = rgb(0, 120, 200);
    Rgb loud = rgb(255, 200, 0);
    Rgb peak = rgb(255, 40, 60);

    /// The centre line, visible when the room is silent. Without it a quiet
    /// room renders an empty panel, which reads as broken rather than quiet.
    Rgb baseline = rgb(20, 28, 40);

    /// Above this fraction of full height, a column is drawn in `peak`.
    /// Thousandths, to stay in integer arithmetic.
    int peakPermille = 780;
};

/// A scrolling level history, mirrored about the centre.
///
/// **Not a spectrum analyser, deliberately.** The TC002 reports a single
/// amplitude about twenty times a second and nothing else - there are no bands
/// to draw, and faking them by splitting one number into columns would be a
/// picture of nothing. What this shows instead is real: the last 52 samples,
/// newest at the right, which at ~22 Hz is about two and a half seconds of
/// sound. You can see the shape of a word after it is said.
///
/// Auto-gain lives here rather than in the platform, because what counts as
/// loud depends on the room and an adapter cannot know that. The window follows
/// a decaying peak, so a quiet room still fills the panel and a shout does not
/// clip to a flat bar.
class Visualizer {
public:
    /// One column per sample, so the history is exactly as wide as the panel.
    static constexpr int kColumns = Framebuffer::kWidth;

    /// Feed one reading. Call at whatever rate samples arrive; the renderer is
    /// independent of it.
    void push(int amplitude) noexcept;

    /// True once any sample has arrived. Until then the app says it is
    /// listening rather than drawing a flatline that looks like silence.
    bool hasSamples() const noexcept { return filled_ > 0; }

    void render(Canvas& canvas, const VisualizerStyle& style = VisualizerStyle{}) const;

    /// The gain window currently in use, for diagnostics and tests.
    int ceiling() const noexcept { return ceiling_; }

private:
    /// Ring buffer, oldest overwritten. No allocation, bounded by construction.
    std::uint16_t samples_[kColumns] = {};
    int head_ = 0;
    int filled_ = 0;

    /// Decaying peak. Starts low so a quiet room is legible immediately, and
    /// rises instantly to any louder sample so a clap never clips.
    int ceiling_ = 600;
};

/// Drawn when the platform has no microphone, so the app says why rather than
/// showing a flatline that is indistinguishable from a silent room.
void renderNoMicrophone(Canvas& canvas, Rgb color);

}  // namespace apps
}  // namespace notrix
