// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

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
    int rssiDbm = 0;
    std::string ipv4;
    std::string hostname;
};

/// Wi-Fi state, read-only at this layer. Joining a network is a provisioning
/// concern, not something an app or scene should be able to trigger.
class INetworkManager {
public:
    virtual ~INetworkManager() = default;
    virtual NetworkStatus status() const = 0;
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
