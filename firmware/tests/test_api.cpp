// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/api/ApiServer.h"

#include <string>

#include "notrix/api/JsonWriter.h"
#include "notrix/app/Carousel.h"
#include "notrix/config/Config.h"
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
struct Fixture {
    SimulatorPlatform platform;
    AppRegistry apps;
    Carousel carousel{apps};
    NotificationQueue notifications;
    Config config;
    ConfigStore configStore{platform.storage()};
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
