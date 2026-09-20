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
/// **Battery is payload byte 0 of command 0x03.** It sat at 91 during one
/// session and 90 hours later, stays inside 0-100, and moves slowly in one
/// direction — which is what a discharging battery does and what nothing else
/// on this link does. Byte 1 has been constant at 12. Byte 2 drifts between
/// roughly 70 and 78 within seconds, far too quickly to be temperature, so it
/// is deliberately left undecoded rather than given an invented meaning.
///
/// Read-only beyond the version handshake. The MCU also drives the panel's
/// power rails, and writing commands whose meaning is a guess is not worth a
/// clock.
class Tc002Mcu final : public IPowerSource {
public:
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
    bool batteryKnown_ = false;
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
