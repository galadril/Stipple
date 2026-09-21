// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "notrix/core/Rgb.h"
#include "notrix/graphics/Framebuffer.h"

namespace notrix {

class Canvas;

namespace apps {

/// How the level is drawn.
///
/// Both are honest about the same single number - what differs is whether the
/// panel shows the last few seconds or only now.
enum class VisualizerStyleKind : std::uint8_t {
    /// A level meter rising from the bottom, with a peak marker that hangs and
    /// falls. Nothing scrolls, so the panel is calm when the room is; a clock
    /// on a shelf should not be moving constantly.
    Meter,
    /// The scrolling history, mirrored about the centre. Shows the shape of a
    /// word after it is said, at the cost of being in motion whenever there is
    /// any sound at all.
    Trace,
};

/// Parse a stored style name. Unknown names fall back to the default rather
/// than failing, so a config written by a newer build still loads.
VisualizerStyleKind visualizerStyleFromName(std::string_view name) noexcept;

/// The stable name for a style, for the API and the config file.
const char* visualizerStyleName(VisualizerStyleKind kind) noexcept;

struct VisualizerStyle {
    VisualizerStyleKind kind = VisualizerStyleKind::Meter;

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

    /// The newest level, 0-1000. Silence until something has been heard.
    int currentPermille() const noexcept;

    /// The highest level seen recently, 0-1000. Falls back toward the current
    /// level so the marker settles rather than pinning at the loudest thing
    /// that ever happened.
    int peakPermille() const noexcept { return peakHold_; }

    /// True once any sample has arrived. Until then the app says it is
    /// listening rather than drawing a flatline that looks like silence.
    bool hasSamples() const noexcept { return filled_ > 0; }

    void render(Canvas& canvas, const VisualizerStyle& style = VisualizerStyle{}) const;

    /// The gain window currently in use, for diagnostics and tests.
    int ceiling() const noexcept { return ceiling_; }

private:
    /// Ring buffer of *already scaled* levels, 0-1000, oldest overwritten.
    ///
    /// Scaled at capture rather than at render, and that is the whole fix for
    /// the behaviour this originally had: storing raw amplitudes and dividing
    /// the history by the current window meant one finger snap raised the
    /// window and shrank every column already on screen. The trace appeared to
    /// reset at the exact moment it should have reacted.
    ///
    /// A column is now decided once, from the window as it stood when that
    /// sound happened, and never changes again. What is on screen is a record,
    /// not a recomputation.
    std::uint16_t levels_[kColumns] = {};
    int head_ = 0;
    int filled_ = 0;

    /// Peak-hold for the meter: rises instantly, falls a little each sample.
    /// Without the hold, a transient is drawn for one frame and missed; with a
    /// hold that never falls, the marker is a high-water mark from an hour ago.
    int peakHold_ = 0;

    /// Decaying peak. Starts low so a quiet room is legible immediately,
    /// rises part-way toward a louder sample rather than onto it, and falls
    /// slowly. Landing exactly on a peak would let one snap set the scale for
    /// the next several seconds.
    int ceiling_ = 600;

    /// The quietest sample recently heard, subtracted from every reading.
    ///
    /// Starts at the top of the range so the first sample defines it and the
    /// panel opens silent. A microphone that reports a few hundred in an empty
    /// room - which this hardware does - otherwise animates constantly, and the
    /// first real sound looks like a reset rather than a response.
    int floor_ = 32767;
};

/// Drawn when the platform has no microphone, so the app says why rather than
/// showing a flatline that is indistinguishable from a silent room.
void renderNoMicrophone(Canvas& canvas, Rgb color);

}  // namespace apps
}  // namespace notrix
