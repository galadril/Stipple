// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "notrix/platform/PlatformServices.h"
#include "notrix/platform/tc002/Tc002Dhcp.h"
#include "notrix/platform/tc002/Tc002Display.h"
#include "notrix/platform/tc002/Tc002HttpServer.h"
#include "notrix/platform/tc002/Tc002Input.h"
#include "notrix/platform/tc002/Tc002Audio.h"
#include "notrix/platform/tc002/Tc002Mcu.h"
#include "notrix/platform/tc002/WpaControl.h"
#include "notrix/platform/tc002/Tc002MqttClient.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// Time on a device with no RTC.
///
/// `zkgui` logs "open /dev/rtc0 fail" on every boot, and there is no battery to
/// keep one running anyway, so the wall clock is meaningless until something
/// sets it. That is exactly the case ISystemClock::wallClockValid() exists for:
/// a clock face that trusted CLOCK_REALTIME on a cold boot would confidently
/// render 1970.
class Tc002Clock final : public ISystemClock {
public:
    /// Anything earlier than this is a kernel default rather than a real time.
    /// 2020-01-01T00:00:00Z — comfortably after any plausible build date and
    /// far enough from the epoch that an unset clock cannot be mistaken for a
    /// set one.
    static constexpr std::int64_t kPlausibleEpoch = 1577836800;

    std::uint64_t monotonicMillis() const override;
    bool wallClockValid() const override;
    std::int64_t unixSeconds() const override;

    /// Zero. The device carries no tzdata, so there is nothing to read an
    /// offset from — it belongs in configuration, where the user sets it, not
    /// in a platform guess.
    int utcOffsetSeconds() const override { return 0; }
};

/// Key/value storage on /data.
///
/// /res is a read-only squashfs, so this is the only writable place on the
/// device — 7.6 MB free, which is ample for configuration and an icon set.
///
/// Atomicity is earned rather than assumed, per IStorage: each write goes to a
/// temporary file, is fsynced, then renamed over the target, and the directory
/// is fsynced after. rename(2) within a filesystem is atomic, so a power cut
/// leaves either the old value or the new one and never a torn mixture. The
/// simulator gets this for free; here it is the whole implementation.
class Tc002Storage final : public IStorage {
public:
    static constexpr std::size_t kMaxValueBytes = 64 * 1024;

    explicit Tc002Storage(std::string directory = "/data/notrix");

    /// Creates the directory if absent. Returns false if it cannot be used,
    /// which the host treats as an unusable platform rather than limping on
    /// with settings that will not persist.
    bool open();

    bool exists(std::string_view key) const override;
    bool read(std::string_view key, std::string& out) const override;
    bool write(std::string_view key, std::string_view value) override;
    bool remove(std::string_view key) override;
    std::size_t maxValueBytes() const override { return kMaxValueBytes; }

    const std::string& directory() const noexcept { return directory_; }

private:
    /// Builds a path, or returns empty if the key is not a safe filename.
    ///
    /// Keys are internal today ("boot", "icons", config), but §23 says API
    /// input must never name a path, and the cheapest way to keep that true is
    /// for this layer to refuse anything that could become one.
    std::string pathFor(std::string_view key) const;

    std::string directory_;
};

/// Wi-Fi state, read-only.
///
/// getifaddrs(3) rather than a shell: the device busybox is missing enough
/// commands — no grep, no sleep, no readlink — that shelling out is a liability,
/// and this is netlink-based so it works in a static binary where NSS does not.
class Tc002Network final : public INetworkManager {
public:
    NetworkStatus status() const override;

    /// True once the supplicant's control socket answers. False means it is
    /// not running, which is a real state on a device that has been put into
    /// hotspot mode - not an error, and not "no networks in range".
    bool canScan() const override;

    bool beginScan() override;
    std::vector<WirelessNetwork> networks() const override;

private:
    /// Opened on first use and kept.
    ///
    /// Mutable because status() and networks() are const - they observe the
    /// device rather than change it - while the socket underneath is not. The
    /// alternative is a non-const interface for reading, which would be worse
    /// documentation of what these calls actually do.
    mutable WpaControl control_;

    /// Ensure the socket is connected, or say it cannot be.
    bool connected() const;
};

/// The TC002 half of the §53 boundary.
///
/// Optional capabilities report absence honestly, per ADR 0013, rather than
/// accepting calls and doing nothing:
///
///   audio     — /dev/mi_ao exists and the SigmaStar MI layer drives it, but
///               nothing here speaks that protocol yet.
///   rebooter  — deliberately absent until there is a reason to expose the most
///               destructive thing NOTRIX can do to a clock.
/// MQTT is always present: unlike audio, "no broker configured" is a state the
/// service already models, so reporting the capability as absent would be the
/// wrong answer.
class Tc002Platform final : public IPlatformServices {
public:
    /// Brings up display, input, storage and clock. Returns false if the
    /// display or input cannot be opened; those are required services and a
    /// clock without them is not worth starting.
    bool open();
    void close() noexcept;

    const char* name() const override { return "tc002"; }

    IFrameBufferDisplay& display() override { return display_; }
    IInputDevice& input() override { return input_; }
    ISystemClock& clock() override { return clock_; }
    IStorage& storage() override { return storage_; }

    INetworkManager* network() override { return &network_; }

    /// Non-null only once the MCU link is open. A device whose serial port
    /// could not be configured reports no battery rather than zero percent.
    IPowerSource* power() override { return mcu_.isOpen() ? &mcu_ : nullptr; }

    /// Same link, same poll: the MCU carries both battery and microphone.
    IMicrophone* microphone() override { return mcu_.isOpen() ? &mcu_ : nullptr; }

    /// Non-null only once the vendor audio library has loaded and accepted a
    /// configuration. A build that cannot dlopen - a static one - reports no
    /// speaker rather than accepting sounds it will never make (ADR 0013).
    IAudioOutput* audio() override { return audio_.isOpen() ? &audio_ : nullptr; }

    /// Non-null once start() has been called on it. Reported through the
    /// interface so core sees a transport appear exactly when one exists.
    IHttpServer* httpServer() override {
        return http_.running() ? &http_ : nullptr;
    }

    /// Always offered: a configured-but-disconnected broker is a state the
    /// service reports, not an absent capability.
    IMqttClient* mqtt() override { return &mqtt_; }

    Tc002Display& panel() noexcept { return display_; }

    /// Concrete, because the MCU is polled from the loop like the transport.
    Tc002Mcu& mcu() noexcept { return mcu_; }

    /// Concrete, because audio is fed from the loop a frame at a time rather
    /// than queued: §16 says it must never block rendering, and a second of
    /// sound is a hundred and twenty frames.
    Tc002Audio& audio_out() noexcept { return audio_; }

    /// Concrete, because the transport is polled rather than threaded and
    /// IHttpServer has no poll() — see Tc002HttpServer for why.
    Tc002HttpServer& http() noexcept { return http_; }

    /// Concrete, and not behind INetworkManager, because holding a lease is
    /// not something core should be able to ask for or turn off. It is what
    /// makes the device reachable at all, on a platform that has nothing else
    /// able to do it.
    Tc002Dhcp& dhcp() noexcept { return dhcp_; }

private:
    Tc002Display display_;
    Tc002Input input_;
    Tc002Clock clock_;
    Tc002Storage storage_;
    Tc002Network network_;
    Tc002Mcu mcu_;
    Tc002Audio audio_;
    Tc002MqttClient mqtt_;
    Tc002HttpServer http_;
    Tc002Dhcp dhcp_;
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
