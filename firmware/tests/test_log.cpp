// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/core/Log.h"

#include <string>

#include "support/TestFramework.h"

using notrix::log::Level;
using notrix::log::levelName;
using notrix::log::RingLog;

namespace {

std::string messageAt(const RingLog& ring, int index) {
    return std::string(ring.at(index).message);
}

}  // namespace

NOTRIX_TEST(RingLog, StoresInOrder) {
    RingLog ring;
    ring.info(100, "first");
    ring.warn(200, "second");

    NOTRIX_CHECK_EQ(ring.count(), 2);
    NOTRIX_CHECK_EQ(messageAt(ring, 0), std::string("first"));
    NOTRIX_CHECK_EQ(messageAt(ring, 1), std::string("second"));
    NOTRIX_CHECK_EQ(ring.at(1).timestampMillis, std::uint64_t(200));
    NOTRIX_CHECK(ring.at(1).level == Level::Warn);
}

NOTRIX_TEST(RingLog, OverwritesOldestWhenFull) {
    // Bounded by construction: logging must never be what exhausts RAM, least
    // of all during an incident when it is busiest.
    RingLog ring;
    for (int i = 0; i < RingLog::kCapacity + 5; ++i) {
        ring.info(static_cast<std::uint64_t>(i), "entry " + std::to_string(i));
    }

    NOTRIX_CHECK_EQ(ring.count(), RingLog::kCapacity);
    NOTRIX_CHECK_EQ(ring.totalWritten(), std::uint32_t(RingLog::kCapacity + 5));

    // The five oldest are gone; index 0 is now entry 5.
    NOTRIX_CHECK_EQ(messageAt(ring, 0), std::string("entry 5"));
    NOTRIX_CHECK_EQ(messageAt(ring, RingLog::kCapacity - 1),
                    std::string("entry " + std::to_string(RingLog::kCapacity + 4)));
}

NOTRIX_TEST(RingLog, LostHistoryIsVisible) {
    RingLog ring;
    for (int i = 0; i < 100; ++i) {
        ring.info(0, "x");
    }
    // The gap between these two is how much was dropped.
    NOTRIX_CHECK_EQ(ring.totalWritten(), std::uint32_t(100));
    NOTRIX_CHECK_EQ(ring.count(), RingLog::kCapacity);
}

NOTRIX_TEST(RingLog, TruncatesRatherThanDropping) {
    // A cut-off line still says what happened.
    RingLog ring;
    const std::string huge(RingLog::kMaxMessageBytes * 3, 'x');
    ring.error(0, huge);

    NOTRIX_CHECK_EQ(ring.count(), 1);
    NOTRIX_CHECK_EQ(messageAt(ring, 0).size(), RingLog::kMaxMessageBytes - 1);
}

NOTRIX_TEST(RingLog, MessageIsAlwaysTerminated) {
    RingLog ring;
    ring.info(0, std::string(RingLog::kMaxMessageBytes * 2, 'a'));
    NOTRIX_CHECK_EQ(ring.at(0).message[RingLog::kMaxMessageBytes - 1], '\0');
}

NOTRIX_TEST(RingLog, EmptyMessageIsAccepted) {
    RingLog ring;
    ring.info(0, "");
    NOTRIX_CHECK_EQ(ring.count(), 1);
    NOTRIX_CHECK(messageAt(ring, 0).empty());
}

NOTRIX_TEST(RingLog, FiltersBelowTheMinimumLevel) {
    // A shipped device should not spend its ring on trace chatter.
    RingLog ring;
    ring.setMinimumLevel(Level::Warn);

    ring.trace(0, "no");
    ring.debug(0, "no");
    ring.info(0, "no");
    ring.warn(0, "yes");
    ring.error(0, "yes");
    ring.fatal(0, "yes");

    NOTRIX_CHECK_EQ(ring.count(), 3);
    NOTRIX_CHECK_EQ(ring.totalWritten(), std::uint32_t(3));
    NOTRIX_CHECK_EQ(messageAt(ring, 0), std::string("yes"));
}

NOTRIX_TEST(RingLog, DefaultLevelKeepsInfoAndAbove) {
    RingLog ring;
    ring.debug(0, "filtered");
    ring.info(0, "kept");
    NOTRIX_CHECK_EQ(ring.count(), 1);
}

NOTRIX_TEST(RingLog, OutOfRangeReadsAreSafe) {
    RingLog ring;
    ring.info(0, "only");

    NOTRIX_CHECK(ring.at(-1).message[0] == '\0');
    NOTRIX_CHECK(ring.at(1).message[0] == '\0');
    NOTRIX_CHECK(ring.at(9999).message[0] == '\0');
}

NOTRIX_TEST(RingLog, ClearKeepsTheTotalCount) {
    // Clearing frees the window, not the record that entries existed.
    RingLog ring;
    ring.info(0, "a");
    ring.info(0, "b");
    ring.clear();

    NOTRIX_CHECK_EQ(ring.count(), 0);
    NOTRIX_CHECK_EQ(ring.totalWritten(), std::uint32_t(2));
}

NOTRIX_TEST(RingLog, EveryLevelHasAName) {
    for (int i = 0; i <= static_cast<int>(Level::Fatal); ++i) {
        const char* name = levelName(static_cast<Level>(i));
        NOTRIX_CHECK(name != nullptr && name[0] != '\0');
    }
}

NOTRIX_TEST(RingLog, FootprintStaysModest) {
    // This buffer competes with the framebuffer for RAM on the device, so its
    // size is a deliberate number rather than an accident.
    std::printf("        [sizeof] RingLog=%zu bytes (%d entries x %zu)\n", sizeof(RingLog),
                RingLog::kCapacity, RingLog::kMaxMessageBytes);
    NOTRIX_CHECK(sizeof(RingLog) <= 4096);
}
