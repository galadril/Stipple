// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/graphics/Canvas.h"

#include <cstddef>

namespace notrix {
namespace {

constexpr int absInt(int v) noexcept {
    return v < 0 ? -v : v;
}

constexpr int minInt(int a, int b) noexcept {
    return a < b ? a : b;
}

}  // namespace

void Canvas::clear(Rgb color) noexcept {
    fillRect(clip_, color);
}

void Canvas::pixel(int x, int y, Rgb color) noexcept {
    if (!clip_.contains(x, y)) {
        return;
    }
    target_.set(x, y, color);
}

void Canvas::hLine(int x, int y, int width, Rgb color) noexcept {
    if (width <= 0) {
        return;
    }
    fillRect(Rect{x, y, width, 1}, color);
}

void Canvas::vLine(int x, int y, int height, Rgb color) noexcept {
    if (height <= 0) {
        return;
    }
    fillRect(Rect{x, y, 1, height}, color);
}

void Canvas::fillRect(const Rect& r, Rgb color) noexcept {
    const Rect area = intersect(r, clip_);
    if (area.empty()) {
        return;
    }
    for (int y = area.y; y < area.bottom(); ++y) {
        for (int x = area.x; x < area.right(); ++x) {
            target_.set(x, y, color);
        }
    }
}

void Canvas::rect(const Rect& r, Rgb color) noexcept {
    if (r.empty()) {
        return;
    }

    hLine(r.x, r.y, r.w, color);
    if (r.h > 1) {
        hLine(r.x, r.bottom() - 1, r.w, color);
    }
    if (r.h > 2) {
        const int sideHeight = r.h - 2;
        vLine(r.x, r.y + 1, sideHeight, color);
        if (r.w > 1) {
            vLine(r.right() - 1, r.y + 1, sideHeight, color);
        }
    }
}

void Canvas::line(int x0, int y0, int x1, int y1, Rgb color) noexcept {
    const int dx = absInt(x1 - x0);
    const int dy = absInt(y1 - y0);

    // A line never leaves its bounding box, so this rejects fully off-screen
    // lines without walking them pixel by pixel.
    const Rect boundingBox{minInt(x0, x1), minInt(y0, y1), dx + 1, dy + 1};
    if (intersect(boundingBox, clip_).empty()) {
        return;
    }

    // Integer Bresenham; each pixel is clipped individually by pixel().
    const int stepX = x0 < x1 ? 1 : -1;
    const int stepY = y0 < y1 ? 1 : -1;
    int error = dx - dy;

    for (;;) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubledError = 2 * error;
        if (doubledError > -dy) {
            error -= dy;
            x0 += stepX;
        }
        if (doubledError < dx) {
            error += dx;
            y0 += stepY;
        }
    }
}

void Canvas::blit(int x, int y, const BitmapView& bitmap) noexcept {
    if (!bitmap.valid()) {
        return;
    }

    const Rect area = intersect(Rect{x, y, bitmap.width, bitmap.height}, clip_);
    if (area.empty()) {
        return;
    }

    for (int py = area.y; py < area.bottom(); ++py) {
        const int sourceY = py - y;
        const Rgb* sourceRow = bitmap.pixels + static_cast<std::ptrdiff_t>(sourceY) * bitmap.width;
        for (int px = area.x; px < area.right(); ++px) {
            target_.set(px, py, sourceRow[px - x]);
        }
    }
}

void Canvas::blitKeyed(int x, int y, const BitmapView& bitmap, Rgb transparent) noexcept {
    if (!bitmap.valid()) {
        return;
    }

    const Rect area = intersect(Rect{x, y, bitmap.width, bitmap.height}, clip_);
    if (area.empty()) {
        return;
    }

    for (int py = area.y; py < area.bottom(); ++py) {
        const int sourceY = py - y;
        const Rgb* sourceRow = bitmap.pixels + static_cast<std::ptrdiff_t>(sourceY) * bitmap.width;
        for (int px = area.x; px < area.right(); ++px) {
            const Rgb source = sourceRow[px - x];
            if (source != transparent) {
                target_.set(px, py, source);
            }
        }
    }
}

}  // namespace notrix
