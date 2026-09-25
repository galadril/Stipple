// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/McuProtocol.h"

#include <vector>

#include "support/TestFramework.h"

using stipple::platform::tc002::mcu::State;
using stipple::platform::tc002::mcu::checksumValid;
using stipple::platform::tc002::mcu::walk;

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

STIPPLE_TEST(McuProtocol, ChecksumIsTheSumOfEveryPrecedingByte) {
    // The rule was reverse-engineered, so it is asserted against captured
    // frames rather than against itself.
    STIPPLE_CHECK(checksumValid(kVersionReply.data(), static_cast<int>(kVersionReply.size())));
    STIPPLE_CHECK(checksumValid(kBattery90.data(), static_cast<int>(kBattery90.size())));
    STIPPLE_CHECK(checksumValid(kChargingOn.data(), static_cast<int>(kChargingOn.size())));
    STIPPLE_CHECK(checksumValid(kChargingOff.data(), static_cast<int>(kChargingOff.size())));
    STIPPLE_CHECK(checksumValid(kStartup.data(), static_cast<int>(kStartup.size())));
}

STIPPLE_TEST(McuProtocol, ACorruptedFrameIsRejectedNotDecoded) {
    // One flipped payload byte. Without the checksum this reads as a perfectly
    // ordinary 70% battery, which is the failure mode worth preventing: the
    // link has no flow control, so this is a thing that happens.
    std::vector<std::uint8_t> corrupt = kBattery90;
    corrupt[4] = 0x46;

    State state;
    STIPPLE_CHECK(!state.consume(corrupt.data(), static_cast<int>(corrupt.size())));
    STIPPLE_CHECK(!state.batteryKnown);
}

STIPPLE_TEST(McuProtocol, BatteryCarriesPercentAndMillivolts) {
    State state;
    feed(state, kBattery90);

    STIPPLE_CHECK(state.batteryKnown);
    STIPPLE_CHECK_EQ(state.percent, 90);
    STIPPLE_CHECK_EQ(state.millivolts, 3139);
}

STIPPLE_TEST(McuProtocol, APercentageOutsideRangeIsDiscardedNotClamped) {
    // Clamping 200 to 100 would invent a full battery out of a bad frame.
    std::vector<std::uint8_t> absurd = {0xff, 0x55, 0x03, 0x03, 0xc8, 0x0c, 0x43, 0x00, 0x00};
    unsigned sum = 0;
    for (std::size_t i = 0; i + 2 < absurd.size(); ++i) { sum += absurd[i]; }
    absurd[7] = static_cast<std::uint8_t>((sum >> 8) & 0xff);
    absurd[8] = static_cast<std::uint8_t>(sum & 0xff);

    State state;
    STIPPLE_CHECK(state.consume(absurd.data(), static_cast<int>(absurd.size())));
    STIPPLE_CHECK(!state.batteryKnown);
}

STIPPLE_TEST(McuProtocol, ChargingTracksTheCable) {
    State state;
    STIPPLE_CHECK(!state.chargingKnown);

    feed(state, kChargingOn);
    STIPPLE_CHECK(state.chargingKnown);
    STIPPLE_CHECK(state.charging);

    feed(state, kChargingOff);
    STIPPLE_CHECK(state.chargingKnown);
    STIPPLE_CHECK(!state.charging);
}

STIPPLE_TEST(McuProtocol, NotChargingIsNotTheSameAsNotKnowing) {
    // The distinction ADR 0013 exists for. A UI that cannot tell these apart
    // will render "on battery" for a device that has simply never said.
    State never;
    STIPPLE_CHECK(!never.chargingKnown);
    STIPPLE_CHECK(!never.charging);

    State told;
    feed(told, kChargingOff);
    STIPPLE_CHECK(told.chargingKnown);
    STIPPLE_CHECK(!told.charging);
}

STIPPLE_TEST(McuProtocol, TheVersionReplyIsReadAsAscii) {
    State state;
    feed(state, kVersionReply);
    STIPPLE_CHECK_EQ(std::string(state.version), std::string("V1.0.17"));
}

STIPPLE_TEST(McuProtocol, TheMicrophoneSaysNothingUntilItIsSwitchedOn) {
    // The reason three separate captures found no audio. The MCU streams levels
    // only after kMicSwitch, and none of those captures had anything on screen
    // that wanted sound - so "no microphone frame exists" looked like a fact
    // about the hardware when it was a fact about what we had been watching.
    State state;
    for (const auto* frame : {&kStartup, &kBattery90, &kChargingOn, &kVersionReply}) {
        feed(state, *frame);
    }
    STIPPLE_CHECK_FALSE(state.micKnown);
}

STIPPLE_TEST(McuProtocol, AMicrophoneLevelIsBigEndianAmplitude) {
    // Captured from the vendor application with its visualiser on screen.
    // 0x01a6 is 422 - a quiet room, which is what the room was.
    const std::vector<std::uint8_t> level = {0xff, 0x55, 0x01, 0x02, 0x01, 0xa6, 0x01, 0xfe};
    STIPPLE_CHECK(checksumValid(level.data(), static_cast<int>(level.size())));

    State state;
    feed(state, level);
    STIPPLE_CHECK(state.micKnown);
    STIPPLE_CHECK_EQ(state.micAmplitude, 422);
}

STIPPLE_TEST(McuProtocol, TheMicrophoneSwitchMatchesWhatWasCaptured) {
    // kMicOn and kMicOff are byte literals lifted from the wire, because a
    // captured byte is evidence and a generated one is a belief. This is what
    // stops the two drifting: if the checksum rule were wrong, the literals and
    // the encoder would disagree here.
    const std::uint8_t on = 0x01;
    const std::uint8_t off = 0x00;

    std::uint8_t built[8] = {};
    int length = stipple::platform::tc002::mcu::encode(
        built, sizeof(built), stipple::platform::tc002::mcu::kMicSwitch, &on, 1);
    STIPPLE_CHECK_EQ(length, static_cast<int>(sizeof(stipple::platform::tc002::mcu::kMicOn)));
    for (int i = 0; i < length; ++i) {
        STIPPLE_CHECK_EQ(static_cast<int>(built[i]),
                        static_cast<int>(stipple::platform::tc002::mcu::kMicOn[i]));
    }

    length = stipple::platform::tc002::mcu::encode(
        built, sizeof(built), stipple::platform::tc002::mcu::kMicSwitch, &off, 1);
    STIPPLE_CHECK_EQ(length, static_cast<int>(sizeof(stipple::platform::tc002::mcu::kMicOff)));
    for (int i = 0; i < length; ++i) {
        STIPPLE_CHECK_EQ(static_cast<int>(built[i]),
                        static_cast<int>(stipple::platform::tc002::mcu::kMicOff[i]));
    }

    // And both are frames the decoder would accept, which the device has to be
    // able to assume about anything we put on a link it also listens to.
    STIPPLE_CHECK(checksumValid(stipple::platform::tc002::mcu::kMicOn,
                               static_cast<int>(sizeof(stipple::platform::tc002::mcu::kMicOn))));
    STIPPLE_CHECK(checksumValid(stipple::platform::tc002::mcu::kMicOff,
                               static_cast<int>(sizeof(stipple::platform::tc002::mcu::kMicOff))));
}

STIPPLE_TEST(McuProtocol, TheVersionQueryLiteralAgreesWithTheEncoder) {
    std::uint8_t built[8] = {};
    const int length = stipple::platform::tc002::mcu::encode(
        built, sizeof(built), stipple::platform::tc002::mcu::kVersion, nullptr, 0);
    STIPPLE_CHECK_EQ(length, static_cast<int>(sizeof(stipple::platform::tc002::mcu::kVersionQuery)));
    for (int i = 0; i < length; ++i) {
        STIPPLE_CHECK_EQ(static_cast<int>(built[i]),
                        static_cast<int>(stipple::platform::tc002::mcu::kVersionQuery[i]));
    }
}

STIPPLE_TEST(McuProtocol, EncodingRefusesWhatWillNotFit) {
    std::uint8_t tiny[4] = {};
    const std::uint8_t payload = 1;
    STIPPLE_CHECK_EQ(stipple::platform::tc002::mcu::encode(tiny, sizeof(tiny), 0x04, &payload, 1), 0);
}

STIPPLE_TEST(McuProtocol, FramesSplitAcrossReadsAreReassembled) {
    // The port is non-blocking and the frames are small, so a frame arriving in
    // two pieces is routine rather than exotic.
    std::vector<std::uint8_t> stream;
    stream.insert(stream.end(), kBattery90.begin(), kBattery90.end());
    stream.insert(stream.end(), kChargingOn.begin(), kChargingOn.end());

    State state;
    const int split = 5;
    const int first = walk(state, stream.data(), split);
    STIPPLE_CHECK_EQ(first, 0);  // nothing whole yet
    STIPPLE_CHECK(!state.batteryKnown);

    const int all = walk(state, stream.data(), static_cast<int>(stream.size()));
    STIPPLE_CHECK_EQ(all, static_cast<int>(stream.size()));
    STIPPLE_CHECK(state.batteryKnown);
    STIPPLE_CHECK(state.charging);
}

STIPPLE_TEST(McuProtocol, GarbageBeforeAFrameIsSkippedToFindIt) {
    std::vector<std::uint8_t> stream = {0x00, 0xff, 0x12, 0x99};
    stream.insert(stream.end(), kBattery90.begin(), kBattery90.end());

    State state;
    const int consumed = walk(state, stream.data(), static_cast<int>(stream.size()));
    STIPPLE_CHECK_EQ(consumed, static_cast<int>(stream.size()));
    STIPPLE_CHECK_EQ(state.percent, 90);
}

STIPPLE_TEST(McuProtocol, AFalseHeaderIsResyncedPastRatherThanTrusted) {
    // A payload is allowed to contain ff 55. Trusting the length byte of a
    // frame that failed its own checksum would step straight over a real
    // frame, so a rejected frame resyncs by one byte instead.
    std::vector<std::uint8_t> stream = {0xff, 0x55, 0x03, 0x03, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    stream.insert(stream.end(), kBattery90.begin(), kBattery90.end());

    State state;
    walk(state, stream.data(), static_cast<int>(stream.size()));
    STIPPLE_CHECK(state.batteryKnown);
    STIPPLE_CHECK_EQ(state.percent, 90);
}

STIPPLE_TEST(McuProtocol, AnIncompleteFrameWaitsForTheRestOfItself) {
    // The counterpart to the test above, and the reason a bad length cannot
    // simply be skipped on sight: a short read mid-frame is routine, and the
    // only honest response is to wait.
    State state;
    const int consumed = walk(state, kBattery90.data(), 6);
    STIPPLE_CHECK_EQ(consumed, 0);
    STIPPLE_CHECK(!state.batteryKnown);
}

STIPPLE_TEST(McuProtocol, ALengthTooLargeToEverFitDoesNotWedgeTheWalker) {
    // Without the capacity guard this stalls forever: the walker waits for 261
    // bytes that will never arrive, every real frame queues up behind it, and
    // the link goes silent until the buffer overflows and throws the backlog
    // away. On a noisy 1.5 Mbaud line with no flow control, that is a hang
    // waiting to happen rather than a hypothetical.
    std::vector<std::uint8_t> stream = {0xff, 0x55, 0x03, 0xff};
    stream.insert(stream.end(), kBattery90.begin(), kBattery90.end());

    State state;
    const int consumed = walk(state, stream.data(), static_cast<int>(stream.size()), 64);
    STIPPLE_CHECK_EQ(consumed, static_cast<int>(stream.size()));
    STIPPLE_CHECK(state.batteryKnown);
    STIPPLE_CHECK_EQ(state.percent, 90);
}
