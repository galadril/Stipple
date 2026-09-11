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

/// Deliberately its own interface rather than a method on IPlatformServices:
/// rebooting is the single most destructive thing NOTRIX can do to a clock, and
/// code that needs it should have to be handed it explicitly.
class IRebooter {
public:
    virtual ~IRebooter() = default;
    virtual void reboot() = 0;
};

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
    virtual INetworkManager* network() { return nullptr; }
    virtual IRebooter* rebooter() { return nullptr; }
};

}  // namespace platform
}  // namespace notrix
