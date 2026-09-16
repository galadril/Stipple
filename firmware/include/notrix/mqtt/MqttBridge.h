// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "notrix/api/Http.h"
#include "notrix/platform/MqttClient.h"

namespace notrix {

namespace config {
struct Config;
}

namespace mqtt {

/// How long to wait before the next connection attempt.
///
/// Exponential with a ceiling: a broker that is down stays down for a while, and
/// a device retrying every second is both useless and rude to the network. The
/// ceiling matters more than the growth rate — without one, a device that was
/// offline overnight would take hours to notice the broker came back.
class Backoff {
public:
    struct Config {
        std::uint32_t firstDelayMillis = 1000;
        std::uint32_t maxDelayMillis = 60000;
        /// Doubling, expressed as a multiplier so it can be tuned without
        /// changing the shape of the code.
        int factor = 2;
    };

    // Two constructors rather than a defaulted argument: Clang refuses to use a
    // nested type's default member initializers inside the enclosing class
    // definition, even though MSVC accepts it. Same fix as FrameScheduler.
    Backoff() noexcept = default;
    explicit Backoff(Config config) noexcept : config_(config) {}

    /// Delay to wait after the failure that has just happened.
    std::uint32_t nextDelayMillis() noexcept;

    /// Called on a successful connection, so the next outage starts short again.
    void reset() noexcept { current_ = 0; }

    std::uint32_t currentDelayMillis() const noexcept { return current_; }
    int attempts() const noexcept { return attempts_; }

private:
    Config config_;
    std::uint32_t current_ = 0;
    int attempts_ = 0;
};

/// The topic namespace (blueprint §20): `notrix/{deviceId}/...`.
///
/// Built once and reused, because assembling these per message would allocate on
/// every publish. Grouped in a struct rather than scattered as free functions so
/// that the whole namespace is visible in one place — it is public API, and a
/// topic that changes silently breaks every automation pointed at it.
struct Topics {
    std::string base;          ///< notrix/{deviceId}
    std::string availability;  ///< .../availability
    std::string status;        ///< .../status
    std::string button;        ///< .../button
    std::string commandFilter; ///< .../cmd/# — everything inbound

    static Topics build(std::string_view baseTopic, std::string_view deviceId);
};

inline constexpr std::string_view kOnline = "online";
inline constexpr std::string_view kOffline = "offline";

/// Translates between MQTT and the device.
///
/// Inbound commands are turned into `api::Request` objects and answered by the
/// ordinary API server. That is the whole design: it makes "fully usable from
/// MQTT without HTTP" true by construction rather than by parallel
/// implementation, and means validation, limits and error shapes cannot drift
/// between the two surfaces. A command that MQTT can express but HTTP cannot
/// would be a bug, not a feature.
///
/// Pure. Messages in, messages out; no sockets and no clock of its own.
class Bridge {
public:
    /// What a command should be turned into.
    struct Translation {
        bool understood = false;
        api::Request request;
        /// Topic to answer on, empty when the sender asked for no reply.
        std::string replyTopic;
    };

    Bridge() = default;

    void setTopics(Topics topics) { topics_ = std::move(topics); }
    const Topics& topics() const noexcept { return topics_; }

    /// Map an inbound message to an API request.
    ///
    /// Returns `understood = false` for anything outside the command namespace
    /// or naming an unknown command. Unknown commands are *reported*, never
    /// guessed at: silently ignoring a typo'd topic is indistinguishable from
    /// the device being broken.
    Translation translate(const platform::MqttMessage& message) const;

    /// Retained device state for the status topic (§20).
    static std::string statusPayload(const config::Config& settings,
                                     std::string_view activeAppId,
                                     std::uint64_t uptimeMillis,
                                     bool healthy,
                                     int rssiDbm,
                                     bool hasRssi);

    /// A button event, for automations that want to react to the hardware.
    static std::string buttonPayload(std::string_view action, int repeat, bool longPress);

    /// Connection options from stored settings, with the will already set so a
    /// device that drops off is marked offline by the broker.
    static platform::MqttConnectOptions connectOptions(const config::Config& settings,
                                                       const Topics& topics);

private:
    Topics topics_;
};

/// Default client id when none is configured: `notrix-{deviceId}`.
///
/// Never the GitHub owner, and never a random value — a client id that changes
/// every boot leaves stale sessions accumulating on the broker.
std::string defaultClientId(std::string_view deviceId);

/// A stable per-device identifier derived from the device name.
///
/// Lower-cased, non-alphanumerics folded to '-', runs collapsed, trimmed. Topics
/// containing spaces or '#' would be at best awkward and at worst unsubscribable,
/// so this is sanitisation rather than decoration.
std::string deviceIdFromName(std::string_view name);

}  // namespace mqtt
}  // namespace notrix
