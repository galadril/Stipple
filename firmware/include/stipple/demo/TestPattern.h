// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace stipple {

class Canvas;

namespace demo {

/// The Stage 1 bring-up pattern (blueprint §52: "render test pattern").
///
/// Deliberately exercises every Canvas primitive — outline, clipped diagonals,
/// per-column gradient, and an animated scan bar — so that a single glance tells
/// you whether geometry, clipping, colour order and frame pacing are all intact.
/// It is the first thing the emulator shows, and it will be the first thing the
/// real TC002 shows once hardware arrives.
///
/// `frame` is a monotonically increasing frame counter; the output is a pure
/// function of it, which is what makes the golden-image tests deterministic.
void drawTestPattern(Canvas& canvas, int frame);

}  // namespace demo
}  // namespace stipple
