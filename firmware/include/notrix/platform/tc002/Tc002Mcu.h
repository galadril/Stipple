// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/platform/PlatformServices.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// The MCU on /dev/ttyS1, and the only source of battery state on this device.
///
/// There is no /sys/class/power_supply, no hwmon and no IIO on the TC002 — all
/// three were checked. The MCU is it.
///
/// Protocol, decoded from a capture of the vendor application and confirmed by
/// asking the MCU ourselves:
///
///     ff 55 <cmd> <len> <payload[len]> <trailer[2]>
///
///     -> ff 55 11 00 01 65                     ask the version
///     <- ff 55 11 07 56 31 2e 30 2e 31 37 ..   "V1.0.17" in ASCII
///     <- ff 55 03 03 5a 0c 4e 02 0e            telemetry, pushed unprompted
///     <- ff 55 02 01 01 01 58                  a flag, always 1 so far
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
/// It also settles bytes 1 and 2, which had been written off as an undecoded
/// fast-moving value. 0c 54 is 3156 mV, 0c 4e is 3150 mV, 0c 49 is 3145 mV:
/// one cell wobbling by a few millivolts, not a second sensor.
///
/// Read-only beyond the version handshake. The MCU also drives the panel's
/// power rails, and writing commands whose meaning is a guess is not worth a
/// clock.
class Tc002Mcu final : public IPowerSource, public IMicrophone {
public:
    /// Command byte carrying the microphone level.
    ///
    /// Found by tracing the stock app while its audio visualiser ran: this
    /// frame arrives at roughly 22 Hz and appears nowhere else, which is
    /// why it had never been seen. The payload is a big-endian 16-bit
    /// amplitude - a quiet room reads a few hundred, a clap reaches 32000.
    ///
    /// This is the whole microphone. There is no capture device, no
    /// libmi_ai.so, and nothing new opens when the visualiser starts: the
    /// level comes over the MCU link or not at all.
    static constexpr std::uint8_t kMicLevel = 0x01;

    /// Command byte carrying periodic telemetry.
    static constexpr std::uint8_t kTelemetry = 0x03;
    /// Command byte for the version handshake.
    static constexpr std::uint8_t kVersion = 0x11;

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
    const char* version() const noexcept { return version_; }

private:
    void consume(const std::uint8_t* frame, int length) noexcept;

    int fd_ = -1;

    /// Partial frames live here between polls. Bounded, per §38: a link that
    /// never produces a valid header must not be able to grow a buffer.
    std::uint8_t buffer_[256] = {};
    int held_ = 0;

    char version_[16] = {};

    int batteryPercent_ = 0;
    int batteryMillivolts_ = 0;

    int micAmplitude_ = 0;
    bool micKnown_ = false;
    bool batteryKnown_ = false;
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
