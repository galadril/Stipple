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
/// **There is no confirmed microphone command.** A constant here once claimed
/// 0x01 carried a level and had been seen while the stock visualiser ran. No
/// capture in this repository supports that and two contradict it: a trace of
/// the vendor application and a listen taken while someone made noise at the
/// device both contain 0x02 and 0x03 and nothing else. The vendor application
/// also writes nothing to this link but the version query, so if audio must be
/// asked for, we have not seen the asking. Until a capture taken with the
/// stock visualiser actually on screen shows otherwise, this device reports
/// that it cannot hear.
///
/// Read-only beyond the version handshake. The MCU also drives the panel's
/// power rails, and writing commands whose meaning is a guess is not worth a
/// clock.
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
    int fd_ = -1;

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
