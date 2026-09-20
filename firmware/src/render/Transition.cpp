// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/render/Transition.h"

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

}  // namespace

TransitionStyle transitionStyleFromName(std::string_view name) noexcept {
    if (name == "slide") return TransitionStyle::Slide;
    if (name == "fade") return TransitionStyle::Fade;
    return TransitionStyle::None;
}

const char* transitionStyleName(TransitionStyle style) noexcept {
    switch (style) {
        case TransitionStyle::Slide: return "slide";
        case TransitionStyle::Fade: return "fade";
        case TransitionStyle::None: break;
    }
    return "none";
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
