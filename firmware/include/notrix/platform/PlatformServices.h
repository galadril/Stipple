// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "notrix/platform/Clock.h"
#include "notrix/platform/Display.h"
#include "notrix/platform/Input.h"
#include "notrix/platform/Storage.h"

namespace notrix {
namespace platform {

/// Speaker. Optional: a platform without audio returns nullptr rather than
/// silently swallowing playback requests.
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    /// Queue a tone. Must return immediately — audio must never block
    /// rendering (blueprint §16).
    virtual bool playTone(int frequencyHz, int durationMillis) = 0;

    /// Queue a named built-in sound. Returns false if unknown.
    virtual bool playSound(std::string_view name) = 0;

    virtual void stop() = 0;

    virtual void setVolume(std::uint8_t volume) = 0;
    virtual std::uint8_t volume() const = 0;
};

struct NetworkStatus {
    bool connected = false;

    /// False means this platform cannot measure a signal, which is not the
    /// same as a signal of zero. A UI showing "0 dBm" for a radio that never
    /// answered is the same confident lie as a battery reading 0% because
    /// nothing did.
    bool signalKnown = false;

    /// Negative, and closer to zero is better. Meaningful only when
    /// `signalKnown`.
    int rssiDbm = 0;

    std::string ipv4;
    std::string hostname;

    /// The network joined, where the platform can say. Empty otherwise -
    /// which is normal on a wired or simulated device, not a failure.
    std::string ssid;

    /// Whether this platform obtains and renews its own address.
    ///
    /// False does not mean "no address". It means nothing is renewing one,
    /// which is a real and different state: a device can be perfectly
    /// reachable on an address it inherited and lose it hours later when the
    /// lease it never owned runs out. Reporting that as an ordinary
    /// connection would hide the one fault nobody can diagnose afterwards
    /// (ADR 0013).
    bool leaseKnown = false;

    /// Seconds left on it. Zero when not held; 0xFFFFFFFF when the server
    /// granted one that never expires. Meaningful only when `leaseKnown`.
    std::uint32_t leaseSeconds = 0;

    /// What the client is doing, in its own words: bound, renewing,
    /// rebinding, selecting. Kept verbatim, because a state a UI does not
    /// recognise is exactly the one worth showing a person.
    std::string leaseState;
};

/// One access point in range.
struct WirelessNetwork {
    std::string ssid;

    /// Negative dBm, closer to zero is better.
    int signalDbm = 0;

    /// False for an open network. A UI that cannot tell asks for a password
    /// that does not exist, or fails to ask for one that does.
    bool secured = false;

    /// Whether this is the network the device is currently on.
    bool current = false;
};

/// Wi-Fi state. Read-only at this layer, and deliberately so: an app or a
/// scene must not be able to change which network the device is on.
class INetworkManager {
public:
    virtual ~INetworkManager() = default;
    virtual NetworkStatus status() const = 0;

    /// Whether this platform can list what is in range at all.
    ///
    /// False on the simulator and on anything wired. Callers check it rather
    /// than inferring from an empty list, because "nothing in range" and "this
    /// device cannot look" are different answers (ADR 0013).
    virtual bool canScan() const { return false; }

    /// Ask for a scan. Returns false if one could not be started.
    ///
    /// Starts it and returns; a scan takes seconds and blueprint §16 does not
    /// allow that on the render loop. Results arrive through `networks()` when
    /// the radio has them, which means a caller asks, waits, and asks again -
    /// exactly as the hardware behaves.
    virtual bool beginScan() { return false; }

    /// What the last completed scan found. Empty until one has.
    virtual std::vector<WirelessNetwork> networks() const { return {}; }

    /// False when the list above is remembered rather than current.
    ///
    /// **One radio cannot host an access point and scan at the same time**,
    /// and the moment a person most needs to pick a network is exactly when
    /// the device is hosting one. So the last scan taken before the radio
    /// changed job is kept and served, and said to be old rather than
    /// presented as what is in range now.
    virtual bool networksAreLive() const { return true; }

    /// Whether this platform can join a network at all. False on the
    /// simulator and on anything wired.
    virtual bool canJoin() const { return false; }

    /// Where a join has got to.
    struct JoinProgress {
        enum class Stage {
            /// Nothing has been asked for.
            Idle,
            /// Working on it. `detail` says which part.
            Working,
            Succeeded,
            Failed,
        };
        Stage stage = Stage::Idle;
        std::string ssid;
        /// What is happening, or why it stopped. Written for a person to
        /// read, because the only person who will read it is the one whose
        /// password did not work.
        std::string detail;
    };

    /// Ask to join. Returns false when it could not even be started - a
    /// password this device cannot set, or a platform that cannot join -
    /// with the reason in `joinProgress().detail`.
    ///
    /// Returns immediately. Joining takes tens of seconds and blueprint §16
    /// does not allow that on the render loop, so the answer arrives through
    /// `joinProgress()`. It also has to return before the work starts for a
    /// blunter reason: the request may well have arrived over the very
    /// access point that joining is about to shut down, and the reply has to
    /// get out first.
    virtual bool beginJoin(const std::string& ssid, const std::string& password) {
        (void)ssid;
        (void)password;
        return false;
    }

    virtual JoinProgress joinProgress() const { return {}; }
};

struct BatteryStatus {
    /// False means this device cannot report a battery at all, not that it is
    /// empty. Callers must tell those apart — a clock showing 0% because
    /// nothing answered is exactly the kind of confident lie ADR 0013 exists
    /// to prevent.
    bool known = false;
    /// 0-100 when `known`.
    int percent = 0;

    /// Cell voltage in millivolts, 0 when unknown.
    ///
    /// Reported alongside the percentage rather than instead of it because the
    /// two answer different questions: the percentage is what a user wants, and
    /// the voltage is what tells you whether to believe it.
    int millivolts = 0;

    /// False means this platform cannot tell whether it is charging, which is
    /// not the same as knowing it is not. Kept separate from `charging` for
    /// the same reason `known` is separate from `percent`.
    bool chargingKnown = false;

    /// True while external power is connected.
    ///
    /// Worth reporting on its own, and also the explanation for a percentage
    /// that appears to jump: a voltage-derived gauge sags under load and
    /// recovers when the cable goes back in. Measured on a TC002, the same
    /// cell read 3158 mV plugged and 3114 mV unplugged - enough to move the
    /// reported charge by several percent without anything having changed.
    bool charging = false;
};

/// Battery state.
///
/// Optional because most panels are mains-only. On the TC002 it comes from the
/// MCU over a serial link and nowhere else: there is no /sys/class/power_supply,
/// no hwmon and no IIO on this hardware.
class IPowerSource {
public:
    virtual ~IPowerSource() = default;
    virtual BatteryStatus battery() const = 0;
};

struct SoundLevel {
    /// False means this platform cannot hear, not that the room is silent.
    /// A visualiser must tell those apart, or a device with no microphone
    /// shows a flatline that looks like a bug.
    bool known = false;

    /// Amplitude, 0 to 32767. Raw rather than normalised: what counts as loud
    /// depends on the room, and an adapter cannot know that. Auto-gain belongs
    /// where the history is, which is in the app.
    int amplitude = 0;
};

/// The microphone, as a single amplitude.
///
/// Not a spectrum. The TC002 reports one 16-bit level roughly twenty times a
/// second over its MCU link and nothing more, so anything claiming to be a
/// spectrum analyser here would be inventing the bands.
class IMicrophone {
public:
    virtual ~IMicrophone() = default;
    virtual SoundLevel level() const = 0;
};

/// Deliberately its own interface rather than a method on IPlatformServices:
/// rebooting is the single most destructive thing NOTRIX can do to a clock, and
/// code that needs it should have to be handed it explicitly.
class IRebooter {
public:
    virtual ~IRebooter() = default;
    virtual void reboot() = 0;
};

class IHttpServer;
class IMqttClient;

/// The whole of the platform, as core code sees it (blueprint §53).
///
/// Services split into two kinds, and the distinction is the point:
///
///   - **Required** (display, input, clock, storage) return references. Without
///     these there is no product, so every platform must supply them and no
///     caller needs a null check.
///   - **Optional** (audio, network, rebooter) return pointers, and `nullptr`
///     means "this platform genuinely cannot do this". Callers must handle
///     absence.
///
/// The alternative — always returning an object whose methods quietly do
/// nothing — was rejected. A stub that accepts `playTone()` and stays silent
/// turns a missing capability into a bug hunt, and would let the simulator pass
/// tests for behaviour the device has never performed. Absence should be
/// visible at the call site. See ADR 0013.
class IPlatformServices {
public:
    virtual ~IPlatformServices() = default;

    /// Identifies the adapter, e.g. "simulator" or "tc002". For diagnostics
    /// only — core code must never branch on it. Anything that needs to differ
    /// between platforms belongs behind one of these interfaces instead.
    virtual const char* name() const = 0;

    virtual IFrameBufferDisplay& display() = 0;
    virtual IInputDevice& input() = 0;
    virtual ISystemClock& clock() = 0;
    virtual IStorage& storage() = 0;

    virtual IAudioOutput* audio() { return nullptr; }

    /// Battery, where there is one to report. Null on a mains-only panel.
    virtual IPowerSource* power() { return nullptr; }

    /// Microphone. Null where the hardware cannot hear.
    virtual IMicrophone* microphone() { return nullptr; }
    virtual INetworkManager* network() { return nullptr; }
    virtual IRebooter* rebooter() { return nullptr; }

    /// HTTP transport. Null everywhere today: the device adapter arrives in
    /// Phase 7, and the browser has no sockets, so the emulator binds ApiServer
    /// straight into JavaScript instead. See platform/HttpServer.h.
    virtual IHttpServer* httpServer() { return nullptr; }

    /// MQTT transport. Absent is the normal case, not a failure: §20 requires
    /// the device to be fully usable without a broker.
    virtual IMqttClient* mqtt() { return nullptr; }
};

}  // namespace platform
}  // namespace notrix
