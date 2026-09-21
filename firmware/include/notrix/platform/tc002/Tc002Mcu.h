// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/platform/PlatformServices.h"
#include "notrix/platform/tc002/McuProtocol.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// The MCU on /dev/ttyS1, and the only source of battery state on this device.
///
/// There is no /sys/class/power_supply, no hwmon and no IIO on the TC002 — all
/// three were checked. The MCU is it.
///
/// This class owns the serial port and nothing else. What the bytes mean lives
/// in McuProtocol.h, where it can be tested on the host; a wire format decoded
/// from captures is exactly the code that should not be trapped inside a file
/// only the device can compile.
///
/// **Command 0x03 is battery**, and its three payload bytes are:
///
///     [0]      percentage, 0-100
///     [1..2]   cell voltage in millivolts, big-endian
///
/// Byte 0 was inferred here first, from a 91-then-90 reading hours apart, and
/// that inference was shaky: a device left on its dock reported a steady 90
/// rather than climbing to 100, which is not what a charging battery does.
/// The layout above is the third-party port's, read for the field mapping
/// only - a fact about Ulanzi's MCU, not their expression of it.
///
/// **Command 0x02 is the charge state**, which had been dismissed here as "a
/// flag, always 1". It is always 1 when every capture is taken over USB. A
/// 75-second capture with the cable deliberately pulled settles it: 01, then
/// 00 the moment it came out, then 01 again on reconnect — and the reported
/// voltage sagged from ~3160 mV to ~3115 mV across exactly that window, which
/// also explains a percentage that appears to jump by several points when
/// nothing about the battery has changed.
///
/// **The microphone has to be switched on**, with command 0x04 and a payload of
/// 1. Until then the MCU sends no audio at all, and command 0x01 - the level,
/// a big-endian 16-bit amplitude at roughly 22 Hz - never appears.
///
/// That conditional cost a day and an entirely wrong conclusion. Three captures
/// were taken looking for audio: a trace of the vendor application, a 75-second
/// listen with someone deliberately making noise, and the vendor application
/// left on its clock face. All three contained 0x02 and 0x03 and nothing else,
/// and the honest-looking reading was that the hardware could not hear. It was
/// actually a fact about what had been on screen: none of the three had
/// anything showing that wanted sound. A fourth capture, taken with the vendor
/// visualiser actually displayed, had 451 audio frames in twenty seconds and
/// the enable sitting in plain sight.
///
/// The switch is also sticky - the MCU keeps streaming until told to stop or
/// until power is lost - which is the rest of the story. The vendor application
/// had enabled it, NOTRIX inherited a microphone it had never asked for, the
/// visualiser worked, and a reboot took it away with nothing in the code having
/// changed.
///
/// Read-only beyond the version handshake and the microphone switch. The MCU
/// also drives the panel's power rails, and writing commands whose meaning is a
/// guess is not worth a clock - but 0x04 is not a guess. Both of its values were
/// watched going out of the vendor application as its visualiser came and went,
/// and the bytes NOTRIX sends are that capture verbatim.
class Tc002Mcu final : public IPowerSource, public IMicrophone {
public:
    /// The protocol constants live in McuProtocol.h, which is where the
    /// decoding they belong to is. Re-exported here because callers and tests
    /// reach for them on this class.
    static constexpr std::uint8_t kMicLevel = mcu::kMicLevel;
    static constexpr std::uint8_t kCharging = mcu::kCharging;
    static constexpr std::uint8_t kTelemetry = mcu::kBattery;
    static constexpr std::uint8_t kVersion = mcu::kVersion;

    ~Tc002Mcu() override;

    /// Opens the port at 1.5 Mbaud and sends the version query. Returns false
    /// if the port cannot be configured; the caller then reports no battery
    /// rather than a fabricated one.
    bool open(const char* devicePath = "/dev/ttyS1");
    bool isOpen() const noexcept { return fd_ >= 0; }
    void close() noexcept;

    /// Drains whatever has arrived. Non-blocking; call once per frame.
    void poll();

    BatteryStatus battery() const override;
    SoundLevel level() const override;

    /// MCU firmware string, once it has answered. Empty until then.
    const char* version() const noexcept { return state_.version; }

private:
    /// Ask the MCU to start streaming audio, once it has proven it is listening.
    /// Called from poll(); safe to call repeatedly.
    void requestMicrophone();

    int fd_ = -1;

    /// Whether the microphone switch has been sent on this connection.
    bool micRequested_ = false;

    /// Partial frames live here between polls. Bounded, per §38: a link that
    /// never produces a valid header must not be able to grow a buffer.
    ///
    /// Sized past mcu::kMaxFrame on purpose. The length field is one byte, so
    /// the longest legal frame is 261 — a 256-byte buffer could not hold one,
    /// and the walker would have had to treat a perfectly valid long frame as
    /// undecodable. Nothing observed comes close to this, which is exactly why
    /// the ceiling should come from the protocol rather than from a round
    /// number.
    std::uint8_t buffer_[mcu::kMaxFrame + 64] = {};
    int held_ = 0;

    /// Everything decoded so far. This class owns the port; McuProtocol owns
    /// what the bytes mean.
    mcu::State state_;
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
