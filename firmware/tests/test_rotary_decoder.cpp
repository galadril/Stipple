// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/RotaryDecoder.h"

#include <vector>

#include "support/TestFramework.h"

using stipple::platform::tc002::decodeRotary;
using stipple::platform::tc002::Detent;

namespace {

int code(Detent d) { return static_cast<int>(d); }

/// The exact ABS_X sequence a real TC002 produced while its knob was turned,
/// read with firmware/tools/input_probe. Ten detents, twenty events.
const std::vector<std::int32_t>& capturedTurn() {
    static const std::vector<std::int32_t> values = {
        13, 11,  8, 1,  8, 1,  13, 11,  8, 1,
         8,  1,  8, 1,  13, 11, 13, 11, 13, 11,
    };
    return values;
}

int detentsIn(const std::vector<std::int32_t>& values) {
    int count = 0;
    for (const std::int32_t value : values) {
        if (decodeRotary(value) != Detent::None) {
            ++count;
        }
    }
    return count;
}

}  // namespace

STIPPLE_TEST(RotaryDecoder, OneDetentPerPairNotPerEvent) {
    // The bug this exists for: the first implementation ticked on every ABS_X
    // value, so one physical click moved the carousel two apps. Reported from
    // hardware as "it swaps 2 apps while 1 knob rotation section was done".
    const std::vector<std::int32_t>& turn = capturedTurn();

    STIPPLE_CHECK_EQ(static_cast<int>(turn.size()), 20);
    STIPPLE_CHECK_EQ(detentsIn(turn), 10);
}

STIPPLE_TEST(RotaryDecoder, TheTrailingHalfOfAPairIsSilent) {
    // 1 and 11 are real values the hardware sends; they are ignored on purpose
    // rather than unrecognised, and that distinction is the whole fix.
    STIPPLE_CHECK_EQ(code(decodeRotary(1)), code(Detent::None));
    STIPPLE_CHECK_EQ(code(decodeRotary(11)), code(Detent::None));
}

STIPPLE_TEST(RotaryDecoder, LeadingValuesCarryDirection) {
    STIPPLE_CHECK_EQ(code(decodeRotary(8)), code(Detent::Clockwise));
    STIPPLE_CHECK_EQ(code(decodeRotary(13)), code(Detent::CounterClockwise));
}

STIPPLE_TEST(RotaryDecoder, DirectionIsConsistentWithinTheCapture) {
    // Each pair must decode to exactly one detent, and to the direction its
    // leading value names - never one of each, which would make the carousel
    // oscillate instead of advance.
    const std::vector<std::int32_t>& turn = capturedTurn();

    for (std::size_t i = 0; i + 1 < turn.size(); i += 2) {
        const Detent lead = decodeRotary(turn[i]);
        const Detent trail = decodeRotary(turn[i + 1]);

        STIPPLE_CHECK(lead != Detent::None);
        STIPPLE_CHECK_EQ(code(trail), code(Detent::None));

        if (turn[i] == 8) {
            STIPPLE_CHECK_EQ(code(lead), code(Detent::Clockwise));
            STIPPLE_CHECK_EQ(turn[i + 1], 1);
        } else {
            STIPPLE_CHECK_EQ(code(lead), code(Detent::CounterClockwise));
            STIPPLE_CHECK_EQ(turn[i + 1], 11);
        }
    }
}

STIPPLE_TEST(RotaryDecoder, UnknownValuesAreNeverGuessedAt) {
    // A wrong direction is worse than a missed detent: it moves the carousel
    // the way the user did not turn.
    for (std::int32_t value = -5; value < 260; ++value) {
        if (value == 8 || value == 13) {
            continue;
        }
        STIPPLE_CHECK_EQ(code(decodeRotary(value)), code(Detent::None));
    }
}
