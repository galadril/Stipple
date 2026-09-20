// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/graphics/Framebuffer.h"

namespace notrix {
namespace platform {

/// The panel, as the rest of NOTRIX sees it.
///
/// Core code renders into a Framebuffer it owns and hands the finished frame
/// here. On the TC002 this writes 3072 bytes to /dev/spidev0.0 and strobes the
/// GPIO 35 latch; in the simulator it reaches a browser canvas. Nothing above
/// this interface can tell which.
class IFrameBufferDisplay {
public:
    virtual ~IFrameBufferDisplay() = default;

    /// Push a completed frame to the panel.
    ///
    /// Implementations must treat this as the only way pixels reach hardware,
    /// and must not retain a reference to `frame` past the call — the caller
    /// owns and reuses it, and there is no second framebuffer to copy into.
    virtual void present(const Framebuffer& frame) = 0;

    /// Global brightness, 0-255. Applied by the platform to the whole panel
    /// after rendering, so apps always draw in true colour.
    virtual void setBrightness(std::uint8_t brightness) = 0;
    virtual std::uint8_t brightness() const = 0;

    /// Shortest interval this display can accept between frames.
    ///
    /// The TC002's display API is internally throttled and Ulanzi warns against
    /// intervals below roughly 15 ms (blueprint §9.4). The scheduler reads this
    /// rather than hard-coding a frame rate, so the simulator and the device can
    /// honestly disagree about what they can sustain.
    virtual int minimumFrameIntervalMillis() const = 0;
};

}  // namespace platform
}  // namespace notrix
