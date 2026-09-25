// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace stipple {

struct Point {
    int x = 0;
    int y = 0;
};

constexpr bool operator==(Point lhs, Point rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

constexpr bool operator!=(Point lhs, Point rhs) noexcept {
    return !(lhs == rhs);
}

/// Axis-aligned rectangle. `right()` and `bottom()` are exclusive bounds, so an
/// empty rect and a zero-area rect are the same thing.
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr int left() const noexcept { return x; }
    constexpr int top() const noexcept { return y; }
    constexpr int right() const noexcept { return x + w; }
    constexpr int bottom() const noexcept { return y + h; }

    constexpr bool empty() const noexcept { return w <= 0 || h <= 0; }

    constexpr bool contains(int px, int py) const noexcept {
        return px >= x && px < right() && py >= y && py < bottom();
    }

    constexpr bool contains(Point p) const noexcept { return contains(p.x, p.y); }
};

constexpr bool operator==(const Rect& lhs, const Rect& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}

constexpr bool operator!=(const Rect& lhs, const Rect& rhs) noexcept {
    return !(lhs == rhs);
}

/// Overlap of two rects, or an empty rect at the origin when they do not touch.
/// Every clip operation funnels through here, so it is deliberately total: it
/// has no failure mode and never produces a negative extent.
constexpr Rect intersect(const Rect& a, const Rect& b) noexcept {
    const int x0 = a.x > b.x ? a.x : b.x;
    const int y0 = a.y > b.y ? a.y : b.y;
    const int x1 = a.right() < b.right() ? a.right() : b.right();
    const int y1 = a.bottom() < b.bottom() ? a.bottom() : b.bottom();

    if (x1 <= x0 || y1 <= y0) {
        return Rect{};
    }
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

}  // namespace stipple
