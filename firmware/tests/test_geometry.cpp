// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/core/Geometry.h"

#include "support/TestFramework.h"

using notrix::intersect;
using notrix::Point;
using notrix::Rect;

NOTRIX_TEST(Geometry, RectEdgesAreExclusiveOnTheFarSide) {
    const Rect r{2, 3, 10, 4};
    NOTRIX_CHECK_EQ(r.left(), 2);
    NOTRIX_CHECK_EQ(r.top(), 3);
    NOTRIX_CHECK_EQ(r.right(), 12);
    NOTRIX_CHECK_EQ(r.bottom(), 7);

    NOTRIX_CHECK(r.contains(2, 3));
    NOTRIX_CHECK(r.contains(11, 6));
    NOTRIX_CHECK_FALSE(r.contains(12, 6));
    NOTRIX_CHECK_FALSE(r.contains(11, 7));
    NOTRIX_CHECK_FALSE(r.contains(1, 3));
}

NOTRIX_TEST(Geometry, ZeroOrNegativeExtentIsEmpty) {
    NOTRIX_CHECK(Rect{}.empty());
    NOTRIX_CHECK(Rect({0, 0, 0, 5}).empty());
    NOTRIX_CHECK(Rect({0, 0, 5, 0}).empty());
    NOTRIX_CHECK(Rect({0, 0, -3, 5}).empty());
    NOTRIX_CHECK_FALSE(Rect({0, 0, 1, 1}).empty());
}

NOTRIX_TEST(Geometry, EmptyRectContainsNothing) {
    const Rect empty{4, 4, 0, 0};
    NOTRIX_CHECK_FALSE(empty.contains(4, 4));
}

NOTRIX_TEST(Geometry, IntersectReturnsOverlap) {
    const Rect a{0, 0, 10, 10};
    const Rect b{5, 5, 10, 10};
    NOTRIX_CHECK_EQ(intersect(a, b), Rect({5, 5, 5, 5}));
}

NOTRIX_TEST(Geometry, IntersectIsCommutative) {
    const Rect a{2, 1, 8, 9};
    const Rect b{5, 5, 20, 2};
    NOTRIX_CHECK_EQ(intersect(a, b), intersect(b, a));
}

NOTRIX_TEST(Geometry, DisjointRectsIntersectToEmpty) {
    const Rect a{0, 0, 4, 4};
    const Rect b{10, 10, 4, 4};
    NOTRIX_CHECK(intersect(a, b).empty());
}

NOTRIX_TEST(Geometry, TouchingEdgesDoNotOverlap) {
    // a ends at x=4 exclusive, b starts at x=4. They share a boundary, not area.
    const Rect a{0, 0, 4, 4};
    const Rect b{4, 0, 4, 4};
    NOTRIX_CHECK(intersect(a, b).empty());
}

NOTRIX_TEST(Geometry, IntersectNeverProducesNegativeExtent) {
    // Clipping funnels through intersect, so a negative width here would turn
    // into out-of-bounds writes further up the renderer.
    const Rect weird{10, 10, -5, -5};
    const Rect panel{0, 0, 52, 16};
    const Rect result = intersect(weird, panel);
    NOTRIX_CHECK(result.w >= 0);
    NOTRIX_CHECK(result.h >= 0);
    NOTRIX_CHECK(result.empty());
}

NOTRIX_TEST(Geometry, IntersectIsIdempotentAndShrinking) {
    const Rect panel{0, 0, 52, 16};
    const Rect clip{10, 2, 20, 8};
    const Rect once = intersect(clip, panel);
    NOTRIX_CHECK_EQ(intersect(once, panel), once);
    NOTRIX_CHECK(once.w <= clip.w);
    NOTRIX_CHECK(once.h <= clip.h);
}

NOTRIX_TEST(Geometry, PointEquality) {
    NOTRIX_CHECK_EQ(Point({3, 4}), Point({3, 4}));
    NOTRIX_CHECK(Point({3, 4}) != Point({4, 3}));
}
