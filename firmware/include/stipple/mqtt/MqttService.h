// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "stipple/core/Log.h"
#include "stipple/mqtt/MqttBridge.h"
#include "stipple/platform/MqttClient.h"

namespace stipple {

namespace api {
class ApiServer;
}

namespace config {
struct Config;
}

namespace mqtt {

/// Everything the service needs, passed in rather than reached for.
struct ServiceContext {
    platform::IMqttClient* client = nullptr;
    api::ApiServer* api = nullptr;
    const config::Config* settings = nullptr;
    log::RingLog* logger = nullptr;
};

/// Owns the connection lifecycle and turns messages into API calls.
///
/// Sits above the platform boundary, so it is the same code on device and in the
/// simulator; only `IMqttClient` differs. It never blocks and never sleeps —
/// `tick` is called from the application loop and decides from the clock whether
/// it is time to try again.
class MqttService : public platform::IMqttListener {
public:
    struct Config {
        Backoff::Config backoff;

        /// How often to republish status while connected. Status is retained, so
        /// this is a heartbeat for anything watching rather than the only way to
        /// learn the state.
        std::uint32_t statusIntervalMillis = 30000;
    };

    MqttService() = default;
    explicit MqttService(Config config) noexcept : config_(config), backoff_(config.backoff) {}

    void setContext(const ServiceContext& context) { context_ = context; }

    /// Read settings and decide whether MQTT should run at all. Safe to call
    /// again after a settings change: a broker that has been reconfigured is
    /// disconnected and reconnected rather than left pointing at the old one.
    /// `nowMillis` is only used to stamp log lines, so a caller outside the
    /// tick loop can still produce entries with a real time on them. It
    /// defaults to leaving the clock where it was.
    void configure(std::uint64_t nowMillis = 0);

    /// Publish or withdraw the Home Assistant discovery entities.
    ///
    /// Called when the connection comes up and whenever the discovery setting
    /// changes. Idempotent: the messages are retained, so republishing the same
    /// thing costs a broker write and changes nothing.
    void publishDiscovery(bool enabled);

    /// Drive the connection. Called every loop iteration; cheap when idle.
    void tick(std::uint64_t nowMillis);

    /// Say goodbye properly, so availability flips to offline without waiting
    /// for the broker's keepalive to expire.
    void shutdown();

    /// Publish a button event (§20). Fire-and-forget: a dropped button event is
    /// not worth queueing, and §38 forbids growing a queue to avoid saying no.
    void publishButton(std::string_view action, int repeat, bool longPress);

    /// Republish status at the next opportunity, after something changed that a
    /// subscriber would care about.
    void invalidateStatus() noexcept { statusDue_ = true; }

    /// Live device state for the status payload.
    ///
    /// Pushed in rather than pulled, because the alternative is handing this
    /// service a pointer to the carousel and the network manager and letting it
    /// reach around the host. Keeping it a plain struct means the status topic
    /// cannot accidentally start depending on something it should not see.
    struct DeviceState {
        std::string activeAppId;
        bool healthy = false;
        int rssiDbm = 0;
        bool hasRssi = false;
        int batteryPercent = 0;
        bool hasBattery = false;
    };
    void setDeviceState(DeviceState state) { deviceState_ = std::move(state); }

    platform::MqttState state() const noexcept;
    const Topics& topics() const noexcept { return bridge_.topics(); }

    /// Counters for diagnostics. Cheap, and the first thing anyone asks for when
    /// an automation does not fire.
    struct Stats {
        std::uint32_t published = 0;
        std::uint32_t received = 0;
        std::uint32_t commandsHandled = 0;
        std::uint32_t commandsRejected = 0;
        std::uint32_t publishFailures = 0;
        std::uint32_t connectAttempts = 0;
    };
    const Stats& stats() const noexcept { return stats_; }

    // platform::IMqttListener
    void onMessage(const platform::MqttMessage& message) override;
    void onStateChanged(platform::MqttState state) override;

private:
    void connectNow(std::uint64_t nowMillis);
    void onConnected();
    bool publish(const platform::MqttMessage& message);
    void publishStatus(std::uint64_t nowMillis);
    void log(log::Level level, std::string_view message);

    Config config_;
    ServiceContext context_;
    Bridge bridge_;
    Backoff backoff_;
    Stats stats_;

    bool enabled_ = false;
    /// Settings fingerprint, so a changed broker reconnects and an unchanged one
    /// does not churn the connection on every save.
    std::string configuredFor_;

    std::uint64_t nextAttemptMillis_ = 0;
    std::uint64_t lastStatusMillis_ = 0;
    /// When this service last knew the time. Separate from
    /// lastStatusMillis_, which only moves when a status is published -
    /// using that to stamp a log froze every MQTT line at the moment of
    /// the last status, which is why the log read out of order.
    std::uint64_t nowMillis_ = 0;
    /// What discovery state the broker currently holds, so a reconnect does not
    /// republish a dozen retained messages it already has.
    bool discoveryPublished_ = false;
    DeviceState deviceState_;
    bool statusDue_ = true;
    bool announced_ = false;
};

}  // namespace mqtt
}  // namespace stipple
