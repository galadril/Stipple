// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/simulator/SimulatorPlatform.h"

#include "stipple/graphics/Canvas.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::platform::ButtonPhase;
using stipple::platform::InputEvent;
using stipple::platform::IPlatformServices;
using stipple::platform::NetworkStatus;
using stipple::platform::RawInput;
using stipple::platform::simulator::SimulatorCapabilities;
using stipple::platform::simulator::SimulatorPlatform;
namespace colors = stipple::colors;

// --- display ----------------------------------------------------------------

STIPPLE_TEST(SimulatorPlatform, PresentStoresTheFrame) {
    SimulatorPlatform platform;

    Framebuffer frame;
    Canvas canvas(frame);
    canvas.pixel(3, 4, colors::kRed);

    platform.display().present(frame);

    STIPPLE_CHECK_EQ(platform.simulatedDisplay().presentCount(), 1u);
    STIPPLE_CHECK_EQ(platform.simulatedDisplay().lastFrame().at(3, 4), colors::kRed);
}

STIPPLE_TEST(SimulatorPlatform, PresentedFrameIsACopyNotAReference) {
    // IFrameBufferDisplay forbids retaining the caller's framebuffer: there is
    // only one, and the caller reuses it for the next frame.
    SimulatorPlatform platform;

    Framebuffer frame;
    Canvas canvas(frame);
    canvas.pixel(0, 0, colors::kGreen);
    platform.display().present(frame);

    frame.clear();

    STIPPLE_CHECK_EQ(platform.simulatedDisplay().lastFrame().at(0, 0), colors::kGreen);
}

STIPPLE_TEST(SimulatorPlatform, BrightnessRoundTrips) {
    SimulatorPlatform platform;
    platform.display().setBrightness(42);
    STIPPLE_CHECK_EQ(static_cast<int>(platform.display().brightness()), 42);
}

STIPPLE_TEST(SimulatorPlatform, ReportsTheDeviceFrameIntervalLimit) {
    // The simulator has no throttle, but reporting the device's limit is what
    // stops animations being designed that the TC002 cannot sustain.
    SimulatorPlatform platform;
    STIPPLE_CHECK(platform.display().minimumFrameIntervalMillis() >= 15);
}

// --- input ------------------------------------------------------------------

STIPPLE_TEST(SimulatorPlatform, InputIsDeliveredInOrder) {
    SimulatorPlatform platform;
    platform.simulatedInput().pressAndRelease(RawInput::KeyMinus, 100, 50);

    InputEvent event;
    STIPPLE_CHECK(platform.input().poll(event));
    STIPPLE_CHECK(event.phase == ButtonPhase::Down);
    STIPPLE_CHECK_EQ(event.timestampMillis, std::uint64_t(100));

    STIPPLE_CHECK(platform.input().poll(event));
    STIPPLE_CHECK(event.phase == ButtonPhase::Up);
    STIPPLE_CHECK_EQ(event.timestampMillis, std::uint64_t(150));

    STIPPLE_CHECK_FALSE(platform.input().poll(event));
}

STIPPLE_TEST(SimulatorPlatform, EmptyQueuePollsFalse) {
    SimulatorPlatform platform;
    InputEvent event;
    STIPPLE_CHECK_FALSE(platform.input().poll(event));
}

STIPPLE_TEST(SimulatorPlatform, QueueOverflowDropsTheOldestEvent) {
    // Blueprint §38 forbids unbounded queues. Dropping the newest instead would
    // let one stuck control mask every press that follows it.
    SimulatorPlatform platform;
    auto& input = platform.simulatedInput();

    const std::size_t capacity = stipple::platform::simulator::SimulatorInput::kCapacity;
    for (std::size_t i = 0; i < capacity + 3; ++i) {
        input.push(InputEvent({RawInput::RotaryPress, ButtonPhase::Tick, static_cast<std::uint64_t>(i)}));
    }

    STIPPLE_CHECK_EQ(input.pending(), capacity);
    STIPPLE_CHECK_EQ(input.droppedEventCount(), 3u);

    // The three oldest are gone, so the queue now starts at timestamp 3.
    InputEvent event;
    STIPPLE_CHECK(input.poll(event));
    STIPPLE_CHECK_EQ(event.timestampMillis, std::uint64_t(3));
}

STIPPLE_TEST(SimulatorPlatform, RotateProducesTickEvents) {
    SimulatorPlatform platform;
    platform.simulatedInput().rotate(true, 500);

    InputEvent event;
    STIPPLE_CHECK(platform.input().poll(event));
    STIPPLE_CHECK(event.source == RawInput::RotaryRight);
    STIPPLE_CHECK(event.phase == ButtonPhase::Tick);
}

// --- clock ------------------------------------------------------------------

STIPPLE_TEST(SimulatorPlatform, MonotonicTimeOnlyMovesWhenAdvanced) {
    SimulatorPlatform platform;
    STIPPLE_CHECK_EQ(platform.clock().monotonicMillis(), std::uint64_t(0));

    platform.simulatedClock().advance(250);
    STIPPLE_CHECK_EQ(platform.clock().monotonicMillis(), std::uint64_t(250));

    platform.simulatedClock().advance(250);
    STIPPLE_CHECK_EQ(platform.clock().monotonicMillis(), std::uint64_t(500));
}

STIPPLE_TEST(SimulatorPlatform, WallClockStartsInvalid) {
    // A freshly booted device has not reached NTP yet. Rendering 01:00 because
    // the wall clock was trusted too early is exactly what this prevents.
    SimulatorPlatform platform;
    STIPPLE_CHECK_FALSE(platform.clock().wallClockValid());
}

STIPPLE_TEST(SimulatorPlatform, WallClockBecomesValidOnceSet) {
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000, 3600);

    STIPPLE_CHECK(platform.clock().wallClockValid());
    STIPPLE_CHECK_EQ(platform.clock().unixSeconds(), std::int64_t(1'700'000'000));
    STIPPLE_CHECK_EQ(platform.clock().utcOffsetSeconds(), 3600);
}

STIPPLE_TEST(SimulatorPlatform, WallClockAdvancesWithMonotonicTime) {
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1000);
    platform.simulatedClock().advance(2500);

    STIPPLE_CHECK_EQ(platform.clock().unixSeconds(), std::int64_t(1002));
}

STIPPLE_TEST(SimulatorPlatform, SubSecondAdvancesAccumulate) {
    // Ten 100 ms frames are one second, not zero. Rounding each advance down
    // would make the clock lose time under animation.
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1000);

    for (int i = 0; i < 10; ++i) {
        platform.simulatedClock().advance(100);
    }

    STIPPLE_CHECK_EQ(platform.clock().unixSeconds(), std::int64_t(1001));
}

STIPPLE_TEST(SimulatorPlatform, InvalidWallClockDoesNotAdvance) {
    SimulatorPlatform platform;
    platform.simulatedClock().advance(5000);
    STIPPLE_CHECK_FALSE(platform.clock().wallClockValid());
}

// --- storage ----------------------------------------------------------------

STIPPLE_TEST(SimulatorPlatform, StorageRoundTrips) {
    SimulatorPlatform platform;
    auto& storage = platform.storage();

    STIPPLE_CHECK(storage.write("config", "{\"schemaVersion\":1}"));
    STIPPLE_CHECK(storage.exists("config"));

    std::string value;
    STIPPLE_CHECK(storage.read("config", value));
    STIPPLE_CHECK_EQ(value, std::string("{\"schemaVersion\":1}"));
}

STIPPLE_TEST(SimulatorPlatform, ReadingAMissingKeyFailsAndClearsOutput) {
    SimulatorPlatform platform;

    std::string value = "stale";
    STIPPLE_CHECK_FALSE(platform.storage().read("absent", value));
    STIPPLE_CHECK(value.empty());
}

STIPPLE_TEST(SimulatorPlatform, WriteReplacesPreviousValue) {
    SimulatorPlatform platform;
    auto& storage = platform.storage();

    storage.write("key", "first");
    storage.write("key", "second");

    std::string value;
    storage.read("key", value);
    STIPPLE_CHECK_EQ(value, std::string("second"));
    STIPPLE_CHECK_EQ(platform.simulatedStorage().keyCount(), std::size_t(1));
}

STIPPLE_TEST(SimulatorPlatform, RemoveDeletesOnlyTheNamedKey) {
    SimulatorPlatform platform;
    auto& storage = platform.storage();

    storage.write("a", "1");
    storage.write("b", "2");

    STIPPLE_CHECK(storage.remove("a"));
    STIPPLE_CHECK_FALSE(storage.exists("a"));
    STIPPLE_CHECK(storage.exists("b"));
    STIPPLE_CHECK_FALSE(storage.remove("a"));  // already gone
}

STIPPLE_TEST(SimulatorPlatform, OversizedValueIsRejected) {
    // Bounded by design: callers check the limit rather than discovering it.
    SimulatorPlatform platform;
    auto& storage = platform.storage();

    const std::string tooBig(storage.maxValueBytes() + 1, 'x');
    STIPPLE_CHECK_FALSE(storage.write("big", tooBig));
    STIPPLE_CHECK_FALSE(storage.exists("big"));
}

STIPPLE_TEST(SimulatorPlatform, EmptyKeyIsRejected) {
    SimulatorPlatform platform;
    STIPPLE_CHECK_FALSE(platform.storage().write("", "value"));
}

// --- optional capabilities ---------------------------------------------------

STIPPLE_TEST(SimulatorPlatform, OptionalCapabilitiesArePresentByDefault) {
    SimulatorPlatform platform;
    STIPPLE_CHECK(platform.audio() != nullptr);
    STIPPLE_CHECK(platform.network() != nullptr);
    STIPPLE_CHECK(platform.rebooter() != nullptr);
}

STIPPLE_TEST(SimulatorPlatform, DisabledCapabilitiesReturnNullptr) {
    // The nullptr branch must be reachable off-device. If it were not, code
    // that assumes a capability exists would first fail on real hardware.
    SimulatorCapabilities none;
    none.audio = false;
    none.network = false;
    none.rebooter = false;

    SimulatorPlatform platform(none);
    STIPPLE_CHECK(platform.audio() == nullptr);
    STIPPLE_CHECK(platform.network() == nullptr);
    STIPPLE_CHECK(platform.rebooter() == nullptr);

    // Required services are still there — those are never optional.
    IPlatformServices& services = platform;
    STIPPLE_CHECK_EQ(services.display().minimumFrameIntervalMillis(), 15);
}

STIPPLE_TEST(SimulatorPlatform, AudioRecordsRequestsWithoutMakingSound) {
    SimulatorPlatform platform;
    auto* audio = platform.audio();

    STIPPLE_CHECK(audio->playTone(440, 100));
    STIPPLE_CHECK(audio->playSound("notify"));
    STIPPLE_CHECK_FALSE(audio->playTone(0, 100));    // invalid frequency
    STIPPLE_CHECK_FALSE(audio->playSound(""));       // unnamed

    const auto& requests = platform.simulatedAudio().requests();
    STIPPLE_CHECK_EQ(requests.size(), std::size_t(2));
    STIPPLE_CHECK(requests[0].isTone);
    STIPPLE_CHECK_EQ(requests[0].frequencyHz, 440);
    STIPPLE_CHECK_EQ(requests[1].sound, std::string("notify"));
}

STIPPLE_TEST(SimulatorPlatform, NetworkStatusIsWhateverWasSet) {
    SimulatorPlatform platform;

    STIPPLE_CHECK_FALSE(platform.network()->status().connected);

    NetworkStatus online;
    online.connected = true;
    online.rssiDbm = -51;
    online.ipv4 = "192.168.1.42";
    platform.simulatedNetwork().setStatus(online);

    STIPPLE_CHECK(platform.network()->status().connected);
    STIPPLE_CHECK_EQ(platform.network()->status().ipv4, std::string("192.168.1.42"));
}

STIPPLE_TEST(SimulatorPlatform, RebooterRecordsRatherThanReboots) {
    SimulatorPlatform platform;
    STIPPLE_CHECK_EQ(platform.simulatedRebooter().rebootCount(), 0u);

    platform.rebooter()->reboot();
    STIPPLE_CHECK_EQ(platform.simulatedRebooter().rebootCount(), 1u);
}

STIPPLE_TEST(SimulatorPlatform, IdentifiesItselfForDiagnostics) {
    SimulatorPlatform platform;
    STIPPLE_CHECK_EQ(std::string(platform.name()), std::string("simulator"));
}
