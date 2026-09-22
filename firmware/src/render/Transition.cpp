// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/render/Transition.h"

#include <cstdint>

#include "notrix/core/Rgb.h"

namespace notrix {
namespace render {
namespace {

constexpr int kFull = 1000;

int clampPermille(int value) noexcept {
    if (value < 0) {
        return 0;
    }
    return value > kFull ? kFull : value;
}

/// 0-1000 to 0-255 without floating point. This runs 832 times a frame on a
/// Cortex-A7 with no FPU worth relying on.
std::uint8_t toFactor(int permille) noexcept {
    return static_cast<std::uint8_t>((permille * 255 + kFull / 2) / kFull);
}

void slide(Framebuffer& out, const Framebuffer& from, const Framebuffer& to,
           TransitionDirection direction, int permille) noexcept {
    // How far the outgoing frame has travelled. At 1000 it has left entirely.
    const int shift = (permille * Framebuffer::kWidth + kFull / 2) / kFull;

    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            // Forward: content moves left, so column x shows what used to be at
            // x + shift, and the incoming frame fills in from the right.
            const int source = (direction == TransitionDirection::Forward)
                                   ? x + shift
                                   : x - shift;

            if (source >= 0 && source < Framebuffer::kWidth) {
                out.set(x, y, from.at(source, y));
            } else {
                // Wrapped past the edge: the incoming frame, offset so its own
                // left edge arrives exactly as the outgoing one leaves.
                const int incoming = (direction == TransitionDirection::Forward)
                                         ? source - Framebuffer::kWidth
                                         : source + Framebuffer::kWidth;
                out.set(x, y, to.at(incoming, y));
            }
        }
    }
}

void fade(Framebuffer& out, const Framebuffer& from, const Framebuffer& to,
          int permille) noexcept {
    // Through black rather than by blending the two. A pixel clock crossfade
    // that mixed the frames would spend the middle of every transition showing
    // two times superimposed, which is unreadable; going dark and coming back
    // reads as one thing replacing another.
    const bool leaving = permille < kFull / 2;
    const Framebuffer& source = leaving ? from : to;

    const int local = leaving ? (kFull - permille * 2) : (permille * 2 - kFull);
    const std::uint8_t factor = toFactor(clampPermille(local));

    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            out.set(x, y, scale(source.at(x, y), factor));
        }
    }
}


/// Reveals `to` column by column, travelling with the gesture.
///
/// Distinct from a slide, which moves both frames: here neither moves and the
/// boundary is a hard edge. On a panel this small that reads as decisive where
/// a slide reads as travel, which suits an app change the user asked for.
void wipe(Framebuffer& out,
          const Framebuffer& from,
          const Framebuffer& to,
          TransitionDirection direction,
          int permille) noexcept {
    const int revealed = (Framebuffer::kWidth * permille) / 1000;

    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            // Forward reveals from the right, following the content's travel in
            // a slide, so the two styles agree about which way "next" is.
            const bool shown = direction == TransitionDirection::Forward
                                   ? x >= Framebuffer::kWidth - revealed
                                   : x < revealed;
            out.set(x, y, shown ? to.at(x, y) : from.at(x, y));
        }
    }
}

/// Swaps pixels over in a fixed scattered order.
///
/// The order comes from a hash of the coordinate rather than a random number
/// generator: the whole transition has to be a pure function of progress, or it
/// would differ between the emulator and the device and could not be tested at
/// all (DESIGN.md §5.2).
void dissolve(Framebuffer& out,
              const Framebuffer& from,
              const Framebuffer& to,
              int permille) noexcept {
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            std::uint32_t v =
                static_cast<std::uint32_t>(y) * 52u + static_cast<std::uint32_t>(x);
            v ^= v >> 16;
            v *= 0x7feb352du;
            v ^= v >> 15;
            v *= 0x846ca68bu;
            v ^= v >> 16;

            // Each pixel gets its own threshold in 0-999 and flips when
            // progress passes it, so the panel dissolves evenly rather than in
            // visible bands.
            const int threshold = static_cast<int>(v % 1000u);
            out.set(x, y, permille > threshold ? to.at(x, y) : from.at(x, y));
        }
    }
}

}  // namespace

TransitionStyle transitionStyleFromName(std::string_view name) noexcept {
    if (name == "slide") return TransitionStyle::Slide;
    if (name == "fade") return TransitionStyle::Fade;
    if (name == "wipe") return TransitionStyle::Wipe;
    if (name == "dissolve") return TransitionStyle::Dissolve;
    return TransitionStyle::None;
}

const char* transitionStyleName(TransitionStyle style) noexcept {
    switch (style) {
        case TransitionStyle::Slide: return "slide";
        case TransitionStyle::Fade: return "fade";
        case TransitionStyle::Wipe: return "wipe";
        case TransitionStyle::Dissolve: return "dissolve";
        case TransitionStyle::None: break;
    }
    return "none";
}

TransitionStyle transitionStyleAt(int index) noexcept {
    switch (index) {
        case 1: return TransitionStyle::Slide;
        case 2: return TransitionStyle::Fade;
        case 3: return TransitionStyle::Wipe;
        case 4: return TransitionStyle::Dissolve;
        default: return TransitionStyle::None;
    }
}

void composite(Framebuffer& out,
               const Framebuffer& from,
               const Framebuffer& to,
               TransitionStyle style,
               TransitionDirection direction,
               int progressPermille) noexcept {
    const int permille = clampPermille(progressPermille);

    switch (style) {
        case TransitionStyle::Slide:
            slide(out, from, to, direction, permille);
            return;
        case TransitionStyle::Fade:
            fade(out, from, to, permille);
            return;
        case TransitionStyle::Wipe:
            wipe(out, from, to, direction, permille);
            return;
        case TransitionStyle::Dissolve:
            dissolve(out, from, to, permille);
            return;
        case TransitionStyle::None:
            break;
    }

    // None: the destination, immediately. Copied rather than skipped so the
    // caller never has to special-case which buffer holds the answer.
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            out.set(x, y, to.at(x, y));
        }
    }
}

}  // namespace render
}  // namespace notrix
