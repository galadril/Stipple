// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/notify/Notifications.h"

#include "stipple/asset/IconStore.h"

#include "stipple/graphics/Canvas.h"
#include "stipple/text/Scroll.h"

namespace stipple {
namespace notify {
namespace {

/// Ranks two notifications by the documented policy: priority first, then
/// arrival order. Returns true when `a` should be shown before `b`.
bool ranksAhead(const Notification& a, const Notification& b) noexcept {
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    return a.sequence < b.sequence;
}

}  // namespace

const char* priorityName(Priority priority) noexcept {
    switch (priority) {
        case Priority::Informational: return "informational";
        case Priority::Normal: return "normal";
        case Priority::Important: return "important";
        case Priority::Urgent: return "urgent";
    }
    return "unknown";
}

Priority priorityFromInt(int value) noexcept {
    if (value <= 0) {
        return Priority::Informational;
    }
    if (value >= 3) {
        return Priority::Urgent;
    }
    return static_cast<Priority>(static_cast<std::uint8_t>(value));
}

const Notification* NotificationQueue::active() const noexcept {
    return hasActive_ ? &active_ : nullptr;
}

std::uint64_t NotificationQueue::activeElapsedMillis(std::uint64_t nowMillis) const noexcept {
    if (!hasActive_ || nowMillis < active_.startedAtMillis) {
        return 0;
    }
    return nowMillis - active_.startedAtMillis;
}

int NotificationQueue::bestQueuedIndex() const noexcept {
    if (queued_.empty()) {
        return -1;
    }
    int best = 0;
    for (int i = 1; i < static_cast<int>(queued_.size()); ++i) {
        if (ranksAhead(queued_[static_cast<std::size_t>(i)], queued_[static_cast<std::size_t>(best)])) {
            best = i;
        }
    }
    return best;
}

int NotificationQueue::worstQueuedIndex() const noexcept {
    if (queued_.empty()) {
        return -1;
    }
    int worst = 0;
    for (int i = 1; i < static_cast<int>(queued_.size()); ++i) {
        // Inverse of ranksAhead: lowest priority, and newest within that band.
        if (ranksAhead(queued_[static_cast<std::size_t>(worst)], queued_[static_cast<std::size_t>(i)])) {
            worst = i;
        }
    }
    return worst;
}

void NotificationQueue::promote(int queuedIndex, std::uint64_t nowMillis) {
    active_ = std::move(queued_[static_cast<std::size_t>(queuedIndex)]);
    queued_.erase(queued_.begin() + queuedIndex);
    active_.startedAtMillis = nowMillis;
    hasActive_ = true;
}

NotificationQueue::PushResult NotificationQueue::push(Notification notification,
                                                      std::uint64_t nowMillis) {
    if (notification.text.empty() || notification.text.size() > kMaxTextBytes ||
        notification.id.size() > kMaxIdBytes) {
        return PushResult::Invalid;
    }
    if (notification.durationSeconds < 1) {
        notification.durationSeconds = 1;
    }

    notification.enqueuedAtMillis = nowMillis;

    if (notification.id.empty()) {
        notification.id = "n" + std::to_string(nextSequence_);
    }

    // Replacing by id keeps the original arrival order, so refreshing a
    // notification does not let it jump the queue ahead of older peers.
    if (hasActive_ && active_.id == notification.id) {
        notification.sequence = active_.sequence;
        active_ = std::move(notification);
        active_.startedAtMillis = nowMillis;  // a refreshed message is shown in full
        return PushResult::Replaced;
    }
    for (std::size_t i = 0; i < queued_.size(); ++i) {
        if (queued_[i].id == notification.id) {
            notification.sequence = queued_[i].sequence;
            queued_[i] = std::move(notification);
            return PushResult::Replaced;
        }
    }

    notification.sequence = nextSequence_++;

    if (!hasActive_) {
        active_ = std::move(notification);
        active_.startedAtMillis = nowMillis;
        hasActive_ = true;
        return PushResult::Shown;
    }

    // Strictly higher priority preempts; equal priority waits.
    if (notification.priority > active_.priority) {
        Notification displaced = std::move(active_);
        active_ = std::move(notification);
        active_.startedAtMillis = nowMillis;
        queued_.push_back(std::move(displaced));
        return PushResult::Shown;
    }

    if (static_cast<int>(queued_.size()) >= kMaxQueued) {
        const int worst = worstQueuedIndex();
        // Drop the incoming one if nothing already waiting ranks below it, so a
        // burst of chatter cannot evict an important message.
        if (worst < 0 || !ranksAhead(notification, queued_[static_cast<std::size_t>(worst)])) {
            ++dropped_;
            return PushResult::DroppedLowPriority;
        }
        queued_.erase(queued_.begin() + worst);
        ++dropped_;
    }

    queued_.push_back(std::move(notification));
    return PushResult::Queued;
}

bool NotificationQueue::tick(std::uint64_t nowMillis) {
    const std::string before = hasActive_ ? active_.id : std::string();

    if (hasActive_ && !active_.hold) {
        const std::uint64_t duration = static_cast<std::uint64_t>(active_.durationSeconds) * 1000u;
        if (activeElapsedMillis(nowMillis) >= duration) {
            hasActive_ = false;
        }
    }

    if (!hasActive_) {
        const int best = bestQueuedIndex();
        if (best >= 0) {
            promote(best, nowMillis);
        }
    } else {
        // Preemption also has to be checked here, not only on push: a queued
        // urgent notification can become eligible when the active one is
        // replaced by a lower-priority refresh.
        const int best = bestQueuedIndex();
        if (best >= 0 && queued_[static_cast<std::size_t>(best)].priority > active_.priority) {
            Notification displaced = std::move(active_);
            promote(best, nowMillis);
            queued_.push_back(std::move(displaced));
        }
    }

    const std::string after = hasActive_ ? active_.id : std::string();
    return before != after;
}

bool NotificationQueue::dismiss(std::string_view id, std::uint64_t nowMillis) {
    if (hasActive_ && active_.id == id) {
        if (!active_.dismissible) {
            return false;
        }
        hasActive_ = false;
        const int best = bestQueuedIndex();
        if (best >= 0) {
            promote(best, nowMillis);
        }
        return true;
    }

    for (std::size_t i = 0; i < queued_.size(); ++i) {
        if (queued_[i].id == id) {
            if (!queued_[i].dismissible) {
                return false;
            }
            queued_.erase(queued_.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

bool NotificationQueue::dismissActive(std::uint64_t nowMillis) {
    if (!hasActive_) {
        return false;
    }
    return dismiss(active_.id, nowMillis);
}

int NotificationQueue::dismissAll(std::uint64_t nowMillis) {
    int removed = 0;

    std::vector<Notification> kept;
    for (Notification& notification : queued_) {
        if (notification.dismissible) {
            ++removed;
        } else {
            kept.push_back(std::move(notification));
        }
    }
    queued_ = std::move(kept);

    if (hasActive_ && active_.dismissible) {
        hasActive_ = false;
        ++removed;
        const int best = bestQueuedIndex();
        if (best >= 0) {
            promote(best, nowMillis);
        }
    }

    return removed;
}

void NotificationQueue::clear() {
    queued_.clear();
    hasActive_ = false;
    dropped_ = 0;
}

void render(Canvas& canvas,
            const Notification& notification,
            const Rect& box,
            std::uint64_t elapsedMillis,
            const asset::IconStore* icons) {
    canvas.fillRect(box, colors::kBlack);

    Rect textBox = box;

    // The icon takes the left edge and the text keeps the rest.
    //
    // Resolved every frame rather than cached: replacing an icon should
    // change every notification that names it, and looking one up is a map
    // probe on a store bounded to 64 entries.
    const asset::Icon* icon =
        (icons != nullptr && !notification.icon.empty())
            ? icons->find(notification.icon)
            : nullptr;

    if (icon != nullptr && icon->width > 0 && icon->height > 0) {
        // Centred vertically in whatever room the panel has, and never past
        // half the width - an icon wide enough to crowd out the message has
        // stopped being an icon.
        const int maxWidth = box.w / 2;
        const int drawWidth = icon->width < maxWidth ? icon->width : maxWidth;
        const int y = box.y + (box.h - icon->height) / 2;

        const int frame = asset::IconStore::frameAt(*icon, elapsedMillis);
        const BitmapView view = asset::IconStore::frameView(*icon, frame);

        // Clipped to the box rather than trusted: an icon taller than the
        // panel is a stored value, and the canvas is the wrong place to
        // discover it.
        canvas.setClip(box);
        if (icon->hasTransparency) {
            canvas.blitKeyed(box.x, y, view, icon->transparent);
        } else {
            canvas.blit(box.x, y, view);
        }
        canvas.resetClip();

        // One column of air, so the glyph does not touch the first letter.
        const int taken = drawWidth + 1;
        textBox.x += taken;
        textBox.w -= taken;
    }

    if (textBox.w <= 0) {
        return;
    }

    text::TextStyle style;
    style.font = &text::font5x7();
    style.color = notification.color;
    style.hAlign = text::HAlign::Center;
    style.vAlign = text::VAlign::Middle;

    text::drawScrolling(canvas, notification.text, textBox, style, text::ScrollMode::Auto,
                        elapsedMillis);
}

}  // namespace notify
}  // namespace stipple
