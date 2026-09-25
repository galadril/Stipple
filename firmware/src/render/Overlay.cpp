// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/render/Overlay.h"

#include "stipple/graphics/Framebuffer.h"

namespace stipple {
namespace render {
namespace {

/// The rows type occupies (DESIGN.md §2): a single centred line runs 4–10.
/// Overlays stay out of it, which is most of what keeps them readable.
constexpr int kCentreTop = 4;
constexpr int kCentreBottom = 10;

bool inCentreBand(int y) noexcept { return y >= kCentreTop && y <= kCentreBottom; }

/// Additive: only ever lights a pixel the app left dark.
///
/// This is the rule that makes overlays safe. Without it, sparseness and
/// colour choice are just good intentions - one unlucky raindrop lands on the
/// stroke of a digit and the time is misread.
void lightIfDark(Framebuffer& frame, int x, int y, Rgb color) noexcept {
    if (!Framebuffer::inBounds(x, y) || inCentreBand(y)) {
        return;
    }
    if (frame.at(x, y) != colors::kBlack) {
        return;
    }
    frame.set(x, y, color);
}

/// A deterministic hash, so every overlay is a pure function of time.
///
/// std::rand would make these untestable and would drift between the emulator
/// and the device. This gives each column its own stable pseudo-random offset
/// and speed from nothing but its index.
constexpr std::uint32_t scramble(std::uint32_t v) noexcept {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

/// Falling streaks. Rain is fast and near-vertical; snow is slow and drifts.
void drawFalling(Framebuffer& frame, std::uint64_t elapsedMillis, Rgb color,
                 int columnStep, int speedBase, int speedSpread, int trail,
                 bool drift) noexcept {
    // Every few columns, not every column. A drop in each of 52 columns is a
    // curtain, and a curtain is exactly what DESIGN.md §7 forbids.
    for (int x = 0; x < Framebuffer::kWidth; x += columnStep) {
        const std::uint32_t seed = scramble(static_cast<std::uint32_t>(x));
        const int speed = speedBase + static_cast<int>(seed % static_cast<std::uint32_t>(speedSpread));
        const int phase = static_cast<int>((seed >> 8) % 64u);

        // Wraps over a span taller than the panel so drops do not all restart
        // together at the top.
        constexpr int kSpan = Framebuffer::kHeight + 8;
        const int travelled =
            static_cast<int>((elapsedMillis * static_cast<std::uint64_t>(speed)) / 1000u);
        const int head = ((travelled + phase) % kSpan) - 4;

        int column = x;
        if (drift) {
            // Snow sways. One column either side is the whole amplitude - more
            // and it stops reading as falling.
            const int sway = static_cast<int>(((elapsedMillis / 400u) + seed) % 3u) - 1;
            column += sway;
        }

        for (int t = 0; t < trail; ++t) {
            lightIfDark(frame, column, head - t, color);
        }
    }
}

/// A bright bar along one edge, on for a few frames every few seconds.
void drawStorm(Framebuffer& frame, std::uint64_t elapsedMillis, Rgb color) noexcept {
    // Rain underneath, so a storm reads as weather rather than as a fault.
    drawFalling(frame, elapsedMillis, color, 7, 26, 14, 3, false);

    // The flash is derived from time, not from a counter, so it is reproducible
    // in a test and identical in the emulator.
    constexpr std::uint64_t kPeriod = 3700;
    constexpr std::uint64_t kFlash = 90;
    const std::uint64_t phase = elapsedMillis % kPeriod;
    if (phase >= kFlash) {
        return;
    }

    // Top edge only. A full-screen flash on a panel in a dark room is
    // unpleasant rather than dramatic.
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
        lightIfDark(frame, x, 0, colors::kWhite);
        if (phase < kFlash / 2) {
            lightIfDark(frame, x, 1, color);
        }
    }
}

/// Static crystals creeping in from the corners.
void drawFrost(Framebuffer& frame, std::uint64_t elapsedMillis, Rgb color) noexcept {
    // Frost does not fall, it accumulates - so this breathes very slowly rather
    // than moving. Anything faster would read as noise.
    const std::uint64_t phase = (elapsedMillis / 900u) % 4u;

    for (int x = 0; x < Framebuffer::kWidth; ++x) {
        const std::uint32_t seed = scramble(static_cast<std::uint32_t>(x) * 2654435761u);

        // Depth falls off from the edges, so the middle of the panel stays clear
        // even in the rows overlays are allowed to use.
        const int edge = x < Framebuffer::kWidth / 2 ? x : Framebuffer::kWidth - 1 - x;
        const int reach = edge < 6 ? (3 - edge / 2) : 1;

        const int extra = static_cast<int>((seed + phase) % 2u);
        for (int d = 0; d < reach + extra; ++d) {
            lightIfDark(frame, x, d, color);
            lightIfDark(frame, x, Framebuffer::kHeight - 1 - d, color);
        }
    }
}


/// Points that brighten and fade on their own slow cycle.
///
/// The only overlay that never moves. Stars that drifted would be a clock
/// tumbling through space, which is a different and much busier idea.
void drawStars(Framebuffer& frame, std::uint64_t elapsedMillis, Rgb color) noexcept {
    constexpr int kCount = 14;
    for (int i = 0; i < kCount; ++i) {
        const std::uint32_t seed = scramble(static_cast<std::uint32_t>(i) * 2246822519u);
        const int x = static_cast<int>(seed % static_cast<std::uint32_t>(Framebuffer::kWidth));
        const int y = static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(Framebuffer::kHeight));

        // Each star keeps its own period, so they never pulse in unison - which
        // would read as the panel flickering rather than as a sky.
        const std::uint64_t period = 1700u + (seed >> 16) % 2300u;
        const std::uint64_t phase = (elapsedMillis + (seed >> 4) % period) % period;

        // Lit for rather less than half its cycle: a sky where every star is
        // always on is a grid of dots.
        if (phase * 3u > period) {
            continue;
        }
        const bool bright = phase * 9u < period;
        lightIfDark(frame, x, y, bright ? color : scale(color, 110));
    }
}

/// Dim horizontal bands drifting sideways.
void drawFog(Framebuffer& frame, std::uint64_t elapsedMillis, Rgb color) noexcept {
    const Rgb dim = scale(color, 90);

    // Only the rows above and below the type band, which for fog is most of the
    // effect: it gathers at the edges and leaves the middle legible.
    const int rows[] = {0, 1, 2, 3, 11, 12, 13, 14, 15};
    for (int r = 0; r < static_cast<int>(sizeof(rows) / sizeof(rows[0])); ++r) {
        const int y = rows[r];
        const std::uint32_t seed = scramble(static_cast<std::uint32_t>(y) * 374761393u);

        // Each band drifts at its own speed and direction, which is what stops
        // it looking like a scrolling texture.
        const int speed = 3 + static_cast<int>(seed % 5u);
        const bool leftward = (seed & 0x100u) != 0;
        const int shift =
            static_cast<int>((elapsedMillis * static_cast<std::uint64_t>(speed)) / 1000u);

        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const int sampled = leftward ? x + shift : x - shift;
            const std::uint32_t here =
                scramble(static_cast<std::uint32_t>(sampled & 0x3f) ^ seed);
            // Sparse: roughly one pixel in three, so it reads as haze rather
            // than as a solid bar.
            if (here % 3u != 0u) {
                continue;
            }
            lightIfDark(frame, x, y, dim);
        }
    }
}

/// Brief bright glints.
void drawSparkle(Framebuffer& frame, std::uint64_t elapsedMillis, Rgb color) noexcept {
    constexpr int kCount = 10;
    for (int i = 0; i < kCount; ++i) {
        const std::uint32_t seed = scramble(static_cast<std::uint32_t>(i) * 2654435761u);

        // Each glint occupies a slot of time and jumps somewhere new for the
        // next one, so the position is a function of which slot we are in.
        const std::uint64_t period = 900u + (seed % 1400u);
        const std::uint64_t slot = elapsedMillis / period;
        const std::uint64_t phase = elapsedMillis % period;

        // On for a tenth of its slot. A sparkle that lingers is a dot.
        if (phase * 10u > period) {
            continue;
        }

        const std::uint32_t placed = scramble(seed ^ static_cast<std::uint32_t>(slot));
        const int x = static_cast<int>(placed % static_cast<std::uint32_t>(Framebuffer::kWidth));
        const int y = static_cast<int>((placed >> 8) % static_cast<std::uint32_t>(Framebuffer::kHeight));

        lightIfDark(frame, x, y, colors::kWhite);
        // A single cross arm, so it glints rather than blinks.
        lightIfDark(frame, x - 1, y, color);
        lightIfDark(frame, x + 1, y, color);
    }
}

/// Tumbling coloured flecks.
void drawConfetti(Framebuffer& frame, std::uint64_t elapsedMillis) noexcept {
    // Its own palette, ignoring OverlayStyle::color. Grey confetti is not
    // confetti, and this is the one overlay whose entire point is colour.
    static constexpr Rgb kColors[] = {
        rgb(230, 70, 90), rgb(240, 180, 40), rgb(80, 200, 120),
        rgb(90, 150, 240), rgb(200, 110, 220),
    };
    constexpr int kPalette = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));

    for (int x = 0; x < Framebuffer::kWidth; x += 5) {
        const std::uint32_t seed = scramble(static_cast<std::uint32_t>(x) * 40503u);
        const int speed = 9 + static_cast<int>(seed % 11u);
        const int phase = static_cast<int>((seed >> 8) % 64u);

        constexpr int kSpan = Framebuffer::kHeight + 8;
        const int travelled =
            static_cast<int>((elapsedMillis * static_cast<std::uint64_t>(speed)) / 1000u);
        const int head = ((travelled + phase) % kSpan) - 4;

        // Tumbling: the sideways offset changes as it falls, so a fleck flutters
        // instead of dropping like a stone.
        const int flutter = static_cast<int>(((static_cast<std::uint32_t>(head) + seed) / 2u) % 3u) - 1;

        const Rgb color = kColors[(seed >> 3) % static_cast<std::uint32_t>(kPalette)];
        lightIfDark(frame, x + flutter, head, color);
    }
}

}  // namespace

Overlay overlayFromName(std::string_view name) noexcept {
    if (name == "rain") return Overlay::Rain;
    if (name == "snow") return Overlay::Snow;
    if (name == "storm") return Overlay::Storm;
    if (name == "frost") return Overlay::Frost;
    if (name == "stars") return Overlay::Stars;
    if (name == "fog") return Overlay::Fog;
    if (name == "sparkle") return Overlay::Sparkle;
    if (name == "confetti") return Overlay::Confetti;
    return Overlay::None;
}

const char* overlayName(Overlay overlay) noexcept {
    switch (overlay) {
        case Overlay::Rain: return "rain";
        case Overlay::Snow: return "snow";
        case Overlay::Storm: return "storm";
        case Overlay::Frost: return "frost";
        case Overlay::Stars: return "stars";
        case Overlay::Fog: return "fog";
        case Overlay::Sparkle: return "sparkle";
        case Overlay::Confetti: return "confetti";
        case Overlay::None: break;
    }
    return "none";
}

Overlay overlayAt(int index) noexcept {
    switch (index) {
        case 1: return Overlay::Rain;
        case 2: return Overlay::Snow;
        case 3: return Overlay::Storm;
        case 4: return Overlay::Frost;
        case 5: return Overlay::Stars;
        case 6: return Overlay::Fog;
        case 7: return Overlay::Sparkle;
        case 8: return Overlay::Confetti;
        default: return Overlay::None;
    }
}

void drawOverlay(Framebuffer& frame,
                 Overlay overlay,
                 std::uint64_t elapsedMillis,
                 const OverlayStyle& style) {
    if (overlay == Overlay::None) {
        return;
    }

    const Rgb color = scale(style.color, style.intensity);

    switch (overlay) {
        case Overlay::Rain:
            // Fast, near-vertical, short trail.
            drawFalling(frame, elapsedMillis, color, 6, 30, 16, 3, false);
            return;
        case Overlay::Snow:
            // Slow, drifting, single flakes rather than streaks.
            drawFalling(frame, elapsedMillis, color, 8, 6, 5, 1, true);
            return;
        case Overlay::Storm:
            drawStorm(frame, elapsedMillis, color);
            return;
        case Overlay::Frost:
            drawFrost(frame, elapsedMillis, color);
            return;
        case Overlay::Stars:
            drawStars(frame, elapsedMillis, color);
            return;
        case Overlay::Fog:
            drawFog(frame, elapsedMillis, color);
            return;
        case Overlay::Sparkle:
            drawSparkle(frame, elapsedMillis, color);
            return;
        case Overlay::Confetti:
            drawConfetti(frame, elapsedMillis);
            return;
        case Overlay::None:
            return;
    }
}

}  // namespace render
}  // namespace stipple
