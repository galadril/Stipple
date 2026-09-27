// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stipple/platform/MqttClient.h"
#include "stipple/script/IScriptMqtt.h"

namespace stipple {
namespace mqtt {

/// What scripts see of the broker, and all they see of it.
///
/// Sits between `MqttService` and the script layer so neither knows about the
/// other. The service hands it messages and a connection state; the script
/// layer asks it questions. It holds a bounded cache of last-seen payloads,
/// because a script runs for a few milliseconds every few seconds and cannot
/// be awake when a message arrives — "the last thing said on this topic" is
/// the only shape of MQTT a frame-based renderer can actually use.
///
/// Every limit in `IScriptMqtt` is enforced here except the per-call publish
/// budget, which lives in the script host with the other per-call budgets
/// because only it knows where one call ends.
class ScriptGateway final : public script::IScriptMqtt {
public:
    /// Watches across all scripts.
    ///
    /// Sixteen scripts could each ask for six. In practice one or two scripts
    /// watch anything at all, and the ceiling exists so that a device cannot
    /// be talked into holding an unbounded subscription list by somebody who
    /// can POST scripts to it.
    static constexpr int kMaxWatchesTotal =
        16 * script::IScriptMqtt::kMaxWatchesPerScript;

    /// The client to publish and subscribe through. Null means no transport,
    /// and then `connected()` is false and everything refuses.
    void setClient(platform::IMqttClient* client) noexcept { client_ = client; }

    /// `stipple/{deviceId}`, the prefix a script's own topics hang off.
    void setBase(std::string base);

    /// Whether the broker is up. Pushed in by the service rather than read off
    /// the client, so a service that has decided MQTT is switched off can say
    /// so without a client existing at all.
    void setConnected(bool connected) noexcept;

    /// The device clock, for stamping arrivals and answering `ageMillis`.
    ///
    /// One clock, pushed once a frame. Reading two different clocks for the
    /// stamp and the comparison is how an age comes out negative.
    void setNowMillis(std::uint64_t nowMillis) noexcept { now_ = nowMillis; }

    /// Offer a delivered message to the cache.
    ///
    /// True when at least one watch wanted it. The service uses that to decide
    /// whether an unrecognised topic is worth complaining about: a message
    /// that some script asked for is not an unknown command.
    bool deliver(const platform::MqttMessage& message);

    /// Re-send every watch as a subscription.
    ///
    /// Called when the connection comes up. A broker that restarted has
    /// forgotten the session, and without this the scripts would go quiet in a
    /// way that looks exactly like the sensors having stopped publishing.
    void resubscribe();

    /// Number of watches held, for diagnostics.
    int watchCount() const noexcept { return static_cast<int>(watches_.size()); }

    // script::IScriptMqtt
    bool connected() const noexcept override;
    bool publish(std::string_view scriptId, std::string_view leaf,
                 std::string_view payload, bool retain) override;
    bool watch(std::string_view scriptId, std::string_view filter) override;
    const std::string* latest(std::string_view scriptId,
                              std::string_view filter) const override;
    std::int64_t ageMillis(std::string_view scriptId,
                           std::string_view filter) const override;
    void forget(std::string_view scriptId) override;

private:
    struct Watch {
        std::string scriptId;
        std::string filter;
        std::string payload;
        std::uint64_t arrivedMillis = 0;
        bool seen = false;
    };

    const Watch* find(std::string_view scriptId, std::string_view filter) const noexcept;

    platform::IMqttClient* client_ = nullptr;
    std::string base_;
    bool connected_ = false;
    std::uint64_t now_ = 0;
    std::vector<Watch> watches_;
};

}  // namespace mqtt
}  // namespace stipple
