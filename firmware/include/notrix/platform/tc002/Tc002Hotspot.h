// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "notrix/platform/tc002/Tc002Dhcp.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// The device's own access point, for when it has nowhere else to go
/// (ADR 0018).
///
/// One radio cannot be an access point and a station at the same time, so
/// starting this **stops wpa_supplicant and drops the network**. That is not a
/// risk to be managed, it is the mechanism: a device runs a hotspot precisely
/// when it has no network worth keeping.
///
/// Everything it writes goes in /tmp. hostapd and dnsmasq both want config
/// files, and /tmp is tmpfs - a botched configuration is gone at the next
/// power cycle rather than living in /data waiting to strand somebody. The
/// vendor's own /data/misc/wifi/hostapd.conf is deliberately left alone.
///
/// **It reverts on its own.** A device that has been sitting on an open access
/// point for an hour with nobody connected has not been provisioned, it has
/// been forgotten - and the network it could not join half an hour ago may be
/// back. Reverting costs nothing and stops the one failure that cannot be
/// recovered over the network.
class Tc002Hotspot {
public:
    /// How long to stay up with nobody connected before going back to trying
    /// the stored networks.
    static constexpr std::uint64_t kRevertMillis = 10u * 60u * 1000u;

    /// The address the device answers on while hosting. 192.168.4.1 is the
    /// convention for this and collides with nothing a home router uses.
    static constexpr const char* kAddress = "192.168.4.1";

    /// Shorten the window it will sit there before giving up.
    ///
    /// Ten minutes is right for a device somebody is actually provisioning
    /// and far too long for a test: the whole of that window is time nobody
    /// can reach the device over the network it gave up.
    void setRevertMillis(std::uint64_t millis) noexcept {
        revertMillis_ = millis < 15000u ? 15000u : millis;
    }

    ~Tc002Hotspot();

    Tc002Hotspot() = default;
    Tc002Hotspot(const Tc002Hotspot&) = delete;
    Tc002Hotspot& operator=(const Tc002Hotspot&) = delete;

    /// Where the address comes back from when this gives the radio up.
    ///
    /// Not optional in practice, and the first live test is why. hostapd
    /// came up, the revert stopped it and restarted wpa_supplicant, and the
    /// device stayed unreachable until it was power-cycled - because on this
    /// platform nothing else turns an association into an address. "Give the
    /// radio back" is two steps, and only one of them was here.
    void useDhcp(Tc002Dhcp* dhcp) noexcept { dhcp_ = dhcp; }

    /// What went wrong, or what changed, once. Empty when there is nothing
    /// new.
    ///
    /// The first test left no evidence at all: the dnsmasq log and the
    /// generated configs were in /tmp, and the power cycle that recovered the
    /// device took them with it. Anything diagnosing a network failure has to
    /// report over a channel that does not depend on the network it is
    /// breaking, so this is read by the loop and put on the panel and in the
    /// ring log rather than written to a file nobody will get to.
    std::string takeEvent();

    /// Whether both daemons are still alive.
    ///
    /// An access point serving no addresses is worse than no access point:
    /// a person connects to it, waits, and concludes the device is broken.
    /// So a dead dnsmasq is a reason to revert, not a degraded mode.
    bool serving() const noexcept { return running_ && hostapdPid_ > 0 && dnsmasqPid_ > 0; }

    /// Take the radio, and serve. Returns false if anything refused, having
    /// first put the station back - a half-started hotspot with no station is
    /// the state nobody can reach.
    bool start(const std::string& ssid, std::uint64_t nowMillis);

    /// Give the radio back to wpa_supplicant.
    void stop();

    bool running() const noexcept { return running_; }

    /// The name being broadcast. Empty when not running.
    const std::string& ssid() const noexcept { return ssid_; }

    /// Call once per tick. Returns true on the tick it gave up and reverted,
    /// so a caller can clear whatever asked for the hotspot in the first place.
    bool tick(std::uint64_t nowMillis);

private:
    bool writeFile(const char* path, const std::string& contents) const;

    /// Run a command and wait for it. Returns its exit status, or -1.
    ///
    /// Waited for rather than left running: every one of these is a
    /// configuration step whose result the next step depends on, and a
    /// hotspot half-configured because ifconfig had not finished is a bug that
    /// only shows up on a slow boot.
    int run(const char* const argv[]) const;

    /// Start a daemon and keep its pid so it can be stopped again.
    int spawn(const char* const argv[]) const;

    /// Reap either daemon if it has exited. Returns true if one had.
    bool reapDead();

    void note(const std::string& text);

    Tc002Dhcp* dhcp_ = nullptr;
    std::string event_;

    std::uint64_t revertMillis_ = kRevertMillis;

    bool running_ = false;
    std::string ssid_;
    std::uint64_t startedAtMillis_ = 0;

    int hostapdPid_ = -1;
    int dnsmasqPid_ = -1;
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
