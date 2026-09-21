// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace notrix {
namespace audio {

/// Generates PCM for a tone, a frame at a time.
///
/// Core rather than platform, and pure: what a beep sounds like is not a fact
/// about SigmaStar hardware, and the arithmetic that decides it is exactly the
/// kind of thing that should be testable without a speaker in the room.
///
/// Pulled a frame at a time rather than rendered into a buffer up front,
/// because blueprint §16 says audio must never block rendering: the device
/// hands the driver one short frame per tick and returns, instead of sitting in
/// a loop for the length of the sound.
class ToneGenerator {
public:
    /// Sample rate the caller will play at. The TC002 runs its speaker at
    /// 16 kHz - read off the vendor application's own configuration rather
    /// than chosen - but nothing here depends on that number.
    explicit ToneGenerator(int sampleRate = 16000) noexcept : sampleRate_(sampleRate) {}

    /// Start a tone. Replaces whatever was playing: a device that queued beeps
    /// would fall behind the thing it is beeping about.
    ///
    /// `gainPermille` scales this one sound relative to the volume setting,
    /// for sounds that should not be as loud as a deliberate beep. A clock
    /// tick is the case it exists for: at the same level as a volume
    /// confirmation it would be unbearable once a second, and turning the
    /// whole device down instead would make everything else inaudible.
    void start(int frequencyHz, int durationMillis, int gainPermille = 1000) noexcept;

    /// Stop immediately.
    void stop() noexcept { remaining_ = 0; }

    bool playing() const noexcept { return remaining_ > 0; }

    /// Samples still to produce.
    int remaining() const noexcept { return remaining_; }

    /// 0-100. Applied here rather than through the hardware's own volume
    /// control, which on this device refuses every value it was offered.
    void setVolumePercent(int percent) noexcept;
    int volumePercent() const noexcept { return volumePercent_; }

    /// Fill up to `count` samples. Returns how many were written; a short
    /// return means the tone ended inside this frame.
    int fill(std::int16_t* samples, int count) noexcept;

private:
    int sampleRate_;
    int remaining_ = 0;
    int total_ = 0;

    /// Fixed-point phase into the sine table, 16 bits of fraction.
    ///
    /// A square wave came first and was the obvious thing to generate - count
    /// to a half period, flip - but it sounds like a square wave: all the odd
    /// harmonics, and on a small speaker that is a buzz rather than a note. A
    /// sine costs one table lookup and an add per sample, which is nothing
    /// even here.
    std::uint32_t phase_ = 0;
    std::uint32_t phaseStep_ = 0;

    int volumePercent_ = 60;

    /// Per-sound scale, 0-1000. Reset by every start().
    int gainPermille_ = 1000;

    /// Peak sample value at full volume.
    ///
    /// Deliberately short of full scale. This drives a small speaker in a
    /// clock, and a tone at full amplitude is both unpleasant and the sort of
    /// thing that makes people turn a device off rather than down.
    static constexpr int kAmplitude = 9000;

    /// Fade in and out, in samples.
    ///
    /// A tone that starts and stops at full amplitude begins and ends with a
    /// step, and a step is a click - the loudest, harshest part of a short
    /// beep. Ramping over a couple of milliseconds removes it entirely and is
    /// inaudible as a fade.
    static constexpr int kRampSamples = 48;
};

}  // namespace audio
}  // namespace notrix
