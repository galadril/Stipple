// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/core/Log.h"

#include <string>

#include "support/TestFramework.h"

using stipple::log::Level;
using stipple::log::levelName;
using stipple::log::RingLog;

namespace {

std::string messageAt(const RingLog& ring, int index) {
    return std::string(ring.at(index).message);
}

}  // namespace

STIPPLE_TEST(RingLog, StoresInOrder) {
    RingLog ring;
    ring.info(100, "first");
    ring.warn(200, "second");

    STIPPLE_CHECK_EQ(ring.count(), 2);
    STIPPLE_CHECK_EQ(messageAt(ring, 0), std::string("first"));
    STIPPLE_CHECK_EQ(messageAt(ring, 1), std::string("second"));
    STIPPLE_CHECK_EQ(ring.at(1).timestampMillis, std::uint64_t(200));
    STIPPLE_CHECK(ring.at(1).level == Level::Warn);
}

STIPPLE_TEST(RingLog, OverwritesOldestWhenFull) {
    // Bounded by construction: logging must never be what exhausts RAM, least
    // of all during an incident when it is busiest.
    RingLog ring;
    for (int i = 0; i < RingLog::kCapacity + 5; ++i) {
        ring.info(static_cast<std::uint64_t>(i), "entry " + std::to_string(i));
    }

    STIPPLE_CHECK_EQ(ring.count(), RingLog::kCapacity);
    STIPPLE_CHECK_EQ(ring.totalWritten(), std::uint32_t(RingLog::kCapacity + 5));

    // The five oldest are gone; index 0 is now entry 5.
    STIPPLE_CHECK_EQ(messageAt(ring, 0), std::string("entry 5"));
    STIPPLE_CHECK_EQ(messageAt(ring, RingLog::kCapacity - 1),
                    std::string("entry " + std::to_string(RingLog::kCapacity + 4)));
}

STIPPLE_TEST(RingLog, LostHistoryIsVisible) {
    RingLog ring;
    for (int i = 0; i < 100; ++i) {
        ring.info(0, "x");
    }
    // The gap between these two is how much was dropped.
    STIPPLE_CHECK_EQ(ring.totalWritten(), std::uint32_t(100));
    STIPPLE_CHECK_EQ(ring.count(), RingLog::kCapacity);
}

STIPPLE_TEST(RingLog, TruncatesRatherThanDropping) {
    // A cut-off line still says what happened.
    RingLog ring;
    const std::string huge(RingLog::kMaxMessageBytes * 3, 'x');
    ring.error(0, huge);

    STIPPLE_CHECK_EQ(ring.count(), 1);
    STIPPLE_CHECK_EQ(messageAt(ring, 0).size(), RingLog::kMaxMessageBytes - 1);
}

STIPPLE_TEST(RingLog, MessageIsAlwaysTerminated) {
    RingLog ring;
    ring.info(0, std::string(RingLog::kMaxMessageBytes * 2, 'a'));
    STIPPLE_CHECK_EQ(ring.at(0).message[RingLog::kMaxMessageBytes - 1], '\0');
}

STIPPLE_TEST(RingLog, EmptyMessageIsAccepted) {
    RingLog ring;
    ring.info(0, "");
    STIPPLE_CHECK_EQ(ring.count(), 1);
    STIPPLE_CHECK(messageAt(ring, 0).empty());
}

STIPPLE_TEST(RingLog, FiltersBelowTheMinimumLevel) {
    // A shipped device should not spend its ring on trace chatter.
    RingLog ring;
    ring.setMinimumLevel(Level::Warn);

    ring.trace(0, "no");
    ring.debug(0, "no");
    ring.info(0, "no");
    ring.warn(0, "yes");
    ring.error(0, "yes");
    ring.fatal(0, "yes");

    STIPPLE_CHECK_EQ(ring.count(), 3);
    STIPPLE_CHECK_EQ(ring.totalWritten(), std::uint32_t(3));
    STIPPLE_CHECK_EQ(messageAt(ring, 0), std::string("yes"));
}

STIPPLE_TEST(RingLog, DefaultLevelKeepsInfoAndAbove) {
    RingLog ring;
    ring.debug(0, "filtered");
    ring.info(0, "kept");
    STIPPLE_CHECK_EQ(ring.count(), 1);
}

STIPPLE_TEST(RingLog, OutOfRangeReadsAreSafe) {
    RingLog ring;
    ring.info(0, "only");

    STIPPLE_CHECK(ring.at(-1).message[0] == '\0');
    STIPPLE_CHECK(ring.at(1).message[0] == '\0');
    STIPPLE_CHECK(ring.at(9999).message[0] == '\0');
}

STIPPLE_TEST(RingLog, ClearKeepsTheTotalCount) {
    // Clearing frees the window, not the record that entries existed.
    RingLog ring;
    ring.info(0, "a");
    ring.info(0, "b");
    ring.clear();

    STIPPLE_CHECK_EQ(ring.count(), 0);
    STIPPLE_CHECK_EQ(ring.totalWritten(), std::uint32_t(2));
}

STIPPLE_TEST(RingLog, EveryLevelHasAName) {
    for (int i = 0; i <= static_cast<int>(Level::Fatal); ++i) {
        const char* name = levelName(static_cast<Level>(i));
        STIPPLE_CHECK(name != nullptr && name[0] != '\0');
    }
}

STIPPLE_TEST(RingLog, FootprintStaysModest) {
    // This buffer competes with the framebuffer for RAM on the device, so its
    // size is a deliberate number rather than an accident.
    std::printf("        [sizeof] RingLog=%zu bytes (%d entries x %zu)\n", sizeof(RingLog),
                RingLog::kCapacity, RingLog::kMaxMessageBytes);
    STIPPLE_CHECK(sizeof(RingLog) <= 4096);
}
