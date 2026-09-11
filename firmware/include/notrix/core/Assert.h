// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>

// NOTRIX_ASSERT is a development-time contract check. It compiles out entirely
// in release builds: on the device an assert must never be the thing that takes
// the clock down, so release code paths are additionally written to degrade
// safely (clip, clamp or ignore) rather than relying on the assertion firing.
#ifdef NDEBUG
#    define NOTRIX_ASSERT(cond) (static_cast<void>(0))
#else
#    define NOTRIX_ASSERT(cond) assert(cond)
#endif
