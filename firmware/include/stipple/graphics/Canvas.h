// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "stipple/core/Geometry.h"
#include "stipple/core/Rgb.h"
#include "stipple/graphics/Framebuffer.h"

namespace stipple {

/// A non-owning view of pixel data used as a blit source (icons, glyph atlases,
/// sprites). Holds a borrowed pointer, so the caller keeps the data alive.
struct BitmapView {
    const Rgb* pixels = nullptr;
    int width = 0;
    int height = 0;

    constexpr bool valid() const noexcept { return pixels != nullptr && width > 0 && height > 0; }
};

/// Drawing primitives over a Framebuffer (blueprint §9.2).
///
/// Every operation is clipped against the current clip rect, which can only ever
/// shrink as you nest ClipScopes. That is the mechanism behind the §9.3 promise
/// that one component cannot corrupt another's region: a misbehaving app cannot
/// draw outside the rect it was handed, no matter what coordinates it passes.
///
/// Nothing here allocates. Canvas is a thin stack-lived handle; construct one
/// per draw pass rather than storing it.
class Canvas {
public:
    explicit Canvas(Framebuffer& target) noexcept : target_(target), clip_(Framebuffer::bounds()) {}

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    // --- clipping ---------------------------------------------------------

    Rect clip() const noexcept { return clip_; }

    /// Replace the clip rect. The result is always within the framebuffer, so
    /// widening past the panel is silently impossible.
    void setClip(const Rect& r) noexcept { clip_ = intersect(r, Framebuffer::bounds()); }

    void resetClip() noexcept { clip_ = Framebuffer::bounds(); }

    // --- primitives -------------------------------------------------------

    /// Fill the current clip region. With no clip set this clears the panel.
    void clear(Rgb color = colors::kBlack) noexcept;

    void pixel(int x, int y, Rgb color) noexcept;

    void hLine(int x, int y, int width, Rgb color) noexcept;
    void vLine(int x, int y, int height, Rgb color) noexcept;

    void line(int x0, int y0, int x1, int y1, Rgb color) noexcept;

    /// One-pixel outline; `fillRect` fills the interior too.
    void rect(const Rect& r, Rgb color) noexcept;
    void fillRect(const Rect& r, Rgb color) noexcept;

    void blit(int x, int y, const BitmapView& bitmap) noexcept;

    /// Blit skipping every source pixel equal to `transparent` — the usual mode
    /// for icons drawn over app content.
    void blitKeyed(int x, int y, const BitmapView& bitmap, Rgb transparent) noexcept;

    Framebuffer& target() noexcept { return target_; }
    const Framebuffer& target() const noexcept { return target_; }

private:
    Framebuffer& target_;
    Rect clip_;
};

/// RAII clip narrowing. The new clip is intersected with the existing one, so a
/// nested scope can never escape its parent, and the previous rect is restored
/// on destruction.
///
///     ClipScope scope(canvas, Rect{11, 0, 40, 8});
///     canvas.clear();            // only touches those 40x8 pixels
class ClipScope {
public:
    ClipScope(Canvas& canvas, const Rect& r) noexcept : canvas_(canvas), saved_(canvas.clip()) {
        canvas_.setClip(intersect(r, saved_));
    }

    ~ClipScope() noexcept { canvas_.setClip(saved_); }

    ClipScope(const ClipScope&) = delete;
    ClipScope& operator=(const ClipScope&) = delete;
    ClipScope(ClipScope&&) = delete;
    ClipScope& operator=(ClipScope&&) = delete;

private:
    Canvas& canvas_;
    Rect saved_;
};

}  // namespace stipple
