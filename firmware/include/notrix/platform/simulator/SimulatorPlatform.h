// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "notrix/platform/PlatformServices.h"

namespace notrix {
namespace platform {
namespace simulator {

/// The panel, off-device. Keeps the most recent frame so tests and the browser
/// emulator can read back exactly what was presented.
class SimulatorDisplay : public IFrameBufferDisplay {
public:
    void present(const Framebuffer& frame) override;

    void setBrightness(std::uint8_t brightness) override { brightness_ = brightness; }
    std::uint8_t brightness() const override { return brightness_; }

    /// The simulator has no throttle of its own, but it reports the device's
    /// limit anyway. Pacing the emulator to what the TC002 can actually sustain
    /// is the whole point of building animations here first (§9.4).
    int minimumFrameIntervalMillis() const override { return 15; }

    const Framebuffer& lastFrame() const { return lastFrame_; }
    std::uint32_t presentCount() const { return presentCount_; }

private:
    Framebuffer lastFrame_;
    std::uint32_t presentCount_ = 0;
    std::uint8_t brightness_ = 255;
};

/// Input queue fed by the emulator UI or by tests.
///
/// Fixed capacity with oldest-dropped overflow, matching the contract in
/// IInputDevice: a stuck button must not be able to hide later presses, and
/// nothing here may allocate without bound (§38).
class SimulatorInput : public IInputDevice {
public:
    static constexpr std::size_t kCapacity = 32;

    bool poll(InputEvent& event) override;
    std::uint32_t droppedEventCount() const override { return dropped_; }

    /// Inject a raw event, as the hardware would.
    void push(const InputEvent& event);

    /// Convenience for a complete press: Down then Up `durationMillis` apart.
    void pressAndRelease(RawInput source, std::uint64_t downMillis, std::uint64_t durationMillis);

    /// Convenience for one rotary detent.
    void rotate(bool clockwise, std::uint64_t timestampMillis);

    std::size_t pending() const { return size_; }
    void clear();

private:
    InputEvent buffer_[kCapacity];
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint32_t dropped_ = 0;
};

/// A clock the test drives.
///
/// Time only moves when `advance()` is called, which is what lets long-press
/// thresholds, animation timing and app durations be tested exactly and
/// instantly instead of with sleeps and tolerances.
class SimulatorClock : public ISystemClock {
public:
    std::uint64_t monotonicMillis() const override { return monotonicMillis_; }
    bool wallClockValid() const override { return wallClockValid_; }
    std::int64_t unixSeconds() const override { return unixSeconds_; }
    int utcOffsetSeconds() const override { return utcOffsetSeconds_; }

    void advance(std::uint64_t millis);

    /// Starts invalid on purpose, mirroring a device that has booted but not yet
    /// reached NTP.
    void setWallClock(std::int64_t unixSeconds, int utcOffsetSeconds = 0);
    void invalidateWallClock() { wallClockValid_ = false; }

private:
    std::uint64_t monotonicMillis_ = 0;
    std::int64_t unixSeconds_ = 0;
    /// Sub-second remainder, so repeated small advances still add up to whole
    /// seconds instead of being rounded away each time.
    std::uint64_t pendingWallMillis_ = 0;
    int utcOffsetSeconds_ = 0;
    bool wallClockValid_ = false;
};

/// In-memory key/value store. Writes are trivially atomic here; the device
/// implementation is where that has to be earned.
class SimulatorStorage : public IStorage {
public:
    static constexpr std::size_t kMaxValueBytes = 64 * 1024;

    bool exists(std::string_view key) const override;
    bool read(std::string_view key, std::string& out) const override;
    bool write(std::string_view key, std::string_view value) override;
    bool remove(std::string_view key) override;
    std::size_t maxValueBytes() const override { return kMaxValueBytes; }

    std::size_t keyCount() const { return entries_.size(); }
    void clear() { entries_.clear(); }

private:
    std::map<std::string, std::string, std::less<>> entries_;
};

/// Records playback requests; makes no sound.
///
/// Honest about what it is: this verifies that NOTRIX *asked* for a sound at the
/// right moment, which is our logic and worth testing. It says nothing about
/// whether the TC002 speaker works — that is Phase 7's problem.
class SimulatorAudio : public IAudioOutput {
public:
    struct Request {
        bool isTone = false;
        int frequencyHz = 0;
        int durationMillis = 0;
        std::string sound;
    };

    bool playTone(int frequencyHz, int durationMillis) override;
    bool playSound(std::string_view name) override;
    void stop() override;

    void setVolume(std::uint8_t volume) override { volume_ = volume; }
    std::uint8_t volume() const override { return volume_; }

    const std::vector<Request>& requests() const { return requests_; }
    std::uint32_t stopCount() const { return stopCount_; }
    void clear() { requests_.clear(); }

private:
    std::vector<Request> requests_;
    std::uint32_t stopCount_ = 0;
    std::uint8_t volume_ = 128;
};

/// Reports whatever status the test sets.
class SimulatorNetwork : public INetworkManager {
public:
    NetworkStatus status() const override { return status_; }
    void setStatus(const NetworkStatus& status) { status_ = status; }

private:
    NetworkStatus status_;
};

/// Records the request. Emphatically does not reboot anything.
class SimulatorRebooter : public IRebooter {
public:
    void reboot() override { ++rebootCount_; }
    std::uint32_t rebootCount() const { return rebootCount_; }

private:
    std::uint32_t rebootCount_ = 0;
};

/// Which optional capabilities this simulated device claims to have.
///
/// Turning one off makes the matching accessor return nullptr, so tests can
/// exercise the path where a platform genuinely lacks a capability. Without
/// this the nullptr branches would never run off-device and would first be
/// discovered on real hardware.
struct SimulatorCapabilities {
    bool audio = true;
    bool network = true;
    bool rebooter = true;
};

/// Complete simulator implementation of the §53 platform boundary.
class SimulatorPlatform : public IPlatformServices {
public:
    explicit SimulatorPlatform(const SimulatorCapabilities& capabilities = SimulatorCapabilities{})
        : capabilities_(capabilities) {}

    const char* name() const override { return "simulator"; }

    IFrameBufferDisplay& display() override { return display_; }
    IInputDevice& input() override { return input_; }
    ISystemClock& clock() override { return clock_; }
    IStorage& storage() override { return storage_; }

    IAudioOutput* audio() override { return capabilities_.audio ? &audio_ : nullptr; }
    INetworkManager* network() override { return capabilities_.network ? &network_ : nullptr; }
    IRebooter* rebooter() override { return capabilities_.rebooter ? &rebooter_ : nullptr; }

    // Concrete accessors for tests and the emulator shell, which need the
    // simulator-only controls that the interfaces deliberately do not expose.
    SimulatorDisplay& simulatedDisplay() { return display_; }
    SimulatorInput& simulatedInput() { return input_; }
    SimulatorClock& simulatedClock() { return clock_; }
    SimulatorStorage& simulatedStorage() { return storage_; }
    SimulatorAudio& simulatedAudio() { return audio_; }
    SimulatorNetwork& simulatedNetwork() { return network_; }
    SimulatorRebooter& simulatedRebooter() { return rebooter_; }

private:
    SimulatorCapabilities capabilities_;
    SimulatorDisplay display_;
    SimulatorInput input_;
    SimulatorClock clock_;
    SimulatorStorage storage_;
    SimulatorAudio audio_;
    SimulatorNetwork network_;
    SimulatorRebooter rebooter_;
};

}  // namespace simulator
}  // namespace platform
}  // namespace notrix
