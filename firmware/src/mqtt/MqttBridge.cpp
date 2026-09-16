// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/mqtt/MqttBridge.h"

#include "notrix/api/JsonWriter.h"
#include "notrix/config/Config.h"
#include "notrix/core/Version.h"

namespace notrix {
namespace platform {

const char* mqttStateName(MqttState state) noexcept {
    switch (state) {
        case MqttState::Disabled: return "disabled";
        case MqttState::Disconnected: return "disconnected";
        case MqttState::Connecting: return "connecting";
        case MqttState::Connected: return "connected";
    }
    return "unknown";
}

}  // namespace platform

namespace mqtt {
namespace {

/// Everything after the command prefix, or empty when the topic is not ours.
std::string_view commandOf(std::string_view topic, std::string_view base) {
    const std::string prefix = std::string(base) + "/cmd/";
    if (topic.size() <= prefix.size() || topic.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }
    return topic.substr(prefix.size());
}

/// Split "apps/clock/activate" into its first segment and the rest.
void splitHead(std::string_view path, std::string_view& head, std::string_view& rest) {
    const std::size_t slash = path.find('/');
    if (slash == std::string_view::npos) {
        head = path;
        rest = {};
        return;
    }
    head = path.substr(0, slash);
    rest = path.substr(slash + 1);
}

}  // namespace

// --- backoff -----------------------------------------------------------------

std::uint32_t Backoff::nextDelayMillis() noexcept {
    ++attempts_;

    if (current_ == 0) {
        current_ = config_.firstDelayMillis;
    } else {
        const int factor = config_.factor < 2 ? 2 : config_.factor;
        // Widened before multiplying so a long outage cannot wrap the delay
        // back round to something tiny and turn backoff into a busy loop.
        const std::uint64_t grown =
            static_cast<std::uint64_t>(current_) * static_cast<std::uint64_t>(factor);
        current_ = grown > config_.maxDelayMillis ? config_.maxDelayMillis
                                                  : static_cast<std::uint32_t>(grown);
    }

    if (current_ > config_.maxDelayMillis) {
        current_ = config_.maxDelayMillis;
    }
    return current_;
}

// --- topics ------------------------------------------------------------------

Topics Topics::build(std::string_view baseTopic, std::string_view deviceId) {
    Topics topics;
    const std::string_view root = baseTopic.empty() ? std::string_view("notrix") : baseTopic;

    topics.base = std::string(root);
    topics.base += '/';
    topics.base += deviceId;

    topics.availability = topics.base + "/availability";
    topics.status = topics.base + "/status";
    topics.button = topics.base + "/button";
    topics.commandFilter = topics.base + "/cmd/#";
    return topics;
}

std::string defaultClientId(std::string_view deviceId) {
    return "notrix-" + std::string(deviceId);
}

std::string deviceIdFromName(std::string_view name) {
    std::string id;
    id.reserve(name.size());

    bool lastWasDash = false;
    for (const char c : name) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9');
        if (alnum) {
            id += static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
            lastWasDash = false;
        } else if (!lastWasDash && !id.empty()) {
            // Runs of punctuation collapse to one separator, so "Kitchen -- 2"
            // does not become "kitchen---2".
            id += '-';
            lastWasDash = true;
        }
    }

    while (!id.empty() && id.back() == '-') {
        id.pop_back();
    }

    // A device whose name is entirely punctuation would otherwise produce an
    // empty topic segment, making `notrix//status` — legal MQTT, but a
    // nightmare to subscribe to by hand.
    return id.empty() ? std::string("device") : id;
}

// --- inbound -----------------------------------------------------------------

Bridge::Translation Bridge::translate(const platform::MqttMessage& message) const {
    Translation translation;

    const std::string_view command = commandOf(message.topic, topics_.base);
    if (command.empty()) {
        return translation;
    }

    std::string_view head;
    std::string_view rest;
    splitHead(command, head, rest);

    api::Request& request = translation.request;
    request.body = message.payload;

    // Each command is one API call. Anything the API refuses, MQTT refuses the
    // same way and for the same reason.
    if (head == "notify" && rest.empty()) {
        request.method = api::Method::Post;
        request.path = "/api/v1/notifications";
    } else if (head == "settings" && rest.empty()) {
        request.method = api::Method::Patch;
        request.path = "/api/v1/settings";
    } else if (head == "apps" && rest.empty()) {
        request.method = api::Method::Post;
        request.path = "/api/v1/apps";
    } else if (head == "apps" && !rest.empty()) {
        std::string_view appId;
        std::string_view action;
        splitHead(rest, appId, action);

        if (appId.empty()) {
            return translation;
        }
        if (action == "activate") {
            request.method = api::Method::Post;
            request.path = "/api/v1/apps/" + std::string(appId) + "/activate";
        } else if (action.empty()) {
            // An empty payload deletes; anything else updates. Publishing an
            // empty retained message is how MQTT conventionally says "this is
            // gone", so honouring it here means a retained app can be cleared
            // the way the rest of the ecosystem expects.
            const bool remove = message.payload.empty();
            request.method = remove ? api::Method::Delete : api::Method::Patch;
            request.path = "/api/v1/apps/" + std::string(appId);
        } else {
            return translation;
        }
    } else if (head == "reboot" && rest.empty()) {
        request.method = api::Method::Post;
        request.path = "/api/v1/system/reboot";
    } else {
        return translation;  // unknown: reported by the caller, never guessed
    }

    translation.understood = true;

    // Results go under /result/, deliberately outside the /cmd/# subscription.
    // Answering inside it would echo every reply straight back to this device,
    // which then reports it as an unknown command — a self-inflicted loop that
    // only shows up once a broker is actually attached.
    translation.replyTopic = topics_.base + "/result/" + std::string(command);
    return translation;
}

// --- outbound ----------------------------------------------------------------

std::string Bridge::statusPayload(const config::Config& settings,
                                  std::string_view activeAppId,
                                  std::uint64_t uptimeMillis,
                                  bool healthy,
                                  int rssiDbm,
                                  bool hasRssi) {
    api::JsonWriter writer;
    writer.beginObject()
        .member("online", true)
        .member("version", kVersion)
        .member("uptimeMillis", static_cast<std::int64_t>(uptimeMillis))
        .member("healthy", healthy)
        .member("name", settings.deviceName)
        .member("brightness", static_cast<int>(settings.display.brightness))
        .member("power", settings.display.power)
        .member("volumePercent", static_cast<int>(settings.audio.volumePercent))
        .member("activeApp", activeAppId);

    if (hasRssi) {
        writer.member("rssiDbm", rssiDbm);
    }

    // No credentials, here or anywhere else that leaves the device (§22). The
    // MQTT block is deliberately absent from status: a broker republishing
    // retained state is the last place a password should be able to surface.
    writer.endObject();
    return writer.take();
}

std::string Bridge::buttonPayload(std::string_view action, int repeat, bool longPress) {
    api::JsonWriter writer;
    writer.beginObject()
        .member("action", action)
        .member("repeat", repeat)
        .member("longPress", longPress)
        .endObject();
    return writer.take();
}

platform::MqttConnectOptions Bridge::connectOptions(const config::Config& settings,
                                                    const Topics& topics) {
    platform::MqttConnectOptions options;
    options.host = settings.mqtt.host;
    options.port = settings.mqtt.port;
    options.clientId = settings.mqtt.clientId.empty()
                           ? defaultClientId(deviceIdFromName(settings.deviceName))
                           : settings.mqtt.clientId;
    options.username = settings.mqtt.username;
    options.password = settings.mqtt.password;
    options.tls = settings.mqtt.tls;
    options.keepAliveSeconds = settings.mqtt.keepAliveSeconds;

    // The will is what makes availability trustworthy: a device that loses power
    // cannot publish "offline" itself, so the broker does it.
    options.willTopic = topics.availability;
    options.willPayload = std::string(kOffline);
    options.willRetained = true;
    return options;
}

}  // namespace mqtt
}  // namespace notrix
