// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/notify/Notifications.h"

#include <string>

#include "support/TestFramework.h"

using stipple::notify::Notification;
using stipple::notify::NotificationQueue;
using stipple::notify::Priority;
using stipple::notify::priorityFromInt;

namespace {

int result(NotificationQueue::PushResult value) {
    return static_cast<int>(value);
}

Notification make(std::string id,
                  Priority priority = Priority::Normal,
                  int durationSeconds = 5) {
    Notification notification;
    notification.id = std::move(id);
    notification.text = "message";
    notification.priority = priority;
    notification.durationSeconds = durationSeconds;
    return notification;
}

std::string activeId(const NotificationQueue& queue) {
    const Notification* active = queue.active();
    return active != nullptr ? active->id : std::string();
}

}  // namespace

// --- basic flow --------------------------------------------------------------

STIPPLE_TEST(Notifications, FirstNotificationShowsImmediately) {
    NotificationQueue queue;
    STIPPLE_CHECK_EQ(result(queue.push(make("a"), 0)),
                    result(NotificationQueue::PushResult::Shown));
    STIPPLE_CHECK_EQ(activeId(queue), std::string("a"));
    STIPPLE_CHECK_EQ(queue.pending(), 0);
    STIPPLE_CHECK_EQ(queue.size(), 1);
}

STIPPLE_TEST(Notifications, EqualPriorityWaitsItsTurn) {
    NotificationQueue queue;
    queue.push(make("a"), 0);
    STIPPLE_CHECK_EQ(result(queue.push(make("b"), 10)),
                    result(NotificationQueue::PushResult::Queued));

    STIPPLE_CHECK_EQ(activeId(queue), std::string("a"));
    STIPPLE_CHECK_EQ(queue.pending(), 1);
}

STIPPLE_TEST(Notifications, ExpiresAfterItsDuration) {
    NotificationQueue queue;
    queue.push(make("a", Priority::Normal, 5), 0);
    queue.push(make("b", Priority::Normal, 5), 0);

    queue.tick(4999);
    STIPPLE_CHECK_EQ(activeId(queue), std::string("a"));

    STIPPLE_CHECK(queue.tick(5000));
    STIPPLE_CHECK_EQ(activeId(queue), std::string("b"));
}

STIPPLE_TEST(Notifications, EmptiesWhenEverythingHasExpired) {
    NotificationQueue queue;
    queue.push(make("a", Priority::Normal, 2), 0);

    queue.tick(2000);
    STIPPLE_CHECK(queue.active() == nullptr);
    STIPPLE_CHECK_EQ(queue.size(), 0);
}

STIPPLE_TEST(Notifications, HoldStaysUntilDismissed) {
    NotificationQueue queue;
    Notification alarm = make("alarm", Priority::Urgent, 1);
    alarm.hold = true;
    queue.push(alarm, 0);

    queue.tick(3600000);  // an hour
    STIPPLE_CHECK_EQ(activeId(queue), std::string("alarm"));

    STIPPLE_CHECK(queue.dismissActive(3600000));
    STIPPLE_CHECK(queue.active() == nullptr);
}

// --- ordering and preemption -------------------------------------------------

STIPPLE_TEST(Notifications, HigherPriorityPreemptsImmediately) {
    NotificationQueue queue;
    queue.push(make("normal", Priority::Normal), 0);
    STIPPLE_CHECK_EQ(result(queue.push(make("urgent", Priority::Urgent), 100)),
                    result(NotificationQueue::PushResult::Shown));

    STIPPLE_CHECK_EQ(activeId(queue), std::string("urgent"));
    STIPPLE_CHECK_EQ(queue.pending(), 1);  // the displaced one is waiting
}

STIPPLE_TEST(Notifications, PreemptedNotificationResumesAfterwards) {
    NotificationQueue queue;
    queue.push(make("normal", Priority::Normal, 5), 0);
    queue.push(make("urgent", Priority::Urgent, 2), 1000);

    STIPPLE_CHECK_EQ(activeId(queue), std::string("urgent"));

    queue.tick(3000);  // urgent has had its 2s
    STIPPLE_CHECK_EQ(activeId(queue), std::string("normal"));
}

STIPPLE_TEST(Notifications, ResumedNotificationRestartsItsDuration) {
    // Showing the last 0.4s of an interrupted message is worse than repeating it.
    NotificationQueue queue;
    queue.push(make("normal", Priority::Normal, 5), 0);
    queue.push(make("urgent", Priority::Urgent, 1), 4600);

    queue.tick(5600);  // urgent done; normal resumes here
    STIPPLE_CHECK_EQ(activeId(queue), std::string("normal"));

    queue.tick(9000);  // 3.4s into the restarted run
    STIPPLE_CHECK_EQ(activeId(queue), std::string("normal"));

    queue.tick(10700);  // now past 5s
    STIPPLE_CHECK(queue.active() == nullptr);
}

STIPPLE_TEST(Notifications, EqualPriorityNeverPreempts) {
    NotificationQueue queue;
    queue.push(make("first", Priority::Important), 0);
    queue.push(make("second", Priority::Important), 100);

    STIPPLE_CHECK_EQ(activeId(queue), std::string("first"));
}

STIPPLE_TEST(Notifications, QueueDrainsByPriorityThenArrival) {
    NotificationQueue queue;
    queue.push(make("blocker", Priority::Urgent, 1), 0);
    queue.push(make("low", Priority::Informational, 1), 10);
    queue.push(make("high", Priority::Important, 1), 20);
    queue.push(make("mid", Priority::Normal, 1), 30);
    queue.push(make("high2", Priority::Important, 1), 40);

    std::string order;
    for (std::uint64_t t = 0; t <= 8000; t += 1000) {
        queue.tick(t);
        const std::string current = activeId(queue);
        if (!current.empty() && (order.empty() || order.rfind(current) == std::string::npos)) {
            if (!order.empty()) {
                order += ',';
            }
            order += current;
        }
    }

    STIPPLE_CHECK_EQ(order, std::string("blocker,high,high2,mid,low"));
}

// --- replacement -------------------------------------------------------------

STIPPLE_TEST(Notifications, SameIdReplacesRatherThanDuplicating) {
    NotificationQueue queue;
    queue.push(make("sensor"), 0);

    Notification updated = make("sensor");
    updated.text = "22.1C";
    STIPPLE_CHECK_EQ(result(queue.push(updated, 100)),
                    result(NotificationQueue::PushResult::Replaced));

    STIPPLE_CHECK_EQ(queue.size(), 1);
    STIPPLE_CHECK_EQ(queue.active()->text, std::string("22.1C"));
}

STIPPLE_TEST(Notifications, ReplacementKeepsQueuePosition) {
    // Refreshing a queued notification must not let it jump ahead of older
    // peers at the same priority.
    NotificationQueue queue;
    queue.push(make("blocker", Priority::Urgent, 1), 0);
    queue.push(make("first", Priority::Normal, 1), 10);
    queue.push(make("second", Priority::Normal, 1), 20);

    Notification refreshed = make("first", Priority::Normal, 1);
    refreshed.text = "refreshed";
    queue.push(refreshed, 30);

    queue.tick(1000);
    STIPPLE_CHECK_EQ(activeId(queue), std::string("first"));
}

STIPPLE_TEST(Notifications, MissingIdIsFilledIn) {
    NotificationQueue queue;
    Notification anonymous;
    anonymous.text = "hello";
    queue.push(anonymous, 0);

    STIPPLE_CHECK(queue.active() != nullptr);
    STIPPLE_CHECK_FALSE(queue.active()->id.empty());
}

// --- dismissal ---------------------------------------------------------------

STIPPLE_TEST(Notifications, DismissingActivePromotesTheNext) {
    NotificationQueue queue;
    queue.push(make("a"), 0);
    queue.push(make("b"), 10);

    STIPPLE_CHECK(queue.dismiss("a", 100));
    STIPPLE_CHECK_EQ(activeId(queue), std::string("b"));
}

STIPPLE_TEST(Notifications, DismissingFromTheQueueLeavesActiveAlone) {
    NotificationQueue queue;
    queue.push(make("a"), 0);
    queue.push(make("b"), 10);

    STIPPLE_CHECK(queue.dismiss("b", 100));
    STIPPLE_CHECK_EQ(activeId(queue), std::string("a"));
    STIPPLE_CHECK_EQ(queue.pending(), 0);
}

STIPPLE_TEST(Notifications, NonDismissibleRefuses) {
    NotificationQueue queue;
    Notification alarm = make("alarm", Priority::Urgent);
    alarm.dismissible = false;
    queue.push(alarm, 0);

    STIPPLE_CHECK_FALSE(queue.dismiss("alarm", 100));
    STIPPLE_CHECK_FALSE(queue.dismissActive(100));
    STIPPLE_CHECK_EQ(activeId(queue), std::string("alarm"));
}

STIPPLE_TEST(Notifications, DismissAllSkipsNonDismissible) {
    NotificationQueue queue;
    Notification alarm = make("alarm", Priority::Urgent);
    alarm.dismissible = false;
    queue.push(alarm, 0);
    queue.push(make("chatter1"), 10);
    queue.push(make("chatter2"), 20);

    STIPPLE_CHECK_EQ(queue.dismissAll(100), 2);
    STIPPLE_CHECK_EQ(activeId(queue), std::string("alarm"));
    STIPPLE_CHECK_EQ(queue.size(), 1);
}

STIPPLE_TEST(Notifications, DismissingSomethingUnknownFails) {
    NotificationQueue queue;
    queue.push(make("a"), 0);
    STIPPLE_CHECK_FALSE(queue.dismiss("nope", 0));
}

STIPPLE_TEST(Notifications, ClearRemovesEverythingIncludingHeldOnes) {
    NotificationQueue queue;
    Notification alarm = make("alarm");
    alarm.dismissible = false;
    queue.push(alarm, 0);
    queue.push(make("b"), 10);

    queue.clear();
    STIPPLE_CHECK_EQ(queue.size(), 0);
    STIPPLE_CHECK(queue.active() == nullptr);
}

// --- bounds ------------------------------------------------------------------

STIPPLE_TEST(Notifications, QueueIsBounded) {
    NotificationQueue queue;
    queue.push(make("active", Priority::Urgent, 600), 0);

    for (int i = 0; i < NotificationQueue::kMaxQueued + 5; ++i) {
        queue.push(make("n" + std::to_string(i), Priority::Normal), 1);
    }

    STIPPLE_CHECK_EQ(queue.pending(), NotificationQueue::kMaxQueued);
    STIPPLE_CHECK(queue.droppedCount() > 0);
}

STIPPLE_TEST(Notifications, OverflowDropsTheLowestPriorityNotTheOldest) {
    // A flood of chatter must not evict an important message already waiting.
    NotificationQueue queue;
    queue.push(make("active", Priority::Urgent, 600), 0);
    queue.push(make("important", Priority::Important), 1);

    for (int i = 0; i < NotificationQueue::kMaxQueued + 10; ++i) {
        queue.push(make("chatter" + std::to_string(i), Priority::Informational), 2);
    }

    // The important one is still queued and comes out first.
    queue.dismiss("active", 3);
    STIPPLE_CHECK_EQ(activeId(queue), std::string("important"));
}

STIPPLE_TEST(Notifications, LowestPriorityArrivalIsRejectedWhenFull) {
    NotificationQueue queue;
    queue.push(make("active", Priority::Urgent, 600), 0);
    for (int i = 0; i < NotificationQueue::kMaxQueued; ++i) {
        queue.push(make("n" + std::to_string(i), Priority::Normal), 1);
    }

    STIPPLE_CHECK_EQ(result(queue.push(make("late", Priority::Informational), 2)),
                    result(NotificationQueue::PushResult::DroppedLowPriority));
}

STIPPLE_TEST(Notifications, RejectsInvalidContent) {
    NotificationQueue queue;

    Notification empty;
    empty.id = "e";
    STIPPLE_CHECK_EQ(result(queue.push(empty, 0)),
                    result(NotificationQueue::PushResult::Invalid));

    Notification huge = make("h");
    huge.text = std::string(NotificationQueue::kMaxTextBytes + 1, 'x');
    STIPPLE_CHECK_EQ(result(queue.push(huge, 0)),
                    result(NotificationQueue::PushResult::Invalid));

    Notification longId = make(std::string(NotificationQueue::kMaxIdBytes + 1, 'i'));
    STIPPLE_CHECK_EQ(result(queue.push(longId, 0)),
                    result(NotificationQueue::PushResult::Invalid));
}

STIPPLE_TEST(Notifications, ZeroDurationIsTreatedAsOneSecond) {
    NotificationQueue queue;
    queue.push(make("a", Priority::Normal, 0), 0);

    queue.tick(500);
    STIPPLE_CHECK_EQ(activeId(queue), std::string("a"));
    queue.tick(1000);
    STIPPLE_CHECK(queue.active() == nullptr);
}

STIPPLE_TEST(Notifications, PriorityFromIntClamps) {
    STIPPLE_CHECK(priorityFromInt(-5) == Priority::Informational);
    STIPPLE_CHECK(priorityFromInt(0) == Priority::Informational);
    STIPPLE_CHECK(priorityFromInt(2) == Priority::Important);
    STIPPLE_CHECK(priorityFromInt(99) == Priority::Urgent);
}

STIPPLE_TEST(Notifications, BackwardsClockDoesNotExpireEarly) {
    NotificationQueue queue;
    queue.push(make("a", Priority::Normal, 5), 10000);

    queue.tick(5000);
    STIPPLE_CHECK_EQ(activeId(queue), std::string("a"));
}
