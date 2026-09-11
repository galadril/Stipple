// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/simulator/SimulatorPlatform.h"

#include "notrix/graphics/Canvas.h"
#include "support/TestFramework.h"

using notrix::Canvas;
using notrix::Framebuffer;
using notrix::platform::ButtonPhase;
using notrix::platform::InputEvent;
using notrix::platform::IPlatformServices;
using notrix::platform::NetworkStatus;
using notrix::platform::RawInput;
using notrix::platform::simulator::SimulatorCapabilities;
using notrix::platform::simulator::SimulatorPlatform;
namespace colors = notrix::colors;

// --- display ----------------------------------------------------------------

NOTRIX_TEST(SimulatorPlatform, PresentStoresTheFrame) {
    SimulatorPlatform platform;

    Framebuffer frame;
    Canvas canvas(frame);
    canvas.pixel(3, 4, colors::kRed);

    platform.display().present(frame);

    NOTRIX_CHECK_EQ(platform.simulatedDisplay().presentCount(), 1u);
    NOTRIX_CHECK_EQ(platform.simulatedDisplay().lastFrame().at(3, 4), colors::kRed);
}

NOTRIX_TEST(SimulatorPlatform, PresentedFrameIsACopyNotAReference) {
    // IFrameBufferDisplay forbids retaining the caller's framebuffer: there is
    // only one, and the caller reuses it for the next frame.
    SimulatorPlatform platform;

    Framebuffer frame;
    Canvas canvas(frame);
    canvas.pixel(0, 0, colors::kGreen);
    platform.display().present(frame);

    frame.clear();

    NOTRIX_CHECK_EQ(platform.simulatedDisplay().lastFrame().at(0, 0), colors::kGreen);
}

NOTRIX_TEST(SimulatorPlatform, BrightnessRoundTrips) {
    SimulatorPlatform platform;
    platform.display().setBrightness(42);
    NOTRIX_CHECK_EQ(static_cast<int>(platform.display().brightness()), 42);
}

NOTRIX_TEST(SimulatorPlatform, ReportsTheDeviceFrameIntervalLimit) {
    // The simulator has no throttle, but reporting the device's limit is what
    // stops animations being designed that the TC002 cannot sustain.
    SimulatorPlatform platform;
    NOTRIX_CHECK(platform.display().minimumFrameIntervalMillis() >= 15);
}

// --- input ------------------------------------------------------------------

NOTRIX_TEST(SimulatorPlatform, InputIsDeliveredInOrder) {
    SimulatorPlatform platform;
    platform.simulatedInput().pressAndRelease(RawInput::KeyLeft, 100, 50);

    InputEvent event;
    NOTRIX_CHECK(platform.input().poll(event));
    NOTRIX_CHECK(event.phase == ButtonPhase::Down);
    NOTRIX_CHECK_EQ(event.timestampMillis, std::uint64_t(100));

    NOTRIX_CHECK(platform.input().poll(event));
    NOTRIX_CHECK(event.phase == ButtonPhase::Up);
    NOTRIX_CHECK_EQ(event.timestampMillis, std::uint64_t(150));

    NOTRIX_CHECK_FALSE(platform.input().poll(event));
}

NOTRIX_TEST(SimulatorPlatform, EmptyQueuePollsFalse) {
    SimulatorPlatform platform;
    InputEvent event;
    NOTRIX_CHECK_FALSE(platform.input().poll(event));
}

NOTRIX_TEST(SimulatorPlatform, QueueOverflowDropsTheOldestEvent) {
    // Blueprint §38 forbids unbounded queues. Dropping the newest instead would
    // let one stuck control mask every press that follows it.
    SimulatorPlatform platform;
    auto& input = platform.simulatedInput();

    const std::size_t capacity = notrix::platform::simulator::SimulatorInput::kCapacity;
    for (std::size_t i = 0; i < capacity + 3; ++i) {
        input.push(InputEvent({RawInput::KeyMiddle, ButtonPhase::Tick, static_cast<std::uint64_t>(i)}));
    }

    NOTRIX_CHECK_EQ(input.pending(), capacity);
    NOTRIX_CHECK_EQ(input.droppedEventCount(), 3u);

    // The three oldest are gone, so the queue now starts at timestamp 3.
    InputEvent event;
    NOTRIX_CHECK(input.poll(event));
    NOTRIX_CHECK_EQ(event.timestampMillis, std::uint64_t(3));
}

NOTRIX_TEST(SimulatorPlatform, RotateProducesTickEvents) {
    SimulatorPlatform platform;
    platform.simulatedInput().rotate(true, 500);

    InputEvent event;
    NOTRIX_CHECK(platform.input().poll(event));
    NOTRIX_CHECK(event.source == RawInput::RotaryRight);
    NOTRIX_CHECK(event.phase == ButtonPhase::Tick);
}

// --- clock ------------------------------------------------------------------

NOTRIX_TEST(SimulatorPlatform, MonotonicTimeOnlyMovesWhenAdvanced) {
    SimulatorPlatform platform;
    NOTRIX_CHECK_EQ(platform.clock().monotonicMillis(), std::uint64_t(0));

    platform.simulatedClock().advance(250);
    NOTRIX_CHECK_EQ(platform.clock().monotonicMillis(), std::uint64_t(250));

    platform.simulatedClock().advance(250);
    NOTRIX_CHECK_EQ(platform.clock().monotonicMillis(), std::uint64_t(500));
}

NOTRIX_TEST(SimulatorPlatform, WallClockStartsInvalid) {
    // A freshly booted device has not reached NTP yet. Rendering 01:00 because
    // the wall clock was trusted too early is exactly what this prevents.
    SimulatorPlatform platform;
    NOTRIX_CHECK_FALSE(platform.clock().wallClockValid());
}

NOTRIX_TEST(SimulatorPlatform, WallClockBecomesValidOnceSet) {
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000, 3600);

    NOTRIX_CHECK(platform.clock().wallClockValid());
    NOTRIX_CHECK_EQ(platform.clock().unixSeconds(), std::int64_t(1'700'000'000));
    NOTRIX_CHECK_EQ(platform.clock().utcOffsetSeconds(), 3600);
}

NOTRIX_TEST(SimulatorPlatform, WallClockAdvancesWithMonotonicTime) {
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1000);
    platform.simulatedClock().advance(2500);

    NOTRIX_CHECK_EQ(platform.clock().unixSeconds(), std::int64_t(1002));
}

NOTRIX_TEST(SimulatorPlatform, SubSecondAdvancesAccumulate) {
    // Ten 100 ms frames are one second, not zero. Rounding each advance down
    // would make the clock lose time under animation.
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1000);

    for (int i = 0; i < 10; ++i) {
        platform.simulatedClock().advance(100);
    }

    NOTRIX_CHECK_EQ(platform.clock().unixSeconds(), std::int64_t(1001));
}

NOTRIX_TEST(SimulatorPlatform, InvalidWallClockDoesNotAdvance) {
    SimulatorPlatform platform;
    platform.simulatedClock().advance(5000);
    NOTRIX_CHECK_FALSE(platform.clock().wallClockValid());
}

// --- storage ----------------------------------------------------------------

NOTRIX_TEST(SimulatorPlatform, StorageRoundTrips) {
    SimulatorPlatform platform;
    auto& storage = platform.storage();

    NOTRIX_CHECK(storage.write("config", "{\"schemaVersion\":1}"));
    NOTRIX_CHECK(storage.exists("config"));

    std::string value;
    NOTRIX_CHECK(storage.read("config", value));
    NOTRIX_CHECK_EQ(value, std::string("{\"schemaVersion\":1}"));
}

NOTRIX_TEST(SimulatorPlatform, ReadingAMissingKeyFailsAndClearsOutput) {
    SimulatorPlatform platform;

    std::string value = "stale";
    NOTRIX_CHECK_FALSE(platform.storage().read("absent", value));
    NOTRIX_CHECK(value.empty());
}

NOTRIX_TEST(SimulatorPlatform, WriteReplacesPreviousValue) {
    SimulatorPlatform platform;
    auto& storage = platform.storage();

    storage.write("key", "first");
    storage.write("key", "second");

    std::string value;
    storage.read("key", value);
    NOTRIX_CHECK_EQ(value, std::string("second"));
    NOTRIX_CHECK_EQ(platform.simulatedStorage().keyCount(), std::size_t(1));
}

NOTRIX_TEST(SimulatorPlatform, RemoveDeletesOnlyTheNamedKey) {
    SimulatorPlatform platform;
    auto& storage = platform.storage();

    storage.write("a", "1");
    storage.write("b", "2");

    NOTRIX_CHECK(storage.remove("a"));
    NOTRIX_CHECK_FALSE(storage.exists("a"));
    NOTRIX_CHECK(storage.exists("b"));
    NOTRIX_CHECK_FALSE(storage.remove("a"));  // already gone
}

NOTRIX_TEST(SimulatorPlatform, OversizedValueIsRejected) {
    // Bounded by design: callers check the limit rather than discovering it.
    SimulatorPlatform platform;
    auto& storage = platform.storage();

    const std::string tooBig(storage.maxValueBytes() + 1, 'x');
    NOTRIX_CHECK_FALSE(storage.write("big", tooBig));
    NOTRIX_CHECK_FALSE(storage.exists("big"));
}

NOTRIX_TEST(SimulatorPlatform, EmptyKeyIsRejected) {
    SimulatorPlatform platform;
    NOTRIX_CHECK_FALSE(platform.storage().write("", "value"));
}

// --- optional capabilities ---------------------------------------------------

NOTRIX_TEST(SimulatorPlatform, OptionalCapabilitiesArePresentByDefault) {
    SimulatorPlatform platform;
    NOTRIX_CHECK(platform.audio() != nullptr);
    NOTRIX_CHECK(platform.network() != nullptr);
    NOTRIX_CHECK(platform.rebooter() != nullptr);
}

NOTRIX_TEST(SimulatorPlatform, DisabledCapabilitiesReturnNullptr) {
    // The nullptr branch must be reachable off-device. If it were not, code
    // that assumes a capability exists would first fail on real hardware.
    SimulatorCapabilities none;
    none.audio = false;
    none.network = false;
    none.rebooter = false;

    SimulatorPlatform platform(none);
    NOTRIX_CHECK(platform.audio() == nullptr);
    NOTRIX_CHECK(platform.network() == nullptr);
    NOTRIX_CHECK(platform.rebooter() == nullptr);

    // Required services are still there — those are never optional.
    IPlatformServices& services = platform;
    NOTRIX_CHECK_EQ(services.display().minimumFrameIntervalMillis(), 15);
}

NOTRIX_TEST(SimulatorPlatform, AudioRecordsRequestsWithoutMakingSound) {
    SimulatorPlatform platform;
    auto* audio = platform.audio();

    NOTRIX_CHECK(audio->playTone(440, 100));
    NOTRIX_CHECK(audio->playSound("notify"));
    NOTRIX_CHECK_FALSE(audio->playTone(0, 100));    // invalid frequency
    NOTRIX_CHECK_FALSE(audio->playSound(""));       // unnamed

    const auto& requests = platform.simulatedAudio().requests();
    NOTRIX_CHECK_EQ(requests.size(), std::size_t(2));
    NOTRIX_CHECK(requests[0].isTone);
    NOTRIX_CHECK_EQ(requests[0].frequencyHz, 440);
    NOTRIX_CHECK_EQ(requests[1].sound, std::string("notify"));
}

NOTRIX_TEST(SimulatorPlatform, NetworkStatusIsWhateverWasSet) {
    SimulatorPlatform platform;

    NOTRIX_CHECK_FALSE(platform.network()->status().connected);

    NetworkStatus online;
    online.connected = true;
    online.rssiDbm = -51;
    online.ipv4 = "192.168.1.42";
    platform.simulatedNetwork().setStatus(online);

    NOTRIX_CHECK(platform.network()->status().connected);
    NOTRIX_CHECK_EQ(platform.network()->status().ipv4, std::string("192.168.1.42"));
}

NOTRIX_TEST(SimulatorPlatform, RebooterRecordsRatherThanReboots) {
    SimulatorPlatform platform;
    NOTRIX_CHECK_EQ(platform.simulatedRebooter().rebootCount(), 0u);

    platform.rebooter()->reboot();
    NOTRIX_CHECK_EQ(platform.simulatedRebooter().rebootCount(), 1u);
}

NOTRIX_TEST(SimulatorPlatform, IdentifiesItselfForDiagnostics) {
    SimulatorPlatform platform;
    NOTRIX_CHECK_EQ(std::string(platform.name()), std::string("simulator"));
}
