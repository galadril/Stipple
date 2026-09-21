// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/tc002/McuProtocol.h"

#include <vector>

#include "support/TestFramework.h"

using notrix::platform::tc002::mcu::State;
using notrix::platform::tc002::mcu::checksumValid;
using notrix::platform::tc002::mcu::walk;

namespace {

/// Frames lifted verbatim from build/device-probe captures, so these tests fail
/// if the decoding stops agreeing with the wire rather than with an idea of it.
const std::vector<std::uint8_t> kVersionReply =
    {0xff, 0x55, 0x11, 0x07, 0x56, 0x31, 0x2e, 0x30, 0x2e, 0x31, 0x37, 0x02, 0xe7};
const std::vector<std::uint8_t> kBattery90 =
    {0xff, 0x55, 0x03, 0x03, 0x5a, 0x0c, 0x43, 0x02, 0x03};
const std::vector<std::uint8_t> kChargingOn = {0xff, 0x55, 0x02, 0x01, 0x01, 0x01, 0x58};
const std::vector<std::uint8_t> kChargingOff = {0xff, 0x55, 0x02, 0x01, 0x00, 0x01, 0x57};
const std::vector<std::uint8_t> kStartup = {0xff, 0x55, 0xfe, 0x00, 0x02, 0x52};

int feed(State& state, const std::vector<std::uint8_t>& bytes) {
    return walk(state, bytes.data(), static_cast<int>(bytes.size()));
}

}  // namespace

NOTRIX_TEST(McuProtocol, ChecksumIsTheSumOfEveryPrecedingByte) {
    // The rule was reverse-engineered, so it is asserted against captured
    // frames rather than against itself.
    NOTRIX_CHECK(checksumValid(kVersionReply.data(), static_cast<int>(kVersionReply.size())));
    NOTRIX_CHECK(checksumValid(kBattery90.data(), static_cast<int>(kBattery90.size())));
    NOTRIX_CHECK(checksumValid(kChargingOn.data(), static_cast<int>(kChargingOn.size())));
    NOTRIX_CHECK(checksumValid(kChargingOff.data(), static_cast<int>(kChargingOff.size())));
    NOTRIX_CHECK(checksumValid(kStartup.data(), static_cast<int>(kStartup.size())));
}

NOTRIX_TEST(McuProtocol, ACorruptedFrameIsRejectedNotDecoded) {
    // One flipped payload byte. Without the checksum this reads as a perfectly
    // ordinary 70% battery, which is the failure mode worth preventing: the
    // link has no flow control, so this is a thing that happens.
    std::vector<std::uint8_t> corrupt = kBattery90;
    corrupt[4] = 0x46;

    State state;
    NOTRIX_CHECK(!state.consume(corrupt.data(), static_cast<int>(corrupt.size())));
    NOTRIX_CHECK(!state.batteryKnown);
}

NOTRIX_TEST(McuProtocol, BatteryCarriesPercentAndMillivolts) {
    State state;
    feed(state, kBattery90);

    NOTRIX_CHECK(state.batteryKnown);
    NOTRIX_CHECK_EQ(state.percent, 90);
    NOTRIX_CHECK_EQ(state.millivolts, 3139);
}

NOTRIX_TEST(McuProtocol, APercentageOutsideRangeIsDiscardedNotClamped) {
    // Clamping 200 to 100 would invent a full battery out of a bad frame.
    std::vector<std::uint8_t> absurd = {0xff, 0x55, 0x03, 0x03, 0xc8, 0x0c, 0x43, 0x00, 0x00};
    unsigned sum = 0;
    for (std::size_t i = 0; i + 2 < absurd.size(); ++i) { sum += absurd[i]; }
    absurd[7] = static_cast<std::uint8_t>((sum >> 8) & 0xff);
    absurd[8] = static_cast<std::uint8_t>(sum & 0xff);

    State state;
    NOTRIX_CHECK(state.consume(absurd.data(), static_cast<int>(absurd.size())));
    NOTRIX_CHECK(!state.batteryKnown);
}

NOTRIX_TEST(McuProtocol, ChargingTracksTheCable) {
    State state;
    NOTRIX_CHECK(!state.chargingKnown);

    feed(state, kChargingOn);
    NOTRIX_CHECK(state.chargingKnown);
    NOTRIX_CHECK(state.charging);

    feed(state, kChargingOff);
    NOTRIX_CHECK(state.chargingKnown);
    NOTRIX_CHECK(!state.charging);
}

NOTRIX_TEST(McuProtocol, NotChargingIsNotTheSameAsNotKnowing) {
    // The distinction ADR 0013 exists for. A UI that cannot tell these apart
    // will render "on battery" for a device that has simply never said.
    State never;
    NOTRIX_CHECK(!never.chargingKnown);
    NOTRIX_CHECK(!never.charging);

    State told;
    feed(told, kChargingOff);
    NOTRIX_CHECK(told.chargingKnown);
    NOTRIX_CHECK(!told.charging);
}

NOTRIX_TEST(McuProtocol, TheVersionReplyIsReadAsAscii) {
    State state;
    feed(state, kVersionReply);
    NOTRIX_CHECK_EQ(std::string(state.version), std::string("V1.0.17"));
}

NOTRIX_TEST(McuProtocol, NoCaptureEverDeliveredAMicrophoneLevel) {
    // The whole reason the visualiser draws NO MIC on this hardware. Two
    // captures — the vendor application, and 75 seconds with someone making
    // noise at the device — contain 0x02 and 0x03 and nothing else. If a mic
    // frame is ever found, this test is the one that should be deleted, and
    // deleting it should require having seen the frame.
    State state;
    for (const auto* frame : {&kStartup, &kBattery90, &kChargingOn, &kVersionReply}) {
        feed(state, *frame);
    }
    NOTRIX_CHECK(!state.micKnown);
}

NOTRIX_TEST(McuProtocol, AMicrophoneFrameWouldBeDecodedIfItArrived) {
    // Decoding is kept even though nothing sends it, so that the day a capture
    // shows one, the only change needed is in the adapter's advertisement.
    std::vector<std::uint8_t> mic = {0xff, 0x55, 0x01, 0x02, 0x7d, 0x00, 0x00, 0x00};
    unsigned sum = 0;
    for (std::size_t i = 0; i + 2 < mic.size(); ++i) { sum += mic[i]; }
    mic[6] = static_cast<std::uint8_t>((sum >> 8) & 0xff);
    mic[7] = static_cast<std::uint8_t>(sum & 0xff);

    State state;
    feed(state, mic);
    NOTRIX_CHECK(state.micKnown);
    NOTRIX_CHECK_EQ(state.micAmplitude, 32000);
}

NOTRIX_TEST(McuProtocol, FramesSplitAcrossReadsAreReassembled) {
    // The port is non-blocking and the frames are small, so a frame arriving in
    // two pieces is routine rather than exotic.
    std::vector<std::uint8_t> stream;
    stream.insert(stream.end(), kBattery90.begin(), kBattery90.end());
    stream.insert(stream.end(), kChargingOn.begin(), kChargingOn.end());

    State state;
    const int split = 5;
    const int first = walk(state, stream.data(), split);
    NOTRIX_CHECK_EQ(first, 0);  // nothing whole yet
    NOTRIX_CHECK(!state.batteryKnown);

    const int all = walk(state, stream.data(), static_cast<int>(stream.size()));
    NOTRIX_CHECK_EQ(all, static_cast<int>(stream.size()));
    NOTRIX_CHECK(state.batteryKnown);
    NOTRIX_CHECK(state.charging);
}

NOTRIX_TEST(McuProtocol, GarbageBeforeAFrameIsSkippedToFindIt) {
    std::vector<std::uint8_t> stream = {0x00, 0xff, 0x12, 0x99};
    stream.insert(stream.end(), kBattery90.begin(), kBattery90.end());

    State state;
    const int consumed = walk(state, stream.data(), static_cast<int>(stream.size()));
    NOTRIX_CHECK_EQ(consumed, static_cast<int>(stream.size()));
    NOTRIX_CHECK_EQ(state.percent, 90);
}

NOTRIX_TEST(McuProtocol, AFalseHeaderIsResyncedPastRatherThanTrusted) {
    // A payload is allowed to contain ff 55. Trusting the length byte of a
    // frame that failed its own checksum would step straight over a real
    // frame, so a rejected frame resyncs by one byte instead.
    std::vector<std::uint8_t> stream = {0xff, 0x55, 0x03, 0x03, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    stream.insert(stream.end(), kBattery90.begin(), kBattery90.end());

    State state;
    walk(state, stream.data(), static_cast<int>(stream.size()));
    NOTRIX_CHECK(state.batteryKnown);
    NOTRIX_CHECK_EQ(state.percent, 90);
}

NOTRIX_TEST(McuProtocol, AnIncompleteFrameWaitsForTheRestOfItself) {
    // The counterpart to the test above, and the reason a bad length cannot
    // simply be skipped on sight: a short read mid-frame is routine, and the
    // only honest response is to wait.
    State state;
    const int consumed = walk(state, kBattery90.data(), 6);
    NOTRIX_CHECK_EQ(consumed, 0);
    NOTRIX_CHECK(!state.batteryKnown);
}

NOTRIX_TEST(McuProtocol, ALengthTooLargeToEverFitDoesNotWedgeTheWalker) {
    // Without the capacity guard this stalls forever: the walker waits for 261
    // bytes that will never arrive, every real frame queues up behind it, and
    // the link goes silent until the buffer overflows and throws the backlog
    // away. On a noisy 1.5 Mbaud line with no flow control, that is a hang
    // waiting to happen rather than a hypothetical.
    std::vector<std::uint8_t> stream = {0xff, 0x55, 0x03, 0xff};
    stream.insert(stream.end(), kBattery90.begin(), kBattery90.end());

    State state;
    const int consumed = walk(state, stream.data(), static_cast<int>(stream.size()), 64);
    NOTRIX_CHECK_EQ(consumed, static_cast<int>(stream.size()));
    NOTRIX_CHECK(state.batteryKnown);
    NOTRIX_CHECK_EQ(state.percent, 90);
}
