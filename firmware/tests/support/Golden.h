// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TestFramework.h"
#include "stipple/graphics/Framebuffer.h"

namespace stipple {
namespace test {

/// Compare a rendered frame against `testdata/<name>.rgb` (raw RGB888,
/// row-major). Blueprint §31.2 — this is what catches pixel regressions when a
/// layout or font change quietly shifts something by one pixel.
///
/// On mismatch the actual and expected frames are written as magnified PNGs into
/// `testdata/_failed/` so the difference can be inspected by eye.
///
/// Fixture policy:
///   - missing fixture, normal run -> created automatically, with a loud notice
///     and a reviewable PNG, so a new test does not fail on its first run
///   - missing fixture, STIPPLE_STRICT_GOLDEN=1 (CI) -> failure, because absent
///     there means it was never committed
///   - mismatch -> always a failure, unless STIPPLE_UPDATE_GOLDEN=1
///
/// Always look at the PNG before committing a created or updated fixture. A
/// blindly regenerated golden file records the bug instead of catching it.
void checkGolden(const char* name, const Framebuffer& framebuffer, const char* file, int line);

}  // namespace test
}  // namespace stipple

#define STIPPLE_CHECK_GOLDEN(name, framebuffer)                                                     \
    ::stipple::test::checkGolden((name), (framebuffer), __FILE__, __LINE__)
