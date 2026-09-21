// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/audio/Tone.h"

namespace notrix {
namespace audio {

void ToneGenerator::start(int frequencyHz, int durationMillis) noexcept {
    if (frequencyHz <= 0 || durationMillis <= 0 || sampleRate_ <= 0) {
        stop();
        return;
    }

    // Clamped rather than refused. A caller asking for 20 kHz on a 16 kHz
    // device has made a mistake, but silence is a worse answer than the
    // highest note the hardware can actually produce.
    const int highest = sampleRate_ / 2;
    if (frequencyHz > highest) {
        frequencyHz = highest;
    }

    halfPeriod_ = sampleRate_ / (frequencyHz * 2);
    if (halfPeriod_ < 1) {
        halfPeriod_ = 1;
    }
    phase_ = 0;
    high_ = true;

    // Bounded, per §38. Nothing on a clock needs a tone longer than this, and
    // an unbounded duration is a stuck buzzer waiting to happen.
    const int longestMillis = 10000;
    if (durationMillis > longestMillis) {
        durationMillis = longestMillis;
    }
    remaining_ = (sampleRate_ / 1000) * durationMillis;
}

void ToneGenerator::setVolumePercent(int percent) noexcept {
    volumePercent_ = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

int ToneGenerator::fill(std::int16_t* samples, int count) noexcept {
    if (samples == nullptr || count <= 0) {
        return 0;
    }

    const int written = remaining_ < count ? remaining_ : count;
    const int level = (kAmplitude * volumePercent_) / 100;

    for (int i = 0; i < written; ++i) {
        samples[i] = static_cast<std::int16_t>(high_ ? level : -level);
        if (++phase_ >= halfPeriod_) {
            phase_ = 0;
            high_ = !high_;
        }
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
