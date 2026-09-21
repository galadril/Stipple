// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstring>

namespace notrix {
namespace platform {
namespace tc002 {

/// The TC002's MCU link, as pure decoding.
///
/// Separated from Tc002Mcu for the same reason RotaryDecoder was: the file that
/// owns the serial port cannot be compiled for the host, so anything left
/// inside it is untestable, and this is protocol work decoded from captures -
/// precisely the code most likely to be wrong and least likely to announce it.
/// A misparsed frame does not crash, it reports a wrong battery percentage.
///
/// Framing, from captures of the vendor application and of the MCU answering
/// us directly:
///
///     ff 55 <cmd> <len> <payload[len]> <checksum[2]>
///
///     -> ff 55 11 00 01 65                     ask the version
///     <- ff 55 11 07 56 31 2e 30 2e 31 37 ..   "V1.0.17" in ASCII
///     <- ff 55 03 03 5a 0c 4e 02 0e            battery, pushed unprompted
///     <- ff 55 02 01 01 01 58                  charging, pushed unprompted
///     <- ff 55 fe 00 02 52                     sent once, at startup
namespace mcu {

/// header(2) + cmd(1) + len(1) + payload(len) + checksum(2)
constexpr int kOverhead = 6;

/// Longest frame worth accepting. The length field is a single byte, so this
/// bounds the walker rather than trusting the wire (blueprint §38).
constexpr int kMaxFrame = kOverhead + 255;

/// Command byte carrying battery charge and cell voltage.
constexpr std::uint8_t kBattery = 0x03;

/// Command byte carrying the charge state: 1 on external power, 0 on battery.
constexpr std::uint8_t kCharging = 0x02;

/// Command byte believed to carry a microphone level. **Never observed** — see
/// Tc002Mcu.h for why this is still here and what would settle it.
constexpr std::uint8_t kMicLevel = 0x01;

/// Command byte for the version handshake.
constexpr std::uint8_t kVersion = 0x11;

/// The version query the vendor application sends, byte for byte. It is also
/// the only thing the vendor application ever writes to this link.
constexpr std::uint8_t kVersionQuery[] = {0xff, 0x55, 0x11, 0x00, 0x01, 0x65};

/// The trailing two bytes are a big-endian 16-bit sum of everything before
/// them, headers included.
///
/// Worked out from captures rather than assumed, and it holds on every frame
/// seen so far: ff+55+11+00 = 0x0165 and the version query ends 01 65;
/// ff+55+02+01+01 = 0x0158 and 01 58; ff+55+03+03+5a+0c+43 = 0x0203 and 02 03.
///
/// Worth checking rather than skipping. The link runs at 1.5 Mbaud with no flow
/// control, so a dropped byte is a real possibility, and the cost of not
/// checking is a charge flag or a battery percentage assembled from whatever
/// happened to follow a corrupted header.
inline bool checksumValid(const std::uint8_t* frame, int length) noexcept {
    if (length < kOverhead) {
        return false;
    }
    unsigned sum = 0;
    for (int i = 0; i < length - 2; ++i) {
        sum += frame[i];
    }
    const unsigned carried = (static_cast<unsigned>(frame[length - 2]) << 8) |
                             static_cast<unsigned>(frame[length - 1]);
    return (sum & 0xffffu) == carried;
}

/// Everything the MCU has told us, and how confident we are about each part.
///
/// Every value is paired with whether it has ever arrived, because on this
/// device the difference matters: the charge flag being 0 and the charge flag
/// never having been sent look identical in an int, and one of them means "on
/// battery" while the other means "we have no idea".
struct State {
    bool batteryKnown = false;
    int percent = 0;
    int millivolts = 0;

    bool chargingKnown = false;
    bool charging = false;

    bool micKnown = false;
    int micAmplitude = 0;

    char version[16] = {};

    /// Apply one whole frame. Returns false if the checksum did not hold, which
    /// tells a frame walker to resync by a byte rather than trust the length.
    bool consume(const std::uint8_t* frame, int length) noexcept {
        if (!checksumValid(frame, length)) {
            return false;
        }

        const std::uint8_t command = frame[2];
        const int payloadLength = frame[3];
        const std::uint8_t* payload = frame + 4;

        if (command == kBattery && payloadLength >= 3) {
            // Anything outside 0-100 is not a percentage, so the whole frame is
            // discarded rather than clamped: clamping 200 to 100 would invent a
            // full battery, and a frame with a nonsense percentage is not one
            // whose voltage should be trusted either.
            if (payload[0] <= 100) {
                percent = static_cast<int>(payload[0]);
                millivolts = (static_cast<int>(payload[1]) << 8) |
                             static_cast<int>(payload[2]);
                batteryKnown = true;
            }
            return true;
        }

        if (command == kCharging && payloadLength >= 1) {
            charging = payload[0] != 0;
            chargingKnown = true;
            return true;
        }

        if (command == kMicLevel && payloadLength >= 2) {
            micAmplitude = (static_cast<int>(payload[0]) << 8) |
                           static_cast<int>(payload[1]);
            micKnown = true;
            return true;
        }

        if (command == kVersion && payloadLength > 0) {
            const int room = static_cast<int>(sizeof(version)) - 1;
            const int copy = payloadLength < room ? payloadLength : room;
            std::memcpy(version, payload, static_cast<std::size_t>(copy));
            version[copy] = '\0';
        }

        // A frame whose checksum held but whose command we do not recognise is
        // still a frame: consuming it keeps the walker aligned on the next one.
        return true;
    }
};

/// Finds whole frames in `buffer` and applies them to `state`.
///
/// Returns how many bytes were consumed; the caller keeps the remainder, which
/// is a frame still arriving. Split out so the buffering policy and the
/// protocol can be tested apart from each other and from the serial port.
///
/// `capacity` is how much room the caller's buffer has. It matters because the
/// length byte is attacker-adjacent data from a noisy 1.5 Mbaud link with no
/// flow control: a false header claiming a length the buffer could never hold
/// would otherwise wedge the walker forever, waiting for bytes that cannot fit.
inline int walk(State& state, const std::uint8_t* buffer, int held,
                int capacity = kMaxFrame) noexcept {
    int at = 0;
    while (at + 4 <= held) {
        if (buffer[at] != 0xff || buffer[at + 1] != 0x55) {
            ++at;
            continue;
        }
        const int total = kOverhead + buffer[at + 3];
        if (total > capacity) {
            // This frame can never be assembled in the space available, so it
            // is not a frame. Resyncing costs one byte; waiting costs every
            // frame that arrives behind it until the buffer overflows and the
            // whole backlog is thrown away.
            ++at;
            continue;
        }
        if (at + total > held) {
            break;  // the rest is still on the wire
        }
        if (state.consume(buffer + at, total)) {
            at += total;
        } else {
            // The checksum disagreed, so `total` came from a length byte we
            // have no reason to trust. Stepping one byte re-searches for the
            // next header rather than skipping over it on the strength of a
            // number the frame itself just failed to vouch for — a payload is
            // allowed to contain ff 55.
            ++at;
        }
    }
    return at;
}

}  // namespace mcu
}  // namespace tc002
}  // namespace platform
}  // namespace notrix
