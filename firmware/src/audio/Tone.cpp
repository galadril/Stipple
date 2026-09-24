// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/audio/Tone.h"

namespace notrix {
namespace audio {
namespace {

/// sin(2*pi*i/64) scaled by 1000, a quarter-cycle at a time by symmetry.
///
/// A table rather than <cmath>: core has no dependencies, this runs per sample
/// on a Cortex-A7, and a beep does not need more resolution than this. At the
/// tones a clock plays the error is far below what a small speaker reproduces.
constexpr int kSineSteps = 64;
constexpr int kSine[kSineSteps] = {
        0,    98,   195,   290,   383,   471,   556,   634,
      707,   773,   831,   882,   924,   957,   981,   995,
     1000,   995,   981,   957,   924,   882,   831,   773,
      707,   634,   556,   471,   383,   290,   195,    98,
        0,   -98,  -195,  -290,  -383,  -471,  -556,  -634,
     -707,  -773,  -831,  -882,  -924,  -957,  -981,  -995,
    -1000,  -995,  -981,  -957,  -924,  -882,  -831,  -773,
     -707,  -634,  -556,  -471,  -383,  -290,  -195,   -98,
};

}  // namespace

void ToneGenerator::start(int frequencyHz, int durationMillis,
                          int gainPermille) noexcept {
    if (frequencyHz <= 0 || durationMillis <= 0 || sampleRate_ <= 0) {
        stop();
        return;
    }

    gainPermille_ = gainPermille < 0 ? 0 : (gainPermille > 1000 ? 1000 : gainPermille);

    // Clamped rather than refused. A caller asking for 20 kHz on a 16 kHz
    // device has made a mistake, but silence is a worse answer than the
    // highest note the hardware can actually produce.
    const int highest = sampleRate_ / 2;
    if (frequencyHz > highest) {
        frequencyHz = highest;
    }

    // How far through the table each sample advances, 16.16 fixed point.
    phaseStep_ = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(frequencyHz) * kSineSteps * 65536u) /
        static_cast<std::uint32_t>(sampleRate_));
    phase_ = 0;

    // Bounded, per §38. Nothing on a clock needs a tone longer than this, and
    // an unbounded duration is a stuck buzzer waiting to happen.
    const int longestMillis = 10000;
    if (durationMillis > longestMillis) {
        durationMillis = longestMillis;
    }
    total_ = (sampleRate_ / 1000) * durationMillis;
    remaining_ = total_;
}

void ToneGenerator::setVolumePercent(int percent) noexcept {
    volumePercent_ = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

int ToneGenerator::fill(std::int16_t* samples, int count) noexcept {
    if (samples == nullptr || count <= 0) {
        return 0;
    }

    const int written = remaining_ < count ? remaining_ : count;
    const int level = ((kAmplitude * volumePercent_) / 100) * gainPermille_ / 1000;

    // Never longer than a quarter of the tone, so a very short beep still
    // fades rather than becoming one long ramp with no note in the middle.
    int ramp = kRampSamples;
    if (ramp > total_ / 4) {
        ramp = total_ / 4;
    }

    for (int i = 0; i < written; ++i) {
        const int index = static_cast<int>((phase_ >> 16) % kSineSteps);
        int value = (kSine[index] * level) / 1000;

        // Fade in at the start and out at the end. Without this a beep begins
        // and ends on a step, and a step is a click - which on a short tone is
        // the loudest thing in it.
        if (ramp > 0) {
            const int done = total_ - remaining_ + i;
            const int left = remaining_ - i;
            if (done < ramp) {
                value = (value * done) / ramp;
            } else if (left < ramp) {
                value = (value * left) / ramp;
            }
        }

        samples[i] = static_cast<std::int16_t>(value);
        phase_ += phaseStep_;
    }

    remaining_ -= written;

    // The tail of a short tone is zero-filled rather than left as whatever the
    // caller's buffer held. A frame is a fixed size on this hardware, so the
    // alternative is a click at the end of every beep.
    for (int i = written; i < count; ++i) {
        samples[i] = 0;
    }

    return written;
}

}  // namespace audio
}  // namespace notrix
