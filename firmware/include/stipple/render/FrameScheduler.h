// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace stipple {
namespace render {

struct FrameStats {
    std::uint32_t rendered = 0;
    /// Times the interval elapsed but nothing needed redrawing. A healthy
    /// static clock face skips far more often than it renders; if this stays at
    /// zero, dirty tracking is not working.
    std::uint32_t skipped = 0;
    /// Frames whose render took longer than the interval allows. Non-zero means
    /// the device cannot sustain the configured rate.
    std::uint32_t overruns = 0;

    std::uint32_t lastRenderMillis = 0;
    std::uint32_t worstRenderMillis = 0;
    std::uint64_t lastPresentMillis = 0;
};

/// Decides when to draw (blueprint §9.4).
///
/// Three jobs, all of which the device needs and none of which existed while the
/// browser's requestAnimationFrame was doing the pacing:
///
///   1. **Respect the panel's floor.** Ulanzi warn against frame intervals below
///      roughly 15 ms, and the display API is internally throttled. The interval
///      is taken from `IFrameBufferDisplay::minimumFrameIntervalMillis()` rather
///      than hard-coded, so the simulator and the device can honestly differ.
///   2. **Skip work that changes nothing.** A clock showing a static minute has
///      no reason to redraw 30 times a second. Callers mark the frame dirty when
///      something actually changes.
///   3. **Notice when it cannot keep up**, so the failure is a metric in
///      diagnostics rather than a user reporting "it feels laggy".
///
/// Time is passed in; the scheduler never reads a clock or sleeps. The caller
/// asks `nextDueMillis()` how long it may sleep for, which keeps the device main
/// loop off a busy-wait without dragging threading into core.
class FrameScheduler {
public:
    struct Config {
        /// Hardware floor. Frames are never presented closer together than this.
        int minimumIntervalMillis = 15;

        /// Desired rate when animating. Blueprint §9.4 asks for 20-30 FPS, not
        /// 60: the panel cannot sustain it and pretending otherwise invites
        /// animations that only look right in a browser.
        int targetFps = 30;

        /// Redraw at least this often even when nothing is dirty, so a panel
        /// glitch or a dropped SPI frame heals itself instead of persisting
        /// until the next content change.
        std::uint32_t periodicRefreshMillis = 5000;
    };

    /// Two constructors rather than a defaulted argument: `Config{}` as a
    /// default argument inside its own enclosing class is ill-formed, because
    /// the nested type's default member initializers are not yet available
    /// there. MSVC accepts it; Clang, correctly, does not.
    FrameScheduler() noexcept : FrameScheduler(Config{}) {}
    explicit FrameScheduler(const Config& config) noexcept;

    /// Something changed; the next due frame should be drawn.
    void invalidate() noexcept { dirty_ = true; }
    bool dirty() const noexcept { return dirty_; }

    /// Should a frame be drawn at `nowMillis`?
    ///
    /// Mutating: it records skipped frames, so call it once per loop iteration
    /// and act on the answer.
    bool beginFrame(std::uint64_t nowMillis) noexcept;

    /// Report a completed render. `renderMillis` covers drawing and presenting.
    void endFrame(std::uint64_t nowMillis, std::uint32_t renderMillis) noexcept;

    /// Earliest time another frame could be drawn. The device loop sleeps until
    /// then rather than spinning.
    std::uint64_t nextDueMillis(std::uint64_t nowMillis) const noexcept;

    /// Effective interval: the larger of the hardware floor and the target rate.
    int intervalMillis() const noexcept { return intervalMillis_; }

    const FrameStats& stats() const noexcept { return stats_; }
    void resetStats() noexcept { stats_ = FrameStats{}; }

    /// Adopt the panel's real floor once the display is known.
    void setMinimumInterval(int millis) noexcept;

private:
    Config config_;
    int intervalMillis_ = 33;

    bool dirty_ = true;  ///< the first frame always draws
    bool hasRendered_ = false;
    std::uint64_t lastEvaluatedMillis_ = 0;
    std::uint64_t lastPresentMillis_ = 0;

    FrameStats stats_;
};

}  // namespace render
}  // namespace stipple
