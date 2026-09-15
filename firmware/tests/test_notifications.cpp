// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/notify/Notifications.h"

#include <string>

#include "support/TestFramework.h"

using notrix::notify::Notification;
using notrix::notify::NotificationQueue;
using notrix::notify::Priority;
using notrix::notify::priorityFromInt;

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

NOTRIX_TEST(Notifications, FirstNotificationShowsImmediately) {
    NotificationQueue queue;
    NOTRIX_CHECK_EQ(result(queue.push(make("a"), 0)),
                    result(NotificationQueue::PushResult::Shown));
    NOTRIX_CHECK_EQ(activeId(queue), std::string("a"));
    NOTRIX_CHECK_EQ(queue.pending(), 0);
    NOTRIX_CHECK_EQ(queue.size(), 1);
}

NOTRIX_TEST(Notifications, EqualPriorityWaitsItsTurn) {
    NotificationQueue queue;
    queue.push(make("a"), 0);
    NOTRIX_CHECK_EQ(result(queue.push(make("b"), 10)),
                    result(NotificationQueue::PushResult::Queued));

    NOTRIX_CHECK_EQ(activeId(queue), std::string("a"));
    NOTRIX_CHECK_EQ(queue.pending(), 1);
}

NOTRIX_TEST(Notifications, ExpiresAfterItsDuration) {
    NotificationQueue queue;
    queue.push(make("a", Priority::Normal, 5), 0);
    queue.push(make("b", Priority::Normal, 5), 0);

    queue.tick(4999);
    NOTRIX_CHECK_EQ(activeId(queue), std::string("a"));

    NOTRIX_CHECK(queue.tick(5000));
    NOTRIX_CHECK_EQ(activeId(queue), std::string("b"));
}

NOTRIX_TEST(Notifications, EmptiesWhenEverythingHasExpired) {
    NotificationQueue queue;
    queue.push(make("a", Priority::Normal, 2), 0);

    queue.tick(2000);
    NOTRIX_CHECK(queue.active() == nullptr);
    NOTRIX_CHECK_EQ(queue.size(), 0);
}

NOTRIX_TEST(Notifications, HoldStaysUntilDismissed) {
    NotificationQueue queue;
    Notification alarm = make("alarm", Priority::Urgent, 1);
    alarm.hold = true;
    queue.push(alarm, 0);

    queue.tick(3600000);  // an hour
    NOTRIX_CHECK_EQ(activeId(queue), std::string("alarm"));

    NOTRIX_CHECK(queue.dismissActive(3600000));
    NOTRIX_CHECK(queue.active() == nullptr);
}

// --- ordering and preemption -------------------------------------------------

NOTRIX_TEST(Notifications, HigherPriorityPreemptsImmediately) {
    NotificationQueue queue;
    queue.push(make("normal", Priority::Normal), 0);
    NOTRIX_CHECK_EQ(result(queue.push(make("urgent", Priority::Urgent), 100)),
                    result(NotificationQueue::PushResult::Shown));

    NOTRIX_CHECK_EQ(activeId(queue), std::string("urgent"));
    NOTRIX_CHECK_EQ(queue.pending(), 1);  // the displaced one is waiting
}

NOTRIX_TEST(Notifications, PreemptedNotificationResumesAfterwards) {
    NotificationQueue queue;
    queue.push(make("normal", Priority::Normal, 5), 0);
    queue.push(make("urgent", Priority::Urgent, 2), 1000);

    NOTRIX_CHECK_EQ(activeId(queue), std::string("urgent"));

    queue.tick(3000);  // urgent has had its 2s
    NOTRIX_CHECK_EQ(activeId(queue), std::string("normal"));
}

NOTRIX_TEST(Notifications, ResumedNotificationRestartsItsDuration) {
    // Showing the last 0.4s of an interrupted message is worse than repeating it.
    NotificationQueue queue;
    queue.push(make("normal", Priority::Normal, 5), 0);
    queue.push(make("urgent", Priority::Urgent, 1), 4600);

    queue.tick(5600);  // urgent done; normal resumes here
    NOTRIX_CHECK_EQ(activeId(queue), std::string("normal"));

    queue.tick(9000);  // 3.4s into the restarted run
    NOTRIX_CHECK_EQ(activeId(queue), std::string("normal"));

    queue.tick(10700);  // now past 5s
    NOTRIX_CHECK(queue.active() == nullptr);
}

NOTRIX_TEST(Notifications, EqualPriorityNeverPreempts) {
    NotificationQueue queue;
    queue.push(make("first", Priority::Important), 0);
    queue.push(make("second", Priority::Important), 100);

    NOTRIX_CHECK_EQ(activeId(queue), std::string("first"));
}

NOTRIX_TEST(Notifications, QueueDrainsByPriorityThenArrival) {
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

    NOTRIX_CHECK_EQ(order, std::string("blocker,high,high2,mid,low"));
}

// --- replacement -------------------------------------------------------------

NOTRIX_TEST(Notifications, SameIdReplacesRatherThanDuplicating) {
    NotificationQueue queue;
    queue.push(make("sensor"), 0);

    Notification updated = make("sensor");
    updated.text = "22.1C";
    NOTRIX_CHECK_EQ(result(queue.push(updated, 100)),
                    result(NotificationQueue::PushResult::Replaced));

    NOTRIX_CHECK_EQ(queue.size(), 1);
    NOTRIX_CHECK_EQ(queue.active()->text, std::string("22.1C"));
}

NOTRIX_TEST(Notifications, ReplacementKeepsQueuePosition) {
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
    NOTRIX_CHECK_EQ(activeId(queue), std::string("first"));
}

NOTRIX_TEST(Notifications, MissingIdIsFilledIn) {
    NotificationQueue queue;
    Notification anonymous;
    anonymous.text = "hello";
    queue.push(anonymous, 0);

    NOTRIX_CHECK(queue.active() != nullptr);
    NOTRIX_CHECK_FALSE(queue.active()->id.empty());
}

// --- dismissal ---------------------------------------------------------------

NOTRIX_TEST(Notifications, DismissingActivePromotesTheNext) {
    NotificationQueue queue;
    queue.push(make("a"), 0);
    queue.push(make("b"), 10);

    NOTRIX_CHECK(queue.dismiss("a", 100));
    NOTRIX_CHECK_EQ(activeId(queue), std::string("b"));
}

NOTRIX_TEST(Notifications, DismissingFromTheQueueLeavesActiveAlone) {
    NotificationQueue queue;
    queue.push(make("a"), 0);
    queue.push(make("b"), 10);

    NOTRIX_CHECK(queue.dismiss("b", 100));
    NOTRIX_CHECK_EQ(activeId(queue), std::string("a"));
    NOTRIX_CHECK_EQ(queue.pending(), 0);
}

NOTRIX_TEST(Notifications, NonDismissibleRefuses) {
    NotificationQueue queue;
    Notification alarm = make("alarm", Priority::Urgent);
    alarm.dismissible = false;
    queue.push(alarm, 0);

    NOTRIX_CHECK_FALSE(queue.dismiss("alarm", 100));
    NOTRIX_CHECK_FALSE(queue.dismissActive(100));
    NOTRIX_CHECK_EQ(activeId(queue), std::string("alarm"));
}

NOTRIX_TEST(Notifications, DismissAllSkipsNonDismissible) {
    NotificationQueue queue;
    Notification alarm = make("alarm", Priority::Urgent);
    alarm.dismissible = false;
    queue.push(alarm, 0);
    queue.push(make("chatter1"), 10);
    queue.push(make("chatter2"), 20);

    NOTRIX_CHECK_EQ(queue.dismissAll(100), 2);
    NOTRIX_CHECK_EQ(activeId(queue), std::string("alarm"));
    NOTRIX_CHECK_EQ(queue.size(), 1);
}

NOTRIX_TEST(Notifications, DismissingSomethingUnknownFails) {
    NotificationQueue queue;
    queue.push(make("a"), 0);
    NOTRIX_CHECK_FALSE(queue.dismiss("nope", 0));
}

NOTRIX_TEST(Notifications, ClearRemovesEverythingIncludingHeldOnes) {
    NotificationQueue queue;
    Notification alarm = make("alarm");
    alarm.dismissible = false;
    queue.push(alarm, 0);
    queue.push(make("b"), 10);

    queue.clear();
    NOTRIX_CHECK_EQ(queue.size(), 0);
    NOTRIX_CHECK(queue.active() == nullptr);
}

// --- bounds ------------------------------------------------------------------

NOTRIX_TEST(Notifications, QueueIsBounded) {
    NotificationQueue queue;
    queue.push(make("active", Priority::Urgent, 600), 0);

    for (int i = 0; i < NotificationQueue::kMaxQueued + 5; ++i) {
        queue.push(make("n" + std::to_string(i), Priority::Normal), 1);
    }

    NOTRIX_CHECK_EQ(queue.pending(), NotificationQueue::kMaxQueued);
    NOTRIX_CHECK(queue.droppedCount() > 0);
}

NOTRIX_TEST(Notifications, OverflowDropsTheLowestPriorityNotTheOldest) {
    // A flood of chatter must not evict an important message already waiting.
    NotificationQueue queue;
    queue.push(make("active", Priority::Urgent, 600), 0);
    queue.push(make("important", Priority::Important), 1);

    for (int i = 0; i < NotificationQueue::kMaxQueued + 10; ++i) {
        queue.push(make("chatter" + std::to_string(i), Priority::Informational), 2);
    }

    // The important one is still queued and comes out first.
    queue.dismiss("active", 3);
    NOTRIX_CHECK_EQ(activeId(queue), std::string("important"));
}

NOTRIX_TEST(Notifications, LowestPriorityArrivalIsRejectedWhenFull) {
    NotificationQueue queue;
    queue.push(make("active", Priority::Urgent, 600), 0);
    for (int i = 0; i < NotificationQueue::kMaxQueued; ++i) {
        queue.push(make("n" + std::to_string(i), Priority::Normal), 1);
    }

    NOTRIX_CHECK_EQ(result(queue.push(make("late", Priority::Informational), 2)),
                    result(NotificationQueue::PushResult::DroppedLowPriority));
}

NOTRIX_TEST(Notifications, RejectsInvalidContent) {
    NotificationQueue queue;

    Notification empty;
    empty.id = "e";
    NOTRIX_CHECK_EQ(result(queue.push(empty, 0)),
                    result(NotificationQueue::PushResult::Invalid));

    Notification huge = make("h");
    huge.text = std::string(NotificationQueue::kMaxTextBytes + 1, 'x');
    NOTRIX_CHECK_EQ(result(queue.push(huge, 0)),
                    result(NotificationQueue::PushResult::Invalid));

    Notification longId = make(std::string(NotificationQueue::kMaxIdBytes + 1, 'i'));
    NOTRIX_CHECK_EQ(result(queue.push(longId, 0)),
                    result(NotificationQueue::PushResult::Invalid));
}

NOTRIX_TEST(Notifications, ZeroDurationIsTreatedAsOneSecond) {
    NotificationQueue queue;
    queue.push(make("a", Priority::Normal, 0), 0);

    queue.tick(500);
    NOTRIX_CHECK_EQ(activeId(queue), std::string("a"));
    queue.tick(1000);
    NOTRIX_CHECK(queue.active() == nullptr);
}

NOTRIX_TEST(Notifications, PriorityFromIntClamps) {
    NOTRIX_CHECK(priorityFromInt(-5) == Priority::Informational);
    NOTRIX_CHECK(priorityFromInt(0) == Priority::Informational);
    NOTRIX_CHECK(priorityFromInt(2) == Priority::Important);
    NOTRIX_CHECK(priorityFromInt(99) == Priority::Urgent);
}

NOTRIX_TEST(Notifications, BackwardsClockDoesNotExpireEarly) {
    NotificationQueue queue;
    queue.push(make("a", Priority::Normal, 5), 10000);

    queue.tick(5000);
    NOTRIX_CHECK_EQ(activeId(queue), std::string("a"));
}
