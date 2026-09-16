// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/mqtt/MqttService.h"

#include <string>

#include "notrix/api/ApiServer.h"
#include "notrix/app/AppRegistry.h"
#include "notrix/app/Carousel.h"
#include "notrix/config/Config.h"
#include "notrix/core/Log.h"
#include "notrix/mqtt/MqttBridge.h"
#include "notrix/notify/Notifications.h"
#include "notrix/platform/simulator/SimulatorPlatform.h"
#include "support/TestFramework.h"

using notrix::api::ApiContext;
using notrix::api::ApiServer;
using notrix::config::Config;
using notrix::mqtt::Backoff;
using notrix::mqtt::Bridge;
using notrix::mqtt::MqttService;
using notrix::mqtt::ServiceContext;
using notrix::mqtt::Topics;
using notrix::platform::MqttMessage;
using notrix::platform::MqttState;
using notrix::platform::simulator::SimulatorPlatform;

namespace {

int stateCode(MqttState state) { return static_cast<int>(state); }

/// A device with MQTT enabled, wired the way ApplicationHost wires it.
struct Fixture {
    SimulatorPlatform platform;
    notrix::app::AppRegistry apps;
    notrix::app::Carousel carousel{apps};
    notrix::notify::NotificationQueue notifications;
    notrix::log::RingLog logger;
    Config config;
    notrix::config::ConfigStore configStore{platform.storage()};
    ApiServer api;
    MqttService service;

    Fixture() : api(makeContext()) {
        config.mqtt.enabled = true;
        config.mqtt.host = "broker.local";
        config.deviceName = "notrix";

        ServiceContext context;
        context.client = platform.mqtt();
        context.api = &api;
        context.settings = &config;
        context.logger = &logger;
        service.setContext(context);
    }

    ApiContext makeContext() {
        ApiContext context;
        context.apps = &apps;
        context.carousel = &carousel;
        context.notifications = &notifications;
        context.config = &config;
        context.configStore = &configStore;
        context.platform = &platform;
        context.logger = &logger;
        return context;
    }

    /// Bring the connection up, as the host's loop would.
    void connect(std::uint64_t nowMillis = 0) {
        service.configure();
        service.tick(nowMillis);
    }

    notrix::platform::simulator::SimulatorMqtt& broker() { return platform.simulatedMqtt(); }

    /// Deliver a command and let the service answer it.
    void command(const std::string& suffix, const std::string& payload) {
        MqttMessage message;
        message.topic = service.topics().base + "/cmd/" + suffix;
        message.payload = payload;
        broker().deliver(message);
    }

    std::string payloadOn(const std::string& topic) {
        const MqttMessage* message = broker().lastOn(topic);
        return message != nullptr ? message->payload : std::string();
    }
};

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

// --- backoff -----------------------------------------------------------------

NOTRIX_TEST(MqttBackoff, GrowsThenSettlesAtTheCeiling) {
    Backoff::Config config;
    config.firstDelayMillis = 100;
    config.maxDelayMillis = 800;
    Backoff backoff(config);

    NOTRIX_CHECK_EQ(static_cast<int>(backoff.nextDelayMillis()), 100);
    NOTRIX_CHECK_EQ(static_cast<int>(backoff.nextDelayMillis()), 200);
    NOTRIX_CHECK_EQ(static_cast<int>(backoff.nextDelayMillis()), 400);
    NOTRIX_CHECK_EQ(static_cast<int>(backoff.nextDelayMillis()), 800);
    NOTRIX_CHECK_EQ(static_cast<int>(backoff.nextDelayMillis()), 800);
}

NOTRIX_TEST(MqttBackoff, ALongOutageNeverWrapsBackToZero) {
    // The failure this guards: an overflowing multiply turns backoff into a busy
    // loop, which is worst exactly when the network is already in trouble.
    Backoff backoff;
    std::uint32_t previous = 0;

    for (int i = 0; i < 2000; ++i) {
        const std::uint32_t delay = backoff.nextDelayMillis();
        NOTRIX_CHECK(delay >= previous || delay == 60000);
        NOTRIX_CHECK(delay > 0);
        NOTRIX_CHECK(delay <= 60000);
        previous = delay;
    }
    NOTRIX_CHECK_EQ(static_cast<int>(previous), 60000);
}

NOTRIX_TEST(MqttBackoff, SuccessStartsTheNextOutageShortAgain) {
    Backoff backoff;
    backoff.nextDelayMillis();
    backoff.nextDelayMillis();
    backoff.nextDelayMillis();

    backoff.reset();
    NOTRIX_CHECK_EQ(static_cast<int>(backoff.currentDelayMillis()), 0);
    NOTRIX_CHECK_EQ(static_cast<int>(backoff.nextDelayMillis()), 1000);
}

// --- identifiers and topics --------------------------------------------------

NOTRIX_TEST(MqttTopics, DeviceIdIsSafeToPutInATopic) {
    using notrix::mqtt::deviceIdFromName;

    NOTRIX_CHECK_EQ(deviceIdFromName("notrix"), std::string("notrix"));
    NOTRIX_CHECK_EQ(deviceIdFromName("Kitchen Clock"), std::string("kitchen-clock"));
    NOTRIX_CHECK_EQ(deviceIdFromName("Kitchen -- 2"), std::string("kitchen-2"));
    NOTRIX_CHECK_EQ(deviceIdFromName("  spaced  "), std::string("spaced"));

    // Wildcards and separators would make the topic unsubscribable or ambiguous.
    NOTRIX_CHECK_EQ(deviceIdFromName("a/b#c+d"), std::string("a-b-c-d"));

    // A name with nothing usable in it still has to produce a topic segment.
    NOTRIX_CHECK_EQ(deviceIdFromName("###"), std::string("device"));
    NOTRIX_CHECK_EQ(deviceIdFromName(""), std::string("device"));
}

NOTRIX_TEST(MqttTopics, NamespaceFollowsTheBlueprint) {
    const Topics topics = Topics::build("notrix", "abc123");

    NOTRIX_CHECK_EQ(topics.base, std::string("notrix/abc123"));
    NOTRIX_CHECK_EQ(topics.availability, std::string("notrix/abc123/availability"));
    NOTRIX_CHECK_EQ(topics.status, std::string("notrix/abc123/status"));
    NOTRIX_CHECK_EQ(topics.button, std::string("notrix/abc123/button"));
    NOTRIX_CHECK_EQ(topics.commandFilter, std::string("notrix/abc123/cmd/#"));
}

NOTRIX_TEST(MqttTopics, AnEmptyBaseFallsBackRatherThanLeadingWithASlash) {
    NOTRIX_CHECK_EQ(Topics::build("", "abc").base, std::string("notrix/abc"));
}

// --- translation -------------------------------------------------------------

NOTRIX_TEST(MqttBridge, CommandsBecomeApiCalls) {
    Bridge bridge;
    bridge.setTopics(Topics::build("notrix", "abc"));

    const auto translate = [&bridge](const char* topic, const char* payload) {
        MqttMessage message;
        message.topic = topic;
        message.payload = payload;
        return bridge.translate(message);
    };

    auto notify = translate("notrix/abc/cmd/notify", R"({"text":"hi"})");
    NOTRIX_CHECK(notify.understood);
    NOTRIX_CHECK_EQ(notify.request.path, std::string("/api/v1/notifications"));
    NOTRIX_CHECK(notify.request.method == notrix::api::Method::Post);
    NOTRIX_CHECK_EQ(notify.request.body, std::string(R"({"text":"hi"})"));

    auto settings = translate("notrix/abc/cmd/settings", R"({"display":{"brightness":10}})");
    NOTRIX_CHECK(settings.understood);
    NOTRIX_CHECK_EQ(settings.request.path, std::string("/api/v1/settings"));
    NOTRIX_CHECK(settings.request.method == notrix::api::Method::Patch);

    auto activate = translate("notrix/abc/cmd/apps/clock/activate", "");
    NOTRIX_CHECK(activate.understood);
    NOTRIX_CHECK_EQ(activate.request.path, std::string("/api/v1/apps/clock/activate"));
    NOTRIX_CHECK(activate.request.method == notrix::api::Method::Post);

    auto reboot = translate("notrix/abc/cmd/reboot", "");
    NOTRIX_CHECK(reboot.understood);
    NOTRIX_CHECK_EQ(reboot.request.path, std::string("/api/v1/system/reboot"));
}

NOTRIX_TEST(MqttBridge, AnEmptyRetainedAppMeansDelete) {
    // Publishing an empty retained message is how MQTT conventionally says "this
    // is gone", so a retained app can be cleared the way the ecosystem expects.
    Bridge bridge;
    bridge.setTopics(Topics::build("notrix", "abc"));

    MqttMessage message;
    message.topic = "notrix/abc/cmd/apps/weather";

    message.payload = "";
    NOTRIX_CHECK(bridge.translate(message).request.method == notrix::api::Method::Delete);

    message.payload = R"({"enabled":false})";
    NOTRIX_CHECK(bridge.translate(message).request.method == notrix::api::Method::Patch);
}

NOTRIX_TEST(MqttBridge, UnknownTopicsAreNotGuessedAt) {
    Bridge bridge;
    bridge.setTopics(Topics::build("notrix", "abc"));

    const char* rejected[] = {
        "notrix/abc/cmd/explode",          // no such command
        "notrix/abc/cmd/apps/clock/spin",  // no such action
        "notrix/abc/cmd/",                 // nothing after the prefix
        "notrix/abc/status",               // ours, but not a command
        "notrix/other/cmd/notify",         // another device
        "somewhere/else/cmd/notify",
        "",
    };

    for (const char* topic : rejected) {
        MqttMessage message;
        message.topic = topic;
        NOTRIX_CHECK_FALSE(bridge.translate(message).understood);
    }
}

NOTRIX_TEST(MqttBridge, RepliesLandOutsideTheCommandSubscription) {
    // Answering inside /cmd/# would echo every reply back to this device, which
    // would then report it as an unknown command — a loop that only appears once
    // a real broker is attached.
    Bridge bridge;
    bridge.setTopics(Topics::build("notrix", "abc"));

    MqttMessage message;
    message.topic = "notrix/abc/cmd/notify";
    const auto translation = bridge.translate(message);

    NOTRIX_CHECK(translation.understood);
    NOTRIX_CHECK_EQ(translation.replyTopic, std::string("notrix/abc/result/notify"));
    NOTRIX_CHECK(translation.replyTopic.find("/cmd/") == std::string::npos);
}

NOTRIX_TEST(MqttBridge, ConnectOptionsCarryAWill) {
    Config config;
    config.deviceName = "Kitchen Clock";
    config.mqtt.host = "broker.local";

    const Topics topics = Topics::build("notrix", "kitchen-clock");
    const auto options = Bridge::connectOptions(config, topics);

    // Availability that depends on the device being well enough to announce its
    // own death is not availability.
    NOTRIX_CHECK_EQ(options.willTopic, topics.availability);
    NOTRIX_CHECK_EQ(options.willPayload, std::string("offline"));
    NOTRIX_CHECK(options.willRetained);

    // A client id that changes every boot leaves stale sessions on the broker.
    NOTRIX_CHECK_EQ(options.clientId, std::string("notrix-kitchen-clock"));
}

// --- the service -------------------------------------------------------------

NOTRIX_TEST(MqttService, DisabledByDefaultAndNeverDialsOut) {
    // §20: a device must be fully usable without a broker, and must never talk
    // to one nobody asked it to.
    Fixture fixture;
    fixture.config.mqtt.enabled = false;

    fixture.service.configure();
    fixture.service.tick(0);

    NOTRIX_CHECK_EQ(stateCode(fixture.service.state()), stateCode(MqttState::Disabled));
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().connectAttempts), 0);
    NOTRIX_CHECK(fixture.broker().published().empty());
}

NOTRIX_TEST(MqttService, EnabledWithNoHostStaysOff) {
    Fixture fixture;
    fixture.config.mqtt.host.clear();

    fixture.connect();
    NOTRIX_CHECK_EQ(stateCode(fixture.service.state()), stateCode(MqttState::Disabled));
}

NOTRIX_TEST(MqttService, AnnouncesItselfOnConnect) {
    Fixture fixture;
    fixture.connect();

    NOTRIX_CHECK_EQ(stateCode(fixture.service.state()), stateCode(MqttState::Connected));

    const MqttMessage* availability = fixture.broker().lastOn(fixture.service.topics().availability);
    NOTRIX_REQUIRE(availability != nullptr);
    NOTRIX_CHECK_EQ(availability->payload, std::string("online"));
    // Retained, so a subscriber arriving later still learns the device is up.
    NOTRIX_CHECK(availability->retained);

    // And it listens for commands.
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.broker().subscriptions().size()), 1);
    NOTRIX_CHECK_EQ(fixture.broker().subscriptions()[0], fixture.service.topics().commandFilter);
}

NOTRIX_TEST(MqttService, PublishesRetainedStatus) {
    Fixture fixture;
    fixture.config.display.brightness = 200;

    MqttService::DeviceState state;
    state.activeAppId = "clock";
    state.healthy = true;
    fixture.service.setDeviceState(state);
    fixture.connect();

    const MqttMessage* status = fixture.broker().lastOn(fixture.service.topics().status);
    NOTRIX_REQUIRE(status != nullptr);
    NOTRIX_CHECK(status->retained);
    NOTRIX_CHECK(contains(status->payload, "\"activeApp\":\"clock\""));
    NOTRIX_CHECK(contains(status->payload, "\"brightness\":200"));
    NOTRIX_CHECK(contains(status->payload, "\"online\":true"));
}

NOTRIX_TEST(MqttService, SayingGoodbyeDoesNotWaitForAKeepalive) {
    Fixture fixture;
    fixture.connect();
    fixture.broker().clear();

    fixture.service.shutdown();

    const MqttMessage* availability = fixture.broker().lastOn(fixture.service.topics().availability);
    NOTRIX_REQUIRE(availability != nullptr);
    NOTRIX_CHECK_EQ(availability->payload, std::string("offline"));
    NOTRIX_CHECK(availability->retained);
}

NOTRIX_TEST(MqttService, RetriesAnUnreachableBrokerWithBackoff) {
    Fixture fixture;
    fixture.broker().setReachable(false);

    fixture.service.configure();

    fixture.service.tick(0);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().connectAttempts), 1);

    // Too soon: the whole point is not to hammer a broker that is down.
    fixture.service.tick(500);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().connectAttempts), 1);

    fixture.service.tick(1000);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().connectAttempts), 2);

    // Second wait is longer than the first.
    fixture.service.tick(2000);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().connectAttempts), 2);
    fixture.service.tick(3000);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().connectAttempts), 3);
}

NOTRIX_TEST(MqttService, ABrokerThatComesBackIsPickedUp) {
    Fixture fixture;
    fixture.broker().setReachable(false);
    fixture.service.configure();
    fixture.service.tick(0);
    NOTRIX_CHECK_EQ(stateCode(fixture.service.state()), stateCode(MqttState::Disconnected));

    fixture.broker().setReachable(true);
    for (std::uint64_t now = 1000; now <= 20000; now += 1000) {
        fixture.service.tick(now);
    }

    NOTRIX_CHECK_EQ(stateCode(fixture.service.state()), stateCode(MqttState::Connected));
    NOTRIX_CHECK_EQ(fixture.payloadOn(fixture.service.topics().availability),
                    std::string("online"));
}

NOTRIX_TEST(MqttService, PublishFailuresAreCountedNotQueued) {
    // §38 forbids an unbounded queue, so a full transport must be allowed to say
    // no and the caller must notice.
    Fixture fixture;
    fixture.connect();

    fixture.broker().setPublishAccepted(false);
    const std::uint32_t before = fixture.service.stats().publishFailures;

    fixture.service.invalidateStatus();
    fixture.service.tick(1000);

    NOTRIX_CHECK(fixture.service.stats().publishFailures > before);
}

// --- MQTT drives the device --------------------------------------------------

NOTRIX_TEST(MqttService, ACommandActuallyChangesTheDevice) {
    // The exit criterion for this phase: usable from MQTT without HTTP.
    Fixture fixture;
    fixture.connect();

    fixture.command("settings", R"({"display":{"brightness":42}})");

    NOTRIX_CHECK_EQ(static_cast<int>(fixture.config.display.brightness), 42);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().commandsHandled), 1);
}

NOTRIX_TEST(MqttService, CommandsAreValidatedExactlyLikeHttp) {
    // Translating into an api::Request rather than reimplementing the handlers
    // is what makes this true by construction.
    Fixture fixture;
    fixture.connect();
    fixture.broker().clear();

    fixture.command("settings", R"({"clock":{"theme":"holographic"}})");

    NOTRIX_CHECK_EQ(fixture.config.clock.theme, std::string("minimal"));

    const std::string reply = fixture.payloadOn(fixture.service.topics().base +
                                                "/result/settings");
    NOTRIX_CHECK(contains(reply, "\"status\":422"));
}

NOTRIX_TEST(MqttService, SuccessAndFailureAreBothAnswered) {
    Fixture fixture;
    fixture.connect();
    fixture.broker().clear();

    fixture.command("notify", R"({"text":"Dinner"})");

    const MqttMessage* reply =
        fixture.broker().lastOn(fixture.service.topics().base + "/result/notify");
    NOTRIX_REQUIRE(reply != nullptr);
    NOTRIX_CHECK(contains(reply->payload, "\"status\":201"));
    // A stale success from last week would be actively misleading.
    NOTRIX_CHECK_FALSE(reply->retained);
}

NOTRIX_TEST(MqttService, UnknownCommandsAreReportedNotSilentlyDropped) {
    Fixture fixture;
    fixture.connect();

    fixture.command("explode", "{}");

    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().commandsRejected), 1);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().commandsHandled), 0);

    bool logged = false;
    for (int i = 0; i < fixture.logger.count(); ++i) {
        if (std::string(fixture.logger.at(i).message).find("unknown command") !=
            std::string::npos) {
            logged = true;
        }
    }
    NOTRIX_CHECK(logged);
}

NOTRIX_TEST(MqttService, AChangeMadeOverMqttIsRepublished) {
    Fixture fixture;
    fixture.connect();
    fixture.broker().clear();

    fixture.command("settings", R"({"display":{"brightness":77}})");
    fixture.service.tick(1000);

    NOTRIX_CHECK(contains(fixture.payloadOn(fixture.service.topics().status),
                          "\"brightness\":77"));
}

// --- secrets -----------------------------------------------------------------

NOTRIX_TEST(MqttService, TheBrokerPasswordNeverLeavesTheDevice) {
    // §22. The one place the password may appear is the connect options; a
    // broker republishing retained state is the last place it should surface.
    Fixture fixture;
    fixture.config.mqtt.username = "notrix";
    fixture.config.mqtt.password = "hunter2-do-not-leak";

    MqttService::DeviceState state;
    state.activeAppId = "clock";
    fixture.service.setDeviceState(state);

    fixture.connect();
    fixture.command("settings", R"({"display":{"brightness":5}})");
    fixture.service.tick(1000);

    // It did reach the transport, or the device could not reconnect.
    NOTRIX_CHECK_EQ(fixture.broker().lastConnectOptions().password,
                    std::string("hunter2-do-not-leak"));

    for (const MqttMessage& message : fixture.broker().published()) {
        NOTRIX_CHECK_FALSE(contains(message.payload, "hunter2-do-not-leak"));
    }
    for (int i = 0; i < fixture.logger.count(); ++i) {
        NOTRIX_CHECK_FALSE(
            contains(std::string(fixture.logger.at(i).message), "hunter2-do-not-leak"));
    }
}

NOTRIX_TEST(MqttService, ReconfiguringPointsAtTheNewBroker) {
    Fixture fixture;
    fixture.connect();
    NOTRIX_CHECK_EQ(fixture.broker().lastConnectOptions().host, std::string("broker.local"));

    fixture.config.mqtt.host = "other.local";
    fixture.service.configure();
    fixture.service.tick(10000);

    NOTRIX_CHECK_EQ(fixture.broker().lastConnectOptions().host, std::string("other.local"));
}

NOTRIX_TEST(MqttService, ReconfiguringWithNoChangeDoesNotChurnTheConnection) {
    Fixture fixture;
    fixture.connect();
    const std::uint32_t attempts = fixture.service.stats().connectAttempts;

    for (int i = 0; i < 5; ++i) {
        fixture.service.configure();
        fixture.service.tick(static_cast<std::uint64_t>(i + 1) * 1000u);
    }

    NOTRIX_CHECK_EQ(static_cast<int>(fixture.service.stats().connectAttempts),
                    static_cast<int>(attempts));
    NOTRIX_CHECK_EQ(stateCode(fixture.service.state()), stateCode(MqttState::Connected));
}

NOTRIX_TEST(MqttService, SwitchingMqttOffDisconnects) {
    Fixture fixture;
    fixture.connect();
    fixture.broker().clear();

    fixture.config.mqtt.enabled = false;
    fixture.service.configure();

    NOTRIX_CHECK_EQ(stateCode(fixture.service.state()), stateCode(MqttState::Disabled));
    NOTRIX_CHECK_EQ(fixture.payloadOn(fixture.service.topics().availability),
                    std::string("offline"));
}

// --- button events -----------------------------------------------------------

NOTRIX_TEST(MqttService, ButtonEventsArePublishedButNotRetained) {
    Fixture fixture;
    fixture.connect();

    fixture.service.publishButton("appNext", 3, false);

    const MqttMessage* event = fixture.broker().lastOn(fixture.service.topics().button);
    NOTRIX_REQUIRE(event != nullptr);
    NOTRIX_CHECK(contains(event->payload, "\"action\":\"appNext\""));
    NOTRIX_CHECK(contains(event->payload, "\"repeat\":3"));
    // A retained button press would replay every time something subscribed.
    NOTRIX_CHECK_FALSE(event->retained);
}

NOTRIX_TEST(MqttService, ButtonEventsWhileDisconnectedAreDroppedNotBuffered) {
    Fixture fixture;
    fixture.broker().setReachable(false);
    fixture.service.configure();
    fixture.service.tick(0);

    fixture.service.publishButton("appNext", 1, false);

    NOTRIX_CHECK(fixture.broker().published().empty());
}
