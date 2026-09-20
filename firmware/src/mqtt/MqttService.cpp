// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/mqtt/MqttService.h"

#include "notrix/api/ApiServer.h"
#include "notrix/api/JsonWriter.h"
#include "notrix/config/Config.h"
#include "notrix/core/Log.h"

namespace notrix {
namespace mqtt {
namespace {

using platform::MqttMessage;
using platform::MqttState;

/// Identifies the settings a connection was made with.
///
/// The password is deliberately absent: changing it should reconnect, but a
/// credential must not sit in a member that diagnostics might one day print
/// (§22). Its length stands in, which distinguishes a change without recording
/// the value.
std::string fingerprint(const config::MqttSettings& mqtt) {
    return mqtt.host + '|' + std::to_string(mqtt.port) + '|' + mqtt.clientId + '|' +
           mqtt.baseTopic + '|' + mqtt.username + '|' + std::to_string(mqtt.password.size()) +
           '|' + (mqtt.tls ? "tls" : "plain");
}

}  // namespace

void MqttService::log(log::Level level, std::string_view message) {
    if (context_.logger != nullptr) {
        context_.logger->write(level, nowMillis_, message);
    }
}

platform::MqttState MqttService::state() const noexcept {
    if (!enabled_ || context_.client == nullptr) {
        return MqttState::Disabled;
    }
    return context_.client->state();
}

// --- lifecycle ---------------------------------------------------------------

void MqttService::configure(std::uint64_t nowMillis) {
    if (nowMillis != 0) {
        nowMillis_ = nowMillis;
    }
    if (context_.settings == nullptr) {
        return;
    }
    const config::MqttSettings& mqtt = context_.settings->mqtt;

    // A platform with no MQTT transport is not a failure — §20 makes the broker
    // optional — but a user who switched MQTT on deserves to know why nothing
    // happened.
    if (mqtt.enabled && context_.client == nullptr) {
        log(log::Level::Warn, "MQTT enabled but this platform has no MQTT transport");
    }

    const bool wanted = mqtt.enabled && context_.client != nullptr && !mqtt.host.empty();
    const std::string wantedFor = wanted ? fingerprint(mqtt) : std::string();

    if (wanted == enabled_ && wantedFor == configuredFor_) {
        return;  // nothing that matters has changed
    }

    if (enabled_ && context_.client != nullptr) {
        shutdown();
    }

    enabled_ = wanted;
    configuredFor_ = wantedFor;
    backoff_.reset();
    nextAttemptMillis_ = 0;
    announced_ = false;
    statusDue_ = true;

    if (!enabled_) {
        return;
    }

    bridge_.setTopics(
        Topics::build(mqtt.baseTopic, deviceIdFromName(context_.settings->deviceName)));
    log(log::Level::Info, "MQTT enabled for " + mqtt.host);
}

void MqttService::connectNow(std::uint64_t nowMillis) {
    ++stats_.connectAttempts;

    const platform::MqttConnectOptions options =
        Bridge::connectOptions(*context_.settings, bridge_.topics());

    if (!context_.client->connect(options, *this)) {
        // Arguments the adapter rejected outright. Retrying identical arguments
        // sooner will not help, so this takes the same backoff as an outage.
        nextAttemptMillis_ = nowMillis + backoff_.nextDelayMillis();
        log(log::Level::Error, "MQTT connection refused by the transport");
        return;
    }

    if (context_.client->state() != MqttState::Connected) {
        nextAttemptMillis_ = nowMillis + backoff_.nextDelayMillis();
    }
}

void MqttService::onConnected() {
    backoff_.reset();
    announced_ = true;

    // Availability first, retained, so a subscriber that arrives later still
    // learns this device is up.
    MqttMessage availability;
    availability.topic = bridge_.topics().availability;
    availability.payload = std::string(kOnline);
    availability.retained = true;
    publish(availability);

    context_.client->subscribe(bridge_.topics().commandFilter, 0);
    statusDue_ = true;

    log(log::Level::Info, "MQTT connected");
}

void MqttService::shutdown() {
    if (!enabled_ || context_.client == nullptr) {
        return;
    }

    if (context_.client->state() == MqttState::Connected && announced_) {
        // Said explicitly rather than left to the will, so a clean restart shows
        // as offline immediately instead of after a keepalive timeout.
        MqttMessage goodbye;
        goodbye.topic = bridge_.topics().availability;
        goodbye.payload = std::string(kOffline);
        goodbye.retained = true;
        publish(goodbye);
    }

    context_.client->disconnect();
    announced_ = false;
}

void MqttService::tick(std::uint64_t nowMillis) {
    nowMillis_ = nowMillis;
    if (!enabled_ || context_.client == nullptr) {
        return;
    }

    context_.client->poll(nowMillis);

    if (context_.client->state() != MqttState::Connected) {
        if (nowMillis < nextAttemptMillis_) {
            return;  // still backing off
        }
        connectNow(nowMillis);

        // Fall through when the connection came up during that call. Returning
        // here would leave the status topic empty until the next tick, so a
        // subscriber watching a device that just booted would see it announce
        // itself as online and then say nothing about its state.
        if (context_.client->state() != MqttState::Connected) {
            return;
        }
    }

    const bool intervalElapsed =
        config_.statusIntervalMillis > 0 &&
        nowMillis - lastStatusMillis_ >= config_.statusIntervalMillis;

    if (statusDue_ || intervalElapsed) {
        publishStatus(nowMillis);
    }
}

// --- publishing --------------------------------------------------------------

bool MqttService::publish(const MqttMessage& message) {
    if (context_.client == nullptr || !context_.client->publish(message)) {
        ++stats_.publishFailures;
        return false;
    }
    ++stats_.published;
    return true;
}

void MqttService::publishStatus(std::uint64_t nowMillis) {
    lastStatusMillis_ = nowMillis;
    statusDue_ = false;

    MqttMessage message;
    message.topic = bridge_.topics().status;
    message.retained = true;
    message.payload =
        Bridge::statusPayload(*context_.settings, deviceState_.activeAppId, nowMillis,
                              deviceState_.healthy, deviceState_.rssiDbm, deviceState_.hasRssi);
    publish(message);
}

void MqttService::publishButton(std::string_view action, int repeat, bool longPress) {
    if (!enabled_ || state() != MqttState::Connected) {
        return;
    }

    MqttMessage message;
    message.topic = bridge_.topics().button;
    message.payload = Bridge::buttonPayload(action, repeat, longPress);
    message.retained = false;  // an event, not a state
    publish(message);
}

// --- inbound -----------------------------------------------------------------

void MqttService::onStateChanged(MqttState state) {
    if (state == MqttState::Connected) {
        onConnected();
        return;
    }
    if (state == MqttState::Disconnected && announced_) {
        announced_ = false;
        log(log::Level::Warn, "MQTT disconnected");
    }
}

void MqttService::onMessage(const MqttMessage& message) {
    ++stats_.received;

    if (context_.api == nullptr) {
        return;
    }

    const Bridge::Translation translation = bridge_.translate(message);
    if (!translation.understood) {
        // Reported, not ignored. A typo in a topic is otherwise indistinguishable
        // from the device being broken, and this is the only place anyone can
        // find out.
        ++stats_.commandsRejected;
        log(log::Level::Warn, "MQTT ignored unknown command on " + message.topic);
        return;
    }

    const api::Response response = context_.api->handle(translation.request, lastStatusMillis_);
    ++stats_.commandsHandled;

    // Answer on a result topic so a caller can tell success from silence. Not
    // retained: a stale success from last week would be actively misleading.
    if (!translation.replyTopic.empty()) {
        MqttMessage reply;
        reply.topic = translation.replyTopic;
        reply.retained = false;

        api::JsonWriter writer;
        writer.beginObject().member("status", response.status);
        writer.rawMember("body", response.body.empty() ? "null" : response.body);
        writer.endObject();
        reply.payload = writer.take();
        publish(reply);
    }

    if (response.status < 400) {
        statusDue_ = true;  // something changed; tell subscribers
    } else {
        log(log::Level::Warn, "MQTT command rejected on " + message.topic);
    }
}

}  // namespace mqtt
}  // namespace notrix
