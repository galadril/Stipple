// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "stipple/core/Rgb.h"

namespace stipple {

class Canvas;

namespace apps {

/// Says an app cannot show what it was asked to, in two short words.
///
/// Two words because that is what fits. The panel is 52 pixels across and the
/// font is five wide, so a line holds about eight characters - enough for a
/// label and nothing like enough for a reason. A Berry error runs to a line of
/// text with a file and a line number in it.
///
/// So the split is deliberate: the panel says *that* something is wrong, and
/// the API, the web UI and the log say *what*. Both halves matter. A panel
/// that goes black is indistinguishable from a working app that drew nothing,
/// from a crashed device and from a dead row of LEDs, and the person standing
/// in front of it has no way to tell which - that is what ADR 0013 is about.
/// But a panel that tries to scroll a stack trace past you at reading speed is
/// not better, it is only busier.
void renderUnavailable(Canvas& canvas, std::string_view top, std::string_view bottom,
                       Rgb color = Rgb{160, 160, 160});

}  // namespace apps
}  // namespace stipple
