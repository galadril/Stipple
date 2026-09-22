// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/api/ApiServer.h"

#include <string>
#include <vector>

#include "notrix/api/JsonWriter.h"
#include "notrix/core/Base64.h"
#include "notrix/app/Carousel.h"
#include "notrix/config/Config.h"
#include "notrix/graphics/Framebuffer.h"
#include "notrix/json/Json.h"
#include "notrix/notify/Notifications.h"
#include "notrix/platform/simulator/SimulatorPlatform.h"
#include "support/TestFramework.h"

using notrix::api::ApiContext;
using notrix::api::ApiOptions;
using notrix::api::ApiServer;
using notrix::api::JsonWriter;
using notrix::api::matchRoute;
using notrix::api::Method;
using notrix::api::methodFromName;
using notrix::api::Request;
using notrix::api::Resource;
using notrix::api::Response;
using notrix::app::AppRegistry;
using notrix::app::Carousel;
using notrix::config::Config;
using notrix::config::ConfigStore;
using notrix::notify::NotificationQueue;
using notrix::platform::simulator::SimulatorPlatform;

namespace {

/// Owns a whole device's worth of state plus the server in front of it.
/// Records what the input endpoint injected, so a test can assert the events
/// that would have reached the mapper rather than their side effects.
struct RecordingInput : notrix::platform::IInputSink {
    std::vector<notrix::platform::InputEvent> events;
    void inject(const notrix::platform::InputEvent& event) override {
        events.push_back(event);
    }
};

struct Fixture {
    SimulatorPlatform platform;
    AppRegistry apps;
    Carousel carousel{apps};
    NotificationQueue notifications;
    Config config;
    ConfigStore configStore{platform.storage()};
    notrix::Framebuffer framebuffer;
    RecordingInput input;
    ApiServer server;

    explicit Fixture(ApiOptions options = ApiOptions{})
        : server(makeContext(), std::move(options)) {}

    ApiContext makeContext() {
        ApiContext context;
        context.apps = &apps;
        context.carousel = &carousel;
        context.notifications = &notifications;
        context.config = &config;
        context.configStore = &configStore;
        context.platform = &platform;
        context.frame = &framebuffer;
        context.input = &input;
        return context;
    }

    Response call(const char* method,
                  const char* path,
                  const std::string& body = {},
                  const std::string& token = {},
                  std::uint64_t nowMillis = 0) {
        Request request;
        request.method = methodFromName(method);
        request.path = path;
        request.body = body;
        request.authToken = token;
        return server.handle(request, nowMillis);
    }

    void addApp(const std::string& id, const std::string& scene = R"({"elements":[]})") {
        notrix::app::App entry;
        entry.id = id;
        entry.name = id;
        entry.sceneJson = scene;
        apps.put(std::move(entry));
    }
};

/// Parse a response body so assertions read as field checks rather than string
/// matching, which would break on harmless formatting changes.
struct Parsed {
    notrix::json::Token tokens[512];
    std::string text;
    notrix::json::Document document{tokens, 512};
    bool ok = false;

    explicit Parsed(std::string body) : text(std::move(body)) {
        ok = document.parse(text) == notrix::json::Error::None;
    }
    notrix::json::Value root() const { return document.root(); }
};

}  // namespace

// --- routing -----------------------------------------------------------------

NOTRIX_TEST(Api, RoutesKnownPaths) {
    NOTRIX_CHECK(matchRoute("/api/v1/device").resource == Resource::Device);
    NOTRIX_CHECK(matchRoute("/api/v1/health").resource == Resource::Health);
    NOTRIX_CHECK(matchRoute("/api/v1/apps").resource == Resource::AppCollection);
    NOTRIX_CHECK(matchRoute("/api/v1/apps/clock").resource == Resource::AppItem);
    NOTRIX_CHECK(matchRoute("/api/v1/apps/clock/activate").resource == Resource::AppActivate);
    NOTRIX_CHECK(matchRoute("/api/v1/notifications").resource == Resource::NotificationCollection);
    NOTRIX_CHECK(matchRoute("/api/v1/notifications/x").resource == Resource::NotificationItem);
    NOTRIX_CHECK(matchRoute("/api/v1/settings").resource == Resource::Settings);
    NOTRIX_CHECK(matchRoute("/api/v1/display/frame").resource == Resource::DisplayFrame);
    NOTRIX_CHECK(matchRoute("/api/v1/input").resource == Resource::Input);
    NOTRIX_CHECK(matchRoute("/api/v1/system/reboot").resource == Resource::SystemReboot);
}

NOTRIX_TEST(Api, ExtractsPathIds) {
    NOTRIX_CHECK_EQ(matchRoute("/api/v1/apps/living-room").id, std::string("living-room"));
    NOTRIX_CHECK_EQ(matchRoute("/api/v1/apps/a/activate").id, std::string("a"));
}

NOTRIX_TEST(Api, TrailingSlashIsNotADifferentResource) {
    NOTRIX_CHECK(matchRoute("/api/v1/apps/").resource == Resource::AppCollection);
    NOTRIX_CHECK(matchRoute("//api//v1//apps//").resource == Resource::AppCollection);
}

NOTRIX_TEST(Api, RejectsUnknownPaths) {
    NOTRIX_CHECK(matchRoute("/").resource == Resource::Unknown);
    NOTRIX_CHECK(matchRoute("/api/v2/device").resource == Resource::Unknown);
    NOTRIX_CHECK(matchRoute("/api/v1").resource == Resource::Unknown);
    NOTRIX_CHECK(matchRoute("/api/v1/nope").resource == Resource::Unknown);
    NOTRIX_CHECK(matchRoute("/api/v1/apps/a/b/c").resource == Resource::Unknown);
    NOTRIX_CHECK(matchRoute("/../../etc/passwd").resource == Resource::Unknown);
}

NOTRIX_TEST(Api, UnknownEndpointReturns404) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/nope").status, 404);
}

NOTRIX_TEST(Api, UnservedApiVersionPointsAtTheOneWeServe) {
    // A caller asking for an API version this device does not serve should be
    // told which one it does, rather than getting a dead end.
    Fixture fixture;

    const Response response = fixture.call("GET", "/api/v2/device");
    NOTRIX_CHECK_EQ(response.status, 404);
    NOTRIX_CHECK(response.body.find("/api/v1") != std::string::npos);

    NOTRIX_CHECK(fixture.call("GET", "/api/device").body.find("/api/v1") != std::string::npos);

    // A genuinely unknown path inside the served version keeps the plain
    // message: the version is fine, the endpoint simply does not exist.
    NOTRIX_CHECK(fixture.call("GET", "/api/v1/nope").body.find("no such endpoint") !=
                 std::string::npos);
    NOTRIX_CHECK(fixture.call("GET", "/api/v1/nope").body.find("version") == std::string::npos);
}

NOTRIX_TEST(Api, WrongMethodReturns405) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("DELETE", "/api/v1/device").status, 405);
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/health").status, 405);
    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/system/reboot").status, 405);
}

NOTRIX_TEST(Api, UnsupportedMethodReturns400) {
    Fixture fixture;
    Request request;
    request.method = Method::Unknown;
    request.path = "/api/v1/device";
    NOTRIX_CHECK_EQ(fixture.server.handle(request, 0).status, 400);
}

// --- authentication ----------------------------------------------------------

NOTRIX_TEST(Api, NoTokenConfiguredMeansOpenAccess) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/device").status, 200);
}

NOTRIX_TEST(Api, ConfiguredTokenIsRequired) {
    ApiOptions options;
    options.authToken = "s3cret";
    Fixture fixture(options);

    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/device").status, 401);
    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/device", "", "wrong").status, 401);
    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/device", "", "s3cret").status, 200);
}

NOTRIX_TEST(Api, HealthIsAlsoProtected) {
    // An unauthenticated liveness probe would leak uptime, version and app
    // names to anything on the LAN.
    ApiOptions options;
    options.authToken = "s3cret";
    Fixture fixture(options);

    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/health").status, 401);
}

NOTRIX_TEST(Api, UnauthenticatedCallersCannotEnumerateEndpoints) {
    // Both a real and a bogus path must answer 401, or the status difference
    // becomes a map of the API.
    ApiOptions options;
    options.authToken = "s3cret";
    Fixture fixture(options);

    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/settings").status, 401);
    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/nope").status, 404);
    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/apps/does-not-exist").status, 401);
}

// --- limits ------------------------------------------------------------------

NOTRIX_TEST(Api, OversizedBodyIsRejectedBeforeParsing) {
    ApiOptions options;
    options.maxBodyBytes = 64;
    Fixture fixture(options);

    const std::string huge(200, 'x');
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/apps", huge).status, 413);
}

NOTRIX_TEST(Api, MalformedJsonIsRejected) {
    Fixture fixture;
    const Response response = fixture.call("POST", "/api/v1/apps", "{not json");
    NOTRIX_CHECK_EQ(response.status, 400);

    Parsed parsed(response.body);
    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK(parsed.root()["error"]["code"].stringEquals("bad_request"));
}

NOTRIX_TEST(Api, NonObjectBodyIsRejected) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/apps", "[1,2,3]").status, 400);
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/apps", "\"text\"").status, 400);
}

NOTRIX_TEST(Api, ErrorsShareOneShape) {
    Fixture fixture;
    Parsed parsed(fixture.call("GET", "/api/v1/nope").body);

    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK(parsed.root()["error"].isObject());
    NOTRIX_CHECK(parsed.root()["error"]["code"].isString());
    NOTRIX_CHECK(parsed.root()["error"]["message"].isString());
}

// --- device information ------------------------------------------------------

NOTRIX_TEST(Api, DeviceReportsIdentityAndDisplay) {
    Fixture fixture;
    Parsed parsed(fixture.call("GET", "/api/v1/device").body);

    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK(parsed.root()["platform"].stringEquals("simulator"));
    NOTRIX_CHECK_EQ(parsed.root()["display"]["width"].toInt(), std::int64_t(52));
    NOTRIX_CHECK_EQ(parsed.root()["display"]["height"].toInt(), std::int64_t(16));
    NOTRIX_CHECK_EQ(parsed.root()["apiVersion"].toInt(), std::int64_t(1));
}

NOTRIX_TEST(Api, AbsentCapabilityIsNullNotFabricated) {
    // A platform with no network is different from one whose network is down.
    notrix::platform::simulator::SimulatorCapabilities none;
    none.network = false;

    SimulatorPlatform platform(none);
    ApiContext context;
    context.platform = &platform;
    ApiServer server(context);

    Request request;
    request.method = Method::Get;
    request.path = "/api/v1/device";

    Parsed parsed(server.handle(request, 0).body);
    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK(parsed.root()["network"].isNull());
}

NOTRIX_TEST(Api, HealthReportsCounts) {
    Fixture fixture;
    fixture.addApp("a");
    fixture.addApp("b");

    Parsed parsed(fixture.call("GET", "/api/v1/health").body);
    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK(parsed.root()["status"].stringEquals("ok"));
    NOTRIX_CHECK_EQ(parsed.root()["apps"].toInt(), std::int64_t(2));
}

NOTRIX_TEST(Api, DiagnosticsLeaksNoSecrets) {
    ApiOptions options;
    options.authToken = "super-secret-token";
    Fixture fixture(options);

    const Response response = fixture.call("GET", "/api/v1/diagnostics", "", "super-secret-token");
    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK(response.body.find("super-secret-token") == std::string::npos);
    NOTRIX_CHECK(response.body.find("token") == std::string::npos);
}

// --- apps --------------------------------------------------------------------

NOTRIX_TEST(Api, ListsAppsInDisplayOrder) {
    Fixture fixture;
    fixture.addApp("zulu");
    fixture.addApp("alpha");

    Parsed parsed(fixture.call("GET", "/api/v1/apps").body);
    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK_EQ(parsed.root()["count"].toInt(), std::int64_t(2));
    NOTRIX_CHECK(parsed.root()["apps"][0]["id"].stringEquals("zulu"));
    NOTRIX_CHECK(parsed.root()["apps"][1]["id"].stringEquals("alpha"));
    NOTRIX_CHECK_EQ(parsed.root()["apps"][1]["position"].toInt(), std::int64_t(1));
}

NOTRIX_TEST(Api, CreatesAnApp) {
    Fixture fixture;
    const Response response = fixture.call("POST", "/api/v1/apps",
        R"({"id":"weather","name":"Weather","durationSeconds":9,
            "scene":{"elements":[{"type":"pixel","x":0,"y":0}]}})");

    NOTRIX_CHECK_EQ(response.status, 201);
    NOTRIX_CHECK(fixture.apps.find("weather") != nullptr);
    NOTRIX_CHECK_EQ(fixture.apps.find("weather")->durationSeconds, 9);

    // The stored scene round-trips as JSON rather than as an escaped string.
    Parsed parsed(response.body);
    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK(parsed.root()["scene"]["elements"].isArray());
}

NOTRIX_TEST(Api, CreateRequiresAnId) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/apps", R"({"name":"nameless"})").status, 422);
}

NOTRIX_TEST(Api, CreateRejectsADuplicate) {
    Fixture fixture;
    fixture.addApp("clock");
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/apps", R"({"id":"clock"})").status, 409);
}

NOTRIX_TEST(Api, SceneMustBeAnObject) {
    // Rejecting rather than guessing: a stringified scene is a client bug and
    // silently accepting it would make two clients mean different things.
    Fixture fixture;
    NOTRIX_CHECK_EQ(
        fixture.call("POST", "/api/v1/apps", R"({"id":"a","scene":"{\"elements\":[]}"})").status,
        422);
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/apps", R"({"id":"b","scene":[1,2]})").status,
                    422);
}

NOTRIX_TEST(Api, GetsASingleApp) {
    Fixture fixture;
    fixture.addApp("clock");

    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/apps/clock").status, 200);
    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/apps/missing").status, 404);
}

NOTRIX_TEST(Api, PutReplacesAndKeepsPosition) {
    Fixture fixture;
    fixture.addApp("a");
    fixture.addApp("b");
    fixture.addApp("c");

    const Response response =
        fixture.call("PUT", "/api/v1/apps/b", R"({"name":"renamed","durationSeconds":3})");

    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK_EQ(fixture.apps.indexOf("b"), 1);
    NOTRIX_CHECK_EQ(fixture.apps.find("b")->name, std::string("renamed"));
}

NOTRIX_TEST(Api, PutIgnoresAnIdInTheBody) {
    // The URL is authoritative; otherwise PUT /apps/a could rename itself to b
    // and quietly clobber a different app.
    Fixture fixture;
    fixture.addApp("a");

    fixture.call("PUT", "/api/v1/apps/a", R"({"id":"hijacked","name":"x"})");
    NOTRIX_CHECK(fixture.apps.find("a") != nullptr);
    NOTRIX_CHECK(fixture.apps.find("hijacked") == nullptr);
}

NOTRIX_TEST(Api, PutCreatesWhenAbsent) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("PUT", "/api/v1/apps/new", R"({"name":"New"})").status, 201);
    NOTRIX_CHECK(fixture.apps.find("new") != nullptr);
}

NOTRIX_TEST(Api, PutWithoutSceneKeepsTheExistingOne) {
    Fixture fixture;
    fixture.addApp("a", R"({"elements":[{"type":"pixel","x":1,"y":1}]})");

    fixture.call("PUT", "/api/v1/apps/a", R"({"name":"renamed"})");
    NOTRIX_CHECK(fixture.apps.find("a")->sceneJson.find("pixel") != std::string::npos);
}

NOTRIX_TEST(Api, DeletesAnApp) {
    Fixture fixture;
    fixture.addApp("a");

    NOTRIX_CHECK_EQ(fixture.call("DELETE", "/api/v1/apps/a").status, 204);
    NOTRIX_CHECK(fixture.apps.find("a") == nullptr);
    NOTRIX_CHECK_EQ(fixture.call("DELETE", "/api/v1/apps/a").status, 404);
}

NOTRIX_TEST(Api, SystemAppsCannotBeDeleted) {
    // Something must still be on screen after a bad API call.
    Fixture fixture;
    notrix::app::App builtin;
    builtin.id = "clock";
    builtin.source = notrix::app::AppSource::System;
    fixture.apps.put(std::move(builtin));

    NOTRIX_CHECK_EQ(fixture.call("DELETE", "/api/v1/apps/clock").status, 409);
    NOTRIX_CHECK(fixture.apps.find("clock") != nullptr);
}

NOTRIX_TEST(Api, ActivatesAnApp) {
    Fixture fixture;
    fixture.addApp("a");
    fixture.addApp("b");
    fixture.carousel.tick(0);

    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/apps/b/activate", "", "", 500).status, 200);
    NOTRIX_CHECK_EQ(std::string(fixture.carousel.activeId()), std::string("b"));
}

NOTRIX_TEST(Api, ActivateRejectsUnknownAndDisabled) {
    Fixture fixture;
    fixture.addApp("a");
    fixture.apps.setEnabled("a", false);

    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/apps/missing/activate").status, 404);
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/apps/a/activate").status, 409);
}

// --- notifications -----------------------------------------------------------

NOTRIX_TEST(Api, PostsANotification) {
    Fixture fixture;
    const Response response = fixture.call("POST", "/api/v1/notifications",
        R"({"id":"bell","text":"Doorbell","priority":2,"durationSeconds":7})");

    NOTRIX_CHECK_EQ(response.status, 201);
    NOTRIX_CHECK(fixture.notifications.active() != nullptr);
    NOTRIX_CHECK_EQ(fixture.notifications.active()->text, std::string("Doorbell"));
}

NOTRIX_TEST(Api, NotificationRequiresText) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/notifications", R"({"id":"x"})").status, 422);
}

NOTRIX_TEST(Api, ListsNotificationState) {
    Fixture fixture;
    fixture.call("POST", "/api/v1/notifications", R"({"text":"one"})");

    Parsed parsed(fixture.call("GET", "/api/v1/notifications").body);
    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK(parsed.root()["active"].isObject());
    NOTRIX_CHECK_EQ(parsed.root()["pending"].toInt(), std::int64_t(0));
}

NOTRIX_TEST(Api, EmptyQueueReportsNullActive) {
    Fixture fixture;
    Parsed parsed(fixture.call("GET", "/api/v1/notifications").body);
    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK(parsed.root()["active"].isNull());
}

NOTRIX_TEST(Api, DismissesOneAndAll) {
    Fixture fixture;
    fixture.call("POST", "/api/v1/notifications", R"({"id":"a","text":"one"})");
    fixture.call("POST", "/api/v1/notifications", R"({"id":"b","text":"two"})");

    NOTRIX_CHECK_EQ(fixture.call("DELETE", "/api/v1/notifications/a").status, 204);
    NOTRIX_CHECK_EQ(fixture.call("DELETE", "/api/v1/notifications/nope").status, 404);
    NOTRIX_CHECK_EQ(fixture.call("DELETE", "/api/v1/notifications").status, 200);
    NOTRIX_CHECK_EQ(fixture.notifications.size(), 0);
}

NOTRIX_TEST(Api, SaturatedQueueAnswers429) {
    // Well-formed request, device simply full. Saying so beats pretending.
    Fixture fixture;
    fixture.call("POST", "/api/v1/notifications",
                 R"({"id":"hold","text":"held","priority":3,"durationSeconds":600})");
    for (int i = 0; i < NotificationQueue::kMaxQueued; ++i) {
        fixture.call("POST", "/api/v1/notifications",
                     R"({"text":"filler","priority":1})");
    }

    const Response response =
        fixture.call("POST", "/api/v1/notifications", R"({"text":"late","priority":0})");
    NOTRIX_CHECK_EQ(response.status, 429);
}

// --- settings ----------------------------------------------------------------

NOTRIX_TEST(Api, ReadsSettings) {
    Fixture fixture;
    Parsed parsed(fixture.call("GET", "/api/v1/settings").body);

    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK_EQ(parsed.root()["display"]["brightness"].toInt(), std::int64_t(128));
    NOTRIX_CHECK(parsed.root()["deviceName"].stringEquals("notrix"));
}

NOTRIX_TEST(Api, PatchUpdatesOnlyWhatIsSupplied) {
    Fixture fixture;
    fixture.config.deviceName = "kitchen";

    const Response response =
        fixture.call("PATCH", "/api/v1/settings", R"({"display":{"brightness":200}})");

    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.config.display.brightness), 200);
    NOTRIX_CHECK_EQ(fixture.config.deviceName, std::string("kitchen"));  // untouched
}

NOTRIX_TEST(Api, PatchPersistsAndAppliesBrightness) {
    Fixture fixture;
    fixture.call("PATCH", "/api/v1/settings", R"({"display":{"brightness":64}})");

    NOTRIX_CHECK_EQ(static_cast<int>(fixture.platform.display().brightness()), 64);

    Config reloaded;
    fixture.configStore.load(reloaded);
    NOTRIX_CHECK_EQ(static_cast<int>(reloaded.display.brightness), 64);
}

NOTRIX_TEST(Api, PatchRejectsOutOfRangeValues) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"display":{"brightness":999}})").status, 422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"apps":{"defaultDurationSeconds":0}})")
            .status,
        422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"clock":{"utcOffsetSeconds":999999}})")
            .status,
        422);
    NOTRIX_CHECK_EQ(fixture.call("PATCH", "/api/v1/settings", R"({"deviceName":""})").status, 422);
}

NOTRIX_TEST(Api, ReportsClockStyleAsReadableText) {
    // Colours go out as #RRGGBB rather than integers: a settings UI can bind a
    // colour input to it directly, and a human reading the response can tell
    // what it is.
    Fixture fixture;
    fixture.config.clock.accentColor = 0x00BEFFu;
    Parsed parsed(fixture.call("GET", "/api/v1/settings").body);

    NOTRIX_CHECK(parsed.ok);
    const auto clock = parsed.root()["clock"];
    NOTRIX_CHECK(clock["accentColor"].stringEquals("#00BEFF"));
    NOTRIX_CHECK(clock["dateOrder"].stringEquals("dayMonthYear"));
    NOTRIX_CHECK(clock["dateYear"].stringEquals("none"));
    NOTRIX_CHECK_EQ(clock["blinkPeriodMillis"].toInt(), std::int64_t(1000));
}

NOTRIX_TEST(Api, PatchAcceptsClockStyle) {
    Fixture fixture;
    const Response response = fixture.call("PATCH", "/api/v1/settings",
                                           R"({"clock":{"color":"#FF8800",
                                                        "dateOrder":"monthDayYear",
                                                        "dateSeparator":"slash",
                                                        "dateYear":"fourDigit",
                                                        "leadingZero":false,
                                                        "blinkPeriodMillis":0}})");

    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.config.clock.color), 0xFF8800);
    NOTRIX_CHECK_EQ(fixture.config.clock.dateOrder, std::string("monthDayYear"));
    NOTRIX_CHECK_EQ(fixture.config.clock.dateSeparator, std::string("slash"));
    NOTRIX_CHECK_EQ(fixture.config.clock.dateYear, std::string("fourDigit"));
    NOTRIX_CHECK_FALSE(fixture.config.clock.leadingZero);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.config.clock.blinkPeriodMillis), 0);
}

NOTRIX_TEST(Api, PatchRejectsUnknownClockNames) {
    // Silently falling back would leave a client believing it had selected
    // something it had not.
    Fixture fixture;
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"clock":{"dateOrder":"stardate"}})").status,
        422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"clock":{"dateSeparator":"comma"}})").status,
        422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"clock":{"dateYear":"roman"}})").status, 422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"clock":{"theme":"holographic"}})").status,
        422);
}

NOTRIX_TEST(Api, PatchRejectsMalformedColours) {
    Fixture fixture;
    const char* bad[] = {R"({"clock":{"color":"blue"}})", R"({"clock":{"color":"#FFF"}})",
                         R"({"clock":{"color":"#GGGGGG"}})",
                         R"({"clock":{"accentColor":"#1234567"}})"};
    for (const char* body : bad) {
        NOTRIX_CHECK_EQ(fixture.call("PATCH", "/api/v1/settings", body).status, 422);
    }
    // ...and the default survived every rejection.
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.config.clock.color), 0xFFFFFF);
}

NOTRIX_TEST(Api, PatchAcceptsZeroBlinkButNotAStrobe) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"clock":{"blinkPeriodMillis":0}})").status,
        200);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"clock":{"blinkPeriodMillis":5}})").status,
        422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"clock":{"blinkPeriodMillis":99999}})")
            .status,
        422);
}

NOTRIX_TEST(Api, SettingsSurviveAGetPatchGetCycle) {
    // Whatever GET reports must be acceptable to PATCH. If the two ever disagree
    // a settings page will start refusing values it just displayed.
    Fixture fixture;
    const std::string first = fixture.call("GET", "/api/v1/settings").body;
    Parsed parsed(first);
    NOTRIX_CHECK(parsed.ok);

    JsonWriter writer;
    writer.beginObject().key("clock").beginObject();
    writer.member("theme", parsed.root()["clock"]["theme"].toString());
    writer.member("color", parsed.root()["clock"]["color"].toString());
    writer.member("accentColor", parsed.root()["clock"]["accentColor"].toString());
    writer.member("dateColor", parsed.root()["clock"]["dateColor"].toString());
    writer.member("dateOrder", parsed.root()["clock"]["dateOrder"].toString());
    writer.member("dateSeparator", parsed.root()["clock"]["dateSeparator"].toString());
    writer.member("dateYear", parsed.root()["clock"]["dateYear"].toString());
    writer.endObject().endObject();

    NOTRIX_CHECK_EQ(fixture.call("PATCH", "/api/v1/settings", writer.str()).status, 200);
    NOTRIX_CHECK_EQ(fixture.call("GET", "/api/v1/settings").body, first);
}

NOTRIX_TEST(Api, TheMqttPasswordIsWriteOnly) {
    // §22. A settings page needs to know whether a password is set; it must
    // never be able to read one back, and must not be handed a masked
    // placeholder it might helpfully save again.
    Fixture fixture;
    fixture.config.mqtt.password = "hunter2-do-not-leak";

    const std::string body = fixture.call("GET", "/api/v1/settings").body;

    NOTRIX_CHECK(body.find("hunter2-do-not-leak") == std::string::npos);
    NOTRIX_CHECK(body.find("\"passwordSet\":true") != std::string::npos);
    NOTRIX_CHECK(body.find("\"password\"") == std::string::npos);
}

NOTRIX_TEST(Api, TheMqttPasswordCanBeSetAndCleared) {
    Fixture fixture;

    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"mqtt":{"password":"secret"}})").status, 200);
    NOTRIX_CHECK_EQ(fixture.config.mqtt.password, std::string("secret"));

    // An empty string is the only way to remove a stored credential.
    fixture.call("PATCH", "/api/v1/settings", R"({"mqtt":{"password":""}})");
    NOTRIX_CHECK(fixture.config.mqtt.password.empty());
}

NOTRIX_TEST(Api, TheAccessPasswordIsWriteOnly) {
    // The same rule as the MQTT password, and for a sharper reason: backups
    // are taken from this API, so anything returned here ends up in a file
    // in somebody's downloads folder.
    Fixture fixture;
    fixture.config.web.username = "mark";
    fixture.config.web.password = "hunter2-do-not-leak";

    const std::string body = fixture.call("GET", "/api/v1/settings").body;

    NOTRIX_CHECK(body.find("hunter2-do-not-leak") == std::string::npos);
    NOTRIX_CHECK(body.find("\"username\":\"mark\"") != std::string::npos);
    NOTRIX_CHECK(body.find("\"passwordSet\":true") != std::string::npos);
}

NOTRIX_TEST(Api, AccessCanBeTurnedOnAndOff) {
    Fixture fixture;

    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings",
                     R"({"web":{"username":"mark","password":"hunter22"}})").status, 200);
    NOTRIX_CHECK_EQ(fixture.config.web.username, std::string("mark"));
    NOTRIX_CHECK_EQ(fixture.config.web.password, std::string("hunter22"));

    // Both cleared together, which is the only way off.
    fixture.call("PATCH", "/api/v1/settings", R"({"web":{"username":"","password":""}})");
    NOTRIX_CHECK(fixture.config.web.username.empty());
    NOTRIX_CHECK(fixture.config.web.password.empty());
}

NOTRIX_TEST(Api, RefusesHalfConfiguredAccess) {
    // The failure mode of getting this wrong is a device nobody can log
    // into, and unlike most settings the page that would fix it is behind
    // the thing that broke. So it is refused here rather than half-applied.
    Fixture fixture;

    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"web":{"username":"mark"}})").status, 422);
    NOTRIX_CHECK(fixture.config.web.username.empty());

    // And clearing only the password, leaving a username behind, is the same
    // mistake from the other direction.
    fixture.config.web.username = "mark";
    fixture.config.web.password = "hunter22";
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"web":{"password":""}})").status, 422);
    NOTRIX_CHECK_EQ(fixture.config.web.password, std::string("hunter22"));
}

NOTRIX_TEST(Api, RefusesAUsernameThatCouldNeverBeSent) {
    // A Basic credential is "user:password" split on the first colon, so a
    // username containing one could never come back. Accepting it would
    // store a setting that locks the device permanently.
    Fixture fixture;
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings",
                     R"({"web":{"username":"ma:rk","password":"hunter22"}})").status, 422);
    NOTRIX_CHECK(fixture.config.web.username.empty());
}

NOTRIX_TEST(Api, PatchAcceptsMqttSettings) {
    Fixture fixture;
    const Response response = fixture.call("PATCH", "/api/v1/settings",
                                           R"({"mqtt":{"enabled":true,"host":"broker.local",
                                                       "port":8883,"baseTopic":"home",
                                                       "tls":true,"keepAliveSeconds":45}})");

    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK(fixture.config.mqtt.enabled);
    NOTRIX_CHECK_EQ(fixture.config.mqtt.host, std::string("broker.local"));
    NOTRIX_CHECK_EQ(fixture.config.mqtt.port, 8883);
    NOTRIX_CHECK_EQ(fixture.config.mqtt.baseTopic, std::string("home"));
    NOTRIX_CHECK(fixture.config.mqtt.tls);
}

NOTRIX_TEST(Api, PatchRejectsUnusableMqttSettings) {
    Fixture fixture;

    // A wildcard base topic would make the device publish to a filter, which no
    // broker accepts and which is miserable to diagnose from the other end.
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"mqtt":{"baseTopic":"home/#"}})").status,
        422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"mqtt":{"baseTopic":"a/+/b"}})").status, 422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"mqtt":{"baseTopic":""}})").status, 422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"mqtt":{"port":0}})").status, 422);
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/settings", R"({"mqtt":{"keepAliveSeconds":1}})").status,
        422);
}

NOTRIX_TEST(Api, RejectedPatchLeavesSettingsUntouched) {
    // Applied to a copy, so a bad field cannot half-update the device.
    Fixture fixture;
    fixture.config.deviceName = "before";

    fixture.call("PATCH", "/api/v1/settings",
                 R"({"deviceName":"after","display":{"brightness":999}})");

    NOTRIX_CHECK_EQ(fixture.config.deviceName, std::string("before"));
}

// --- system ------------------------------------------------------------------

NOTRIX_TEST(Api, RebootIsAccepted) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/system/reboot").status, 202);
    NOTRIX_CHECK_EQ(fixture.platform.simulatedRebooter().rebootCount(), 1u);
}

NOTRIX_TEST(Api, RebootReports501WhenUnsupported) {
    notrix::platform::simulator::SimulatorCapabilities none;
    none.rebooter = false;
    SimulatorPlatform platform(none);

    ApiContext context;
    context.platform = &platform;
    ApiServer server(context);

    Request request;
    request.method = Method::Post;
    request.path = "/api/v1/system/reboot";
    NOTRIX_CHECK_EQ(server.handle(request, 0).status, 501);
}

// --- JSON writer -------------------------------------------------------------

NOTRIX_TEST(Api, JsonWriterEscapesAndNests) {
    JsonWriter writer;
    writer.beginObject()
        .member("quote", "he said \"hi\"")
        .member("newline", "a\nb")
        .member("count", 3)
        .member("flag", true)
        .key("list")
        .beginArray()
        .value("x")
        .value(std::int64_t(7))
        .endArray()
        .endObject();

    Parsed parsed(writer.take());
    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK_EQ(parsed.root()["quote"].toString(), std::string("he said \"hi\""));
    NOTRIX_CHECK_EQ(parsed.root()["newline"].toString(), std::string("a\nb"));
    NOTRIX_CHECK_EQ(parsed.root()["list"][1].toInt(), std::int64_t(7));
}

NOTRIX_TEST(Api, JsonWriterHandlesEmptyContainers) {
    JsonWriter writer;
    writer.beginObject().key("empty").beginArray().endArray().key("obj").beginObject().endObject().endObject();

    Parsed parsed(writer.take());
    NOTRIX_CHECK(parsed.ok);
    NOTRIX_CHECK_EQ(parsed.root()["empty"].size(), 0);
}

NOTRIX_TEST(Api, EveryResponseBodyIsValidJson) {
    // Any malformed body would break every client at once, so sweep the surface.
    Fixture fixture;
    fixture.addApp("a");
    fixture.call("POST", "/api/v1/notifications", R"({"id":"n","text":"hi"})");

    const char* paths[] = {
        "/api/v1/device", "/api/v1/health", "/api/v1/version", "/api/v1/diagnostics",
        "/api/v1/apps",   "/api/v1/apps/a", "/api/v1/settings", "/api/v1/notifications",
        "/api/v1/nope",   "/api/v1/apps/missing",
    };

    for (const char* path : paths) {
        const Response response = fixture.call("GET", path);
        if (response.body.empty()) {
            continue;  // 204 carries no body
        }
        Parsed parsed(response.body);
        NOTRIX_CHECK(parsed.ok);
    }
}

// --- live view and injected input --------------------------------------------

NOTRIX_TEST(Api, TheFrameEndpointReturnsTheWholePanel) {
    Fixture fixture;
    fixture.framebuffer.fill(notrix::colors::kBlack);
    fixture.framebuffer.set(0, 0, notrix::rgb(255, 0, 0));
    fixture.framebuffer.set(51, 15, notrix::rgb(0, 0, 255));

    const Response response = fixture.call("GET", "/api/v1/display/frame");
    NOTRIX_CHECK_EQ(response.status, 200);

    // Base64 of 52*16*3 bytes, which must be the whole panel and not a crop.
    const std::size_t expected =
        notrix::base64::encodedSize(notrix::Framebuffer::kByteSize);
    const std::size_t open = response.body.find("\"pixels\":\"");
    NOTRIX_CHECK(open != std::string::npos);
    const std::size_t start = open + 10;
    const std::size_t close = response.body.find('"', start);
    NOTRIX_CHECK_EQ(close - start, expected);

    NOTRIX_CHECK(response.body.find("\"width\":52") != std::string::npos);
    NOTRIX_CHECK(response.body.find("\"height\":16") != std::string::npos);
    NOTRIX_CHECK(response.body.find("rgb888") != std::string::npos);
}

NOTRIX_TEST(Api, TheFrameEndpointIsReadOnly) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/display/frame").status, 405);
}

NOTRIX_TEST(Api, AButtonPressArrivesAsDownAndUp) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(
        fixture.call("POST", "/api/v1/input", R"({"control":"middle"})", {}, 5000).status,
        204);

    NOTRIX_CHECK_EQ(fixture.input.events.size(), std::size_t{2});
    NOTRIX_CHECK(fixture.input.events[0].source == notrix::platform::RawInput::KeyMiddle);
    NOTRIX_CHECK(fixture.input.events[0].phase == notrix::platform::ButtonPhase::Down);
    NOTRIX_CHECK(fixture.input.events[1].phase == notrix::platform::ButtonPhase::Up);
}

NOTRIX_TEST(Api, HoldMillisReachesTheMapperAsARealLongPress) {
    // The whole point of the field: without it a browser could only ever tap,
    // and half the default bindings are long presses.
    Fixture fixture;
    fixture.call("POST", "/api/v1/input", R"({"control":"plus","holdMillis":900})", {}, 1000);

    NOTRIX_CHECK_EQ(fixture.input.events.size(), std::size_t{2});
    NOTRIX_CHECK_EQ(fixture.input.events[1].timestampMillis -
                        fixture.input.events[0].timestampMillis,
                    std::uint64_t{900});
}

NOTRIX_TEST(Api, ADetentIsOneTickNotAPress) {
    Fixture fixture;
    fixture.call("POST", "/api/v1/input", R"({"control":"right"})", {}, 0);

    NOTRIX_CHECK_EQ(fixture.input.events.size(), std::size_t{1});
    NOTRIX_CHECK(fixture.input.events[0].source == notrix::platform::RawInput::RotaryRight);
    NOTRIX_CHECK(fixture.input.events[0].phase == notrix::platform::ButtonPhase::Tick);
}

NOTRIX_TEST(Api, AnUnknownControlIsRefusedRatherThanGuessed) {
    // A web button that silently pressed the wrong control would be worse than
    // one that did nothing at all.
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/input", R"({"control":"wheel"})").status, 422);
    NOTRIX_CHECK_EQ(fixture.call("POST", "/api/v1/input", R"({})").status, 400);
    NOTRIX_CHECK(fixture.input.events.empty());
}

NOTRIX_TEST(Api, AnAbsurdHoldIsRefused) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(
        fixture.call("POST", "/api/v1/input", R"({"control":"plus","holdMillis":99999})").status,
        422);
    NOTRIX_CHECK(fixture.input.events.empty());
}

// --- enabling and disabling apps ---------------------------------------------

NOTRIX_TEST(Api, DisablingAnAppIsAPatchNotAReplace) {
    // The config page has always sent PATCH here. The route only handled GET,
    // DELETE and PUT, so every toggle in the UI answered 405 and the app stayed
    // exactly as it was.
    Fixture fixture;
    fixture.addApp("weather");

    const Response off =
        fixture.call("PATCH", "/api/v1/apps/weather", R"({"enabled":false})");
    NOTRIX_CHECK_EQ(off.status, 200);
    NOTRIX_CHECK_FALSE(fixture.apps.find("weather")->enabled);

    const Response on =
        fixture.call("PATCH", "/api/v1/apps/weather", R"({"enabled":true})");
    NOTRIX_CHECK_EQ(on.status, 200);
    NOTRIX_CHECK(fixture.apps.find("weather")->enabled);
}

NOTRIX_TEST(Api, PatchingLeavesEverythingItDoesNotName) {
    Fixture fixture;
    fixture.addApp("weather", R"({"elements":[]})");
    fixture.apps.find("weather")->name = "Weather";
    fixture.apps.find("weather")->durationSeconds = 12;

    fixture.call("PATCH", "/api/v1/apps/weather", R"({"enabled":false})");

    const notrix::app::App* entry = fixture.apps.find("weather");
    NOTRIX_CHECK_EQ(entry->name, std::string("Weather"));
    NOTRIX_CHECK_EQ(entry->durationSeconds, 12);
    NOTRIX_CHECK_FALSE(entry->sceneJson.empty());
}

NOTRIX_TEST(Api, PatchingSomethingAbsentIsNotFound) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(fixture.call("PATCH", "/api/v1/apps/ghost", R"({"enabled":false})").status,
                    404);
}

NOTRIX_TEST(Api, ASceneCannotBePatchedIn) {
    // Changing what an app *is* is a replace. Allowing it here would make PATCH
    // a second, subtly different way to create apps.
    Fixture fixture;
    fixture.addApp("weather");
    NOTRIX_CHECK_EQ(
        fixture.call("PATCH", "/api/v1/apps/weather", R"({"scene":{"elements":[]}})").status,
        422);
}

NOTRIX_TEST(Api, ReplacingASystemAppKeepsItABuiltin) {
    // A PUT that dropped `builtin` left an entry that existed, was enabled, and
    // rendered nothing - the clock silently replaced by a blank card.
    Fixture fixture;
    notrix::app::App clock;
    clock.id = "clock";
    clock.name = "Clock";
    clock.source = notrix::app::AppSource::System;
    clock.builtin = notrix::app::Builtin::Clock;
    fixture.apps.put(std::move(clock));

    fixture.call("PUT", "/api/v1/apps/clock", R"({"enabled":false})");
    NOTRIX_CHECK(fixture.apps.find("clock")->builtin == notrix::app::Builtin::Clock);

    fixture.call("PATCH", "/api/v1/apps/clock", R"({"enabled":true})");
    NOTRIX_CHECK(fixture.apps.find("clock")->builtin == notrix::app::Builtin::Clock);
    NOTRIX_CHECK(fixture.apps.find("clock")->source == notrix::app::AppSource::System);
}

NOTRIX_TEST(Api, AnAppCanBeMovedByPatchingItsPosition) {
    // Position is a property of the app like any other, so it moves on the
    // same verb rather than needing an endpoint of its own.
    Fixture fixture;
    fixture.addApp("one");
    fixture.addApp("two");
    fixture.addApp("three");

    const Response moved = fixture.call("PATCH", "/api/v1/apps/three", R"({"position":0})");
    NOTRIX_CHECK_EQ(static_cast<int>(moved.status), 200);

    NOTRIX_CHECK_EQ(fixture.apps.at(0)->id, std::string("three"));
    NOTRIX_CHECK_EQ(fixture.apps.at(1)->id, std::string("one"));
    NOTRIX_CHECK_EQ(fixture.apps.at(2)->id, std::string("two"));
}

NOTRIX_TEST(Api, MovingAnAppReportsItsNewPosition) {
    // The reply used to say position 0 whatever happened, which is a lie a client
    // would happily rebuild its list from.
    Fixture fixture;
    fixture.addApp("one");
    fixture.addApp("two");
    fixture.addApp("three");

    const Response moved = fixture.call("PATCH", "/api/v1/apps/one", R"({"position":2})");
    NOTRIX_CHECK_EQ(static_cast<int>(moved.status), 200);
    NOTRIX_CHECK(moved.body.find("\"position\":2") != std::string::npos);
}

NOTRIX_TEST(Api, APositionOutsideTheInstalledAppsIsRefused) {
    Fixture fixture;
    fixture.addApp("one");
    fixture.addApp("two");

    NOTRIX_CHECK_EQ(
        static_cast<int>(fixture.call("PATCH", "/api/v1/apps/one", R"({"position":9})").status), 422);
    NOTRIX_CHECK_EQ(
        static_cast<int>(fixture.call("PATCH", "/api/v1/apps/one", R"({"position":-1})").status), 422);

    // And nothing moved on the way to being refused.
    NOTRIX_CHECK_EQ(fixture.apps.at(0)->id, std::string("one"));
}

// --- reset -------------------------------------------------------------------

NOTRIX_TEST(Api, ResetPutsSettingsBackToDefaults) {
    Fixture fixture;
    fixture.config.display.brightness = 17;
    fixture.config.clock.theme = "calendar";
    fixture.config.apps.defaultDurationSeconds = 44;

    const Response reset = fixture.call("POST", "/api/v1/system/reset");
    NOTRIX_CHECK_EQ(static_cast<int>(reset.status), 200);

    const notrix::config::Config defaults;
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.config.display.brightness),
                    static_cast<int>(defaults.display.brightness));
    NOTRIX_CHECK_EQ(fixture.config.clock.theme, defaults.clock.theme);
    NOTRIX_CHECK_EQ(fixture.config.apps.defaultDurationSeconds,
                    defaults.apps.defaultDurationSeconds);
}

NOTRIX_TEST(Api, ResetKeepsTheDeviceName) {
    // It is how somebody tells one of these from another on the network, it is
    // not a setting that can be "wrong", and losing it means finding the device
    // again before you can fix whatever you were resetting.
    Fixture fixture;
    fixture.config.deviceName = "kitchen";
    fixture.config.display.brightness = 17;

    fixture.call("POST", "/api/v1/system/reset");

    NOTRIX_CHECK_EQ(fixture.config.deviceName, std::string("kitchen"));
}

NOTRIX_TEST(Api, ResetKeepsTheArrangementWithTheApps) {
    // Clearing the stored order cleared only the stored copy: the running
    // device stayed in the user's order until it next rebooted and then
    // silently reverted. A reset that takes effect at an unpredictable point
    // in the future is worse than one that does nothing.
    Fixture fixture;
    notrix::config::AppPreference arranged;
    arranged.id = "battery";
    fixture.config.apps.order.push_back(arranged);
    fixture.config.display.brightness = 17;

    fixture.call("POST", "/api/v1/system/reset");
    NOTRIX_REQUIRE(fixture.config.apps.order.size() == 1);
    NOTRIX_CHECK_EQ(fixture.config.apps.order[0].id, std::string("battery"));

    // And it goes when the apps do, because then there is nothing to arrange.
    fixture.call("POST", "/api/v1/system/reset", R"({"apps":true})");
    NOTRIX_CHECK(fixture.config.apps.order.empty());
}

NOTRIX_TEST(Api, ResetKeepsAppsUnlessAskedOtherwise) {
    // Somebody resetting settings to sort out a display problem should not
    // silently lose the apps an integration spent a week pushing.
    Fixture fixture;
    fixture.addApp("pushed");

    fixture.call("POST", "/api/v1/system/reset");
    NOTRIX_CHECK(fixture.apps.find("pushed") != nullptr);

    fixture.call("POST", "/api/v1/system/reset", R"({"apps":true})");
    NOTRIX_CHECK(fixture.apps.find("pushed") == nullptr);
}

NOTRIX_TEST(Api, ResetIsPersisted) {
    // The running device is on defaults either way; a caller that believes the
    // reset survived a reboot when it did not will be surprised later.
    Fixture fixture;
    fixture.config.display.brightness = 17;
    fixture.call("POST", "/api/v1/system/reset");

    notrix::config::Config stored;
    fixture.configStore.load(stored);
    const notrix::config::Config defaults;
    NOTRIX_CHECK_EQ(static_cast<int>(stored.display.brightness),
                    static_cast<int>(defaults.display.brightness));
}

NOTRIX_TEST(Api, ResetRefusesTheWrongMethod) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.call("GET", "/api/v1/system/reset").status), 405);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.call("DELETE", "/api/v1/system/reset").status), 405);
}

NOTRIX_TEST(Api, SettingsRoundTripThroughABackup) {
    // What the web UI's backup and restore actually do: read the settings
    // document, send the whole thing back, and get the same device state. If
    // any field the API emits is one it will not accept, this fails - which is
    // the only way to notice a write-only or read-only field before a user
    // does.
    Fixture fixture;
    fixture.config.deviceName = "hallway";
    fixture.config.display.brightness = 42;
    fixture.config.display.overlay = "confetti";
    fixture.config.clock.theme = "calendar";
    fixture.config.clock.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    fixture.config.apps.defaultDurationSeconds = 17;
    fixture.config.apps.transition = "dissolve";
    fixture.config.visualizer.style = "meter";
    fixture.config.notifications.sound = "alert";
    fixture.config.clock.tick = true;

    notrix::config::AppPreference arranged;
    arranged.id = "battery";
    arranged.enabled = false;
    arranged.durationSeconds = 9;
    fixture.config.apps.order.push_back(arranged);

    const Response saved = fixture.call("GET", "/api/v1/settings");
    NOTRIX_REQUIRE(saved.status == 200);
    const std::string backup = saved.body;

    // Wipe, then restore from the backup exactly as the page does.
    fixture.call("POST", "/api/v1/system/reset");
    const Response restored = fixture.call("PATCH", "/api/v1/settings", backup);
    NOTRIX_CHECK_EQ(static_cast<int>(restored.status), 200);

    NOTRIX_CHECK_EQ(static_cast<int>(fixture.config.display.brightness), 42);
    NOTRIX_CHECK_EQ(fixture.config.display.overlay, std::string("confetti"));
    NOTRIX_CHECK_EQ(fixture.config.clock.theme, std::string("calendar"));
    NOTRIX_CHECK_EQ(fixture.config.clock.timezone,
                    std::string("CET-1CEST,M3.5.0,M10.5.0/3"));
    NOTRIX_CHECK_EQ(fixture.config.apps.defaultDurationSeconds, 17);
    NOTRIX_CHECK_EQ(fixture.config.apps.transition, std::string("dissolve"));
    NOTRIX_CHECK_EQ(fixture.config.visualizer.style, std::string("meter"));
    NOTRIX_CHECK_EQ(fixture.config.notifications.sound, std::string("alert"));
    NOTRIX_CHECK(fixture.config.clock.tick);

    // The arrangement too. It was persisted to flash and left out of this
    // document, so a backup silently omitted it and a restore could not bring
    // it back - and this test passed anyway until it started looking.
    NOTRIX_REQUIRE(fixture.config.apps.order.size() == 1);
    NOTRIX_CHECK_EQ(fixture.config.apps.order[0].id, std::string("battery"));
    NOTRIX_CHECK_FALSE(fixture.config.apps.order[0].enabled);
    NOTRIX_CHECK_EQ(fixture.config.apps.order[0].durationSeconds, 9);
}

NOTRIX_TEST(Api, AMalformedOrderIsRefusedRatherThanPartlyApplied) {
    Fixture fixture;

    NOTRIX_CHECK_EQ(static_cast<int>(
        fixture.call("PATCH", "/api/v1/settings",
                     R"({"apps":{"order":[{"enabled":true}]}})").status), 422);
    NOTRIX_CHECK_EQ(static_cast<int>(
        fixture.call("PATCH", "/api/v1/settings",
                     R"({"apps":{"order":["clock"]}})").status), 422);
    NOTRIX_CHECK_EQ(static_cast<int>(
        fixture.call("PATCH", "/api/v1/settings",
                     R"({"apps":{"order":[{"id":"a","durationSeconds":99999}]}})").status), 422);

    // Nothing was written on the way to being refused.
    NOTRIX_CHECK(fixture.config.apps.order.empty());
}

NOTRIX_TEST(Api, AButtonCanBeHeldAcrossRequests) {
    // Without this the only thing reachable from outside is a complete press,
    // which makes any two-button gesture untestable except by standing in
    // front of the device - and the rescue gesture is exactly that, on the one
    // path that has to work when nothing else does.
    Fixture fixture;

    NOTRIX_CHECK_EQ(static_cast<int>(
        fixture.call("POST", "/api/v1/input", R"({"control":"minus","phase":"down"})").status),
        204);
    NOTRIX_REQUIRE(fixture.input.events.size() == 1);
    NOTRIX_CHECK(fixture.input.events[0].phase == notrix::platform::ButtonPhase::Down);

    NOTRIX_CHECK_EQ(static_cast<int>(
        fixture.call("POST", "/api/v1/input", R"({"control":"minus","phase":"up"})").status),
        204);
    NOTRIX_REQUIRE(fixture.input.events.size() == 2);
    NOTRIX_CHECK(fixture.input.events[1].phase == notrix::platform::ButtonPhase::Up);
}

NOTRIX_TEST(Api, AnUnknownPhaseIsRefused) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(static_cast<int>(
        fixture.call("POST", "/api/v1/input", R"({"control":"minus","phase":"sideways"})").status),
        422);
    NOTRIX_CHECK(fixture.input.events.empty());
}

NOTRIX_TEST(Api, WithoutAPhaseAPressIsStillCompleteBothWays) {
    // The common case stays one request, because a web UI pressing a button
    // should not have to remember to let go.
    Fixture fixture;
    fixture.call("POST", "/api/v1/input", R"({"control":"plus"})");
    NOTRIX_REQUIRE(fixture.input.events.size() == 2);
    NOTRIX_CHECK(fixture.input.events[0].phase == notrix::platform::ButtonPhase::Down);
    NOTRIX_CHECK(fixture.input.events[1].phase == notrix::platform::ButtonPhase::Up);
}

// --- network -----------------------------------------------------------------

NOTRIX_TEST(Api, NetworkReportsWhatTheDeviceIsOn) {
    Fixture fixture;
    notrix::platform::NetworkStatus status;
    status.connected = true;
    status.ipv4 = "192.168.1.50";
    status.hostname = "notrix";
    status.ssid = "Example Network";
    status.signalKnown = true;
    status.rssiDbm = -51;
    fixture.platform.simulatedNetwork().setStatus(status);

    const Response answer = fixture.call("GET", "/api/v1/network");
    NOTRIX_CHECK_EQ(static_cast<int>(answer.status), 200);
    NOTRIX_CHECK(answer.body.find("\"ssid\":\"Example Network\"") != std::string::npos);
    NOTRIX_CHECK(answer.body.find("\"rssiDbm\":-51") != std::string::npos);
}

NOTRIX_TEST(Api, APlatformThatCannotScanSaysSoRatherThanReturningNothing) {
    // An empty list and "this device cannot look" are different answers, and
    // they are indistinguishable without saying which one this is (ADR 0013).
    Fixture fixture;

    const Response answer = fixture.call("GET", "/api/v1/network");
    NOTRIX_CHECK(answer.body.find("\"canScan\":false") != std::string::npos);
    NOTRIX_CHECK(answer.body.find("\"networks\":[]") != std::string::npos);

    NOTRIX_CHECK_EQ(static_cast<int>(fixture.call("POST", "/api/v1/network/scan").status), 501);
}

NOTRIX_TEST(Api, ScanningIsAcceptedRatherThanWaitedFor) {
    // A scan takes seconds and §16 does not allow waiting for one here, so the
    // reply says it started and the caller asks again.
    Fixture fixture;
    fixture.platform.simulatedNetwork().setScannable(true);

    const Response answer = fixture.call("POST", "/api/v1/network/scan");
    NOTRIX_CHECK_EQ(static_cast<int>(answer.status), 202);
    NOTRIX_CHECK(answer.body.find("scanning") != std::string::npos);
}

NOTRIX_TEST(Api, ScanResultsComeBackThroughTheNetworkResource) {
    Fixture fixture;
    fixture.platform.simulatedNetwork().setScannable(true);

    notrix::platform::WirelessNetwork found;
    found.ssid = "Example Network";
    found.signalDbm = -42;
    found.secured = true;
    found.current = true;
    fixture.platform.simulatedNetwork().setNetworks({found});

    const Response answer = fixture.call("GET", "/api/v1/network");
    NOTRIX_CHECK(answer.body.find("\"canScan\":true") != std::string::npos);
    NOTRIX_CHECK(answer.body.find("\"signalDbm\":-42") != std::string::npos);
    NOTRIX_CHECK(answer.body.find("\"secured\":true") != std::string::npos);
    NOTRIX_CHECK(answer.body.find("\"current\":true") != std::string::npos);
}

NOTRIX_TEST(Api, NetworkRefusesTheWrongMethods) {
    Fixture fixture;
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.call("POST", "/api/v1/network").status), 405);
    NOTRIX_CHECK_EQ(static_cast<int>(fixture.call("GET", "/api/v1/network/scan").status), 405);
}
