// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/audio/Tone.h"

#include "support/TestFramework.h"

using stipple::audio::ToneGenerator;

namespace {

/// Counts sign changes, which for a square wave is twice the frequency.
int crossings(const std::int16_t* samples, int count) {
    int changes = 0;
    for (int i = 1; i < count; ++i) {
        const bool before = samples[i - 1] >= 0;
        const bool now = samples[i] >= 0;
        if (before != now) {
            ++changes;
        }
    }
    return changes;
}

}  // namespace

STIPPLE_TEST(Tone, SilentUntilAskedForSomething) {
    ToneGenerator tone;
    STIPPLE_CHECK_FALSE(tone.playing());

    std::int16_t samples[64];
    STIPPLE_CHECK_EQ(tone.fill(samples, 64), 0);
    for (int i = 0; i < 64; ++i) {
        STIPPLE_CHECK_EQ(static_cast<int>(samples[i]), 0);
    }
}

STIPPLE_TEST(Tone, DurationDecidesHowManySamplesArrive) {
    ToneGenerator tone(16000);
    tone.start(1000, 100);  // 100 ms at 16 kHz
    STIPPLE_CHECK_EQ(tone.remaining(), 1600);

    std::int16_t samples[1600];
    STIPPLE_CHECK_EQ(tone.fill(samples, 1600), 1600);
    STIPPLE_CHECK_FALSE(tone.playing());
}

STIPPLE_TEST(Tone, TheFrequencyAskedForIsTheFrequencyProduced) {
    ToneGenerator tone(16000);
    tone.start(1000, 1000);

    std::int16_t samples[16000];
    tone.fill(samples, 16000);

    // A second of a 1 kHz square wave crosses zero about 2000 times. Integer
    // half-periods make it approximate, so this allows a few percent.
    const int changes = crossings(samples, 16000);
    STIPPLE_CHECK(changes > 1900);
    STIPPLE_CHECK(changes < 2100);
}

STIPPLE_TEST(Tone, VolumeScalesTheSamples) {
    // The hardware's own volume control refuses every value it was offered, so
    // this is the only volume the device has.
    auto peak = [](int percent) {
        ToneGenerator tone(16000);
        tone.setVolumePercent(percent);
        tone.start(1000, 50);
        std::int16_t samples[800];
        tone.fill(samples, 800);
        int loudest = 0;
        for (int i = 0; i < 800; ++i) {
            const int magnitude = samples[i] < 0 ? -samples[i] : samples[i];
            if (magnitude > loudest) { loudest = magnitude; }
        }
        return loudest;
    };

    STIPPLE_CHECK(peak(100) > peak(50));
    STIPPLE_CHECK(peak(50) > peak(10));
    STIPPLE_CHECK_EQ(peak(0), 0);
}

STIPPLE_TEST(Tone, AShortToneZeroFillsTheRestOfItsFrame) {
    // Frames are a fixed size on this hardware, so the alternative to zeroing
    // the tail is whatever the buffer held last time - a click at the end of
    // every beep.
    ToneGenerator tone(16000);
    tone.start(1000, 1);  // 16 samples

    std::int16_t samples[128];
    for (int i = 0; i < 128; ++i) { samples[i] = 999; }

    STIPPLE_CHECK_EQ(tone.fill(samples, 128), 16);
    for (int i = 16; i < 128; ++i) {
        STIPPLE_CHECK_EQ(static_cast<int>(samples[i]), 0);
    }
}

STIPPLE_TEST(Tone, StartingAgainReplacesRatherThanQueues) {
    // A device that queued beeps would fall behind the thing it is beeping
    // about, and the queue is unbounded by nature.
    ToneGenerator tone(16000);
    tone.start(1000, 1000);
    tone.start(1000, 10);
    STIPPLE_CHECK_EQ(tone.remaining(), 160);
}

STIPPLE_TEST(Tone, NonsenseIsRefusedRatherThanRendered) {
    ToneGenerator tone(16000);

    tone.start(0, 100);
    STIPPLE_CHECK_FALSE(tone.playing());

    tone.start(1000, 0);
    STIPPLE_CHECK_FALSE(tone.playing());

    tone.start(-5, 100);
    STIPPLE_CHECK_FALSE(tone.playing());
}

STIPPLE_TEST(Tone, DurationIsBounded) {
    // §38: nothing on a clock needs a longer tone, and an unbounded one is a
    // stuck buzzer waiting to happen.
    ToneGenerator tone(16000);
    tone.start(1000, 60 * 60 * 1000);
    STIPPLE_CHECK(tone.remaining() <= 16000 * 10);
}

STIPPLE_TEST(Tone, AFrequencyAboveWhatTheRateCanCarryIsClampedNotSilenced) {
    // Silence is a worse answer than the highest note the hardware can make.
    ToneGenerator tone(16000);
    tone.start(20000, 100);
    STIPPLE_CHECK(tone.playing());
}
