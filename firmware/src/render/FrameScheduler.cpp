// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/render/FrameScheduler.h"

namespace notrix {
namespace render {
namespace {

int effectiveInterval(int minimumIntervalMillis, int targetFps) noexcept {
    const int floorMillis = minimumIntervalMillis > 0 ? minimumIntervalMillis : 1;
    const int fps = targetFps > 0 ? targetFps : 1;
    const int targetMillis = 1000 / fps;
    // The hardware floor wins: asking for 60 FPS on a panel that cannot do it
    // would just produce overruns.
    return targetMillis > floorMillis ? targetMillis : floorMillis;
}

}  // namespace

FrameScheduler::FrameScheduler(const Config& config) noexcept : config_(config) {
    intervalMillis_ = effectiveInterval(config_.minimumIntervalMillis, config_.targetFps);
}

void FrameScheduler::setMinimumInterval(int millis) noexcept {
    config_.minimumIntervalMillis = millis;
    intervalMillis_ = effectiveInterval(config_.minimumIntervalMillis, config_.targetFps);
}

bool FrameScheduler::beginFrame(std::uint64_t nowMillis) noexcept {
    // A clock that stepped backwards must not stall rendering until real time
    // catches up. Re-baseline and treat this frame as due: the old timeline is
    // no longer trustworthy, so waiting a full interval on it would be waiting
    // on nothing.
    bool steppedBack = false;
    if (nowMillis < lastEvaluatedMillis_) {
        lastEvaluatedMillis_ = nowMillis;
        lastPresentMillis_ = nowMillis;
        steppedBack = true;
    }

    const bool due = !hasRendered_ || steppedBack ||
                     (nowMillis - lastEvaluatedMillis_) >= static_cast<std::uint64_t>(intervalMillis_);
    if (!due) {
        return false;  // not yet time; not a skip
    }
    lastEvaluatedMillis_ = nowMillis;

    const bool refreshDue =
        config_.periodicRefreshMillis > 0 &&
        (nowMillis - lastPresentMillis_) >= config_.periodicRefreshMillis;

    if (!dirty_ && !refreshDue) {
        ++stats_.skipped;
        return false;
    }
    return true;
}

void FrameScheduler::endFrame(std::uint64_t nowMillis, std::uint32_t renderMillis) noexcept {
    dirty_ = false;
    hasRendered_ = true;
    lastPresentMillis_ = nowMillis;

    ++stats_.rendered;
    stats_.lastRenderMillis = renderMillis;
    stats_.lastPresentMillis = nowMillis;
    if (renderMillis > stats_.worstRenderMillis) {
        stats_.worstRenderMillis = renderMillis;
    }
    if (renderMillis > static_cast<std::uint32_t>(intervalMillis_)) {
        ++stats_.overruns;
    }
}

std::uint64_t FrameScheduler::nextDueMillis(std::uint64_t nowMillis) const noexcept {
    if (!hasRendered_) {
        return nowMillis;
    }
    const std::uint64_t due = lastEvaluatedMillis_ + static_cast<std::uint64_t>(intervalMillis_);
    return due > nowMillis ? due : nowMillis;
}

}  // namespace render
}  // namespace notrix
