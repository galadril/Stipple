// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stipple/core/Rgb.h"

namespace stipple {
namespace asset {
class IconStore;
}  // namespace asset
}  // namespace stipple

namespace stipple {

class Canvas;
struct Rect;

namespace notify {

/// Blueprint §13 priority ladder.
enum class Priority : std::uint8_t {
    Informational = 0,
    Normal = 1,
    Important = 2,
    Urgent = 3,
};

const char* priorityName(Priority priority) noexcept;

/// Clamps rather than rejecting, so an out-of-range value from an integration
/// still delivers the notification.
Priority priorityFromInt(int value) noexcept;

struct Notification {
    std::string id;
    std::string text;
    Priority priority = Priority::Normal;

    int durationSeconds = 5;

    /// Stay on screen until explicitly dismissed. Ignores `durationSeconds`.
    bool hold = false;

    /// Whether a button press may clear it. An alarm may want to insist.
    bool dismissible = true;

    Rgb color = colors::kWhite;
    std::string sound;

    /// An icon from the store, by id. Empty for text alone.
    ///
    /// A notification is the one thing that interrupts whatever you were
    /// looking at, and on a panel this size a glyph says "mail" or "doorbell"
    /// faster than four scrolling words can. The id is resolved at render
    /// time rather than stored as pixels, so replacing an icon updates every
    /// notification that names it.
    std::string icon;

    // Assigned by the queue.
    std::uint64_t enqueuedAtMillis = 0;
    std::uint64_t startedAtMillis = 0;
    std::uint32_t sequence = 0;
};

/// Deterministic notification queue.
///
/// Blueprint §13 requires the policy be deterministic, so it is stated plainly:
///
///   1. Ordering is by priority descending, then arrival order ascending. Two
///      notifications never tie, because the sequence number breaks it.
///   2. A strictly higher priority preempts whatever is showing. The preempted
///      one returns to the queue keeping its original arrival order, and
///      restarts its duration when it resumes — showing the last 0.4s of an
///      interrupted message is worse than showing it again.
///   3. Equal priority never preempts. An urgent alert interrupts; a second
///      normal one waits its turn.
///   4. On overflow the *lowest priority, newest* entry is dropped, which may be
///      the incoming one. Dropping the oldest would let a flood of chatter push
///      out an important message that was already waiting.
///
/// Everything is bounded (§38): a fixed queue depth and a cap on text length.
class NotificationQueue {
public:
    static constexpr int kMaxQueued = 16;
    static constexpr std::size_t kMaxTextBytes = 256;
    static constexpr std::size_t kMaxIdBytes = 64;

    enum class PushResult {
        Shown,               ///< accepted and displayed immediately
        Queued,              ///< accepted, waiting its turn
        Replaced,            ///< an existing notification with the same id was updated
        DroppedLowPriority,  ///< queue full and nothing here ranked lower
        Invalid,             ///< empty text, or a field over its limit
    };

    /// An empty id is filled in automatically, so callers that do not care about
    /// addressing a notification later can omit it.
    PushResult push(Notification notification, std::uint64_t nowMillis);

    /// Advance time: expire the active notification and apply preemption.
    /// Returns true when the visible notification changed.
    bool tick(std::uint64_t nowMillis);

    /// Remove by id, whether active or queued. Respects `dismissible`.
    bool dismiss(std::string_view id, std::uint64_t nowMillis);

    /// Dismiss whatever is showing. Returns false if nothing is, or if it
    /// refuses to be dismissed.
    bool dismissActive(std::uint64_t nowMillis);

    /// Returns how many were removed; non-dismissible ones are left alone.
    int dismissAll(std::uint64_t nowMillis);

    /// Unconditional reset, including non-dismissible entries.
    void clear();

    const Notification* active() const noexcept;

    /// Waiting, not counting the active one. Blueprint §22 surfaces this as
    /// "notification queue depth" in diagnostics.
    int pending() const noexcept { return static_cast<int>(queued_.size()); }

    int size() const noexcept { return pending() + (hasActive_ ? 1 : 0); }

    /// Notifications discarded because the queue was full.
    std::uint32_t droppedCount() const noexcept { return dropped_; }

    /// Milliseconds the active notification has been showing.
    std::uint64_t activeElapsedMillis(std::uint64_t nowMillis) const noexcept;

private:
    int bestQueuedIndex() const noexcept;
    int worstQueuedIndex() const noexcept;
    void promote(int queuedIndex, std::uint64_t nowMillis);

    std::vector<Notification> queued_;
    Notification active_;
    bool hasActive_ = false;
    std::uint32_t nextSequence_ = 1;
    std::uint32_t dropped_ = 0;
};

/// Draw a notification as a full-panel overlay: an optional icon, then its
/// text, auto-scrolled when too wide, over a cleared background.
///
/// `icons` may be null, and the named icon may be missing - in both cases the
/// text takes the whole panel. That is deliberate: the message is the point,
/// and a notification that refuses to appear because a decoration is absent
/// would be the worst possible trade.
void render(Canvas& canvas,
            const Notification& notification,
            const Rect& box,
            std::uint64_t elapsedMillis,
            const asset::IconStore* icons = nullptr);

}  // namespace notify
}  // namespace stipple
