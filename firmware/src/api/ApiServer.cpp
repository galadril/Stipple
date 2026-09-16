// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/api/ApiServer.h"

#include <vector>

#include "notrix/api/JsonWriter.h"
#include "notrix/app/AppRegistry.h"
#include "notrix/asset/IconStore.h"
#include "notrix/app/Carousel.h"
#include "notrix/apps/ClockApp.h"
#include "notrix/config/Config.h"
#include "notrix/core/Version.h"
#include "notrix/json/Json.h"
#include "notrix/notify/Notifications.h"
#include "notrix/platform/PlatformServices.h"

namespace notrix {
namespace api {
namespace {

/// A parsed request body, owning its token storage.
///
/// Tokens are heap-allocated because the budget is configurable, but they are
/// bounded by ApiOptions and freed as soon as the request is answered. This is
/// request-handling, not the render path, so the allocation rule that governs
/// rendering does not apply here.
class Body {
public:
    Body(std::string_view text, int maxTokens, std::size_t maxBytes)
        : tokens_(static_cast<std::size_t>(maxTokens > 0 ? maxTokens : 1)),
          document_(tokens_.data(), static_cast<int>(tokens_.size())) {
        json::Limits limits;
        limits.maxInputBytes = maxBytes;
        error_ = document_.parse(text, limits);
    }

    bool valid() const { return error_ == json::Error::None; }
    const char* errorText() const { return json::describe(error_); }
    json::Value root() const { return document_.root(); }

private:
    std::vector<json::Token> tokens_;
    json::Document document_;
    json::Error error_ = json::Error::None;
};

void writeApp(JsonWriter& writer, const app::App& entry, int position) {
    writer.beginObject()
        .member("id", entry.id)
        .member("name", entry.name)
        .member("enabled", entry.enabled)
        .member("position", position)
        .member("durationSeconds", entry.durationSeconds)
        .member("source", app::appSourceName(entry.source));
    // Stored scenes are validated on the way in, so embedding them verbatim
    // avoids a needless parse-and-reserialise round trip.
    writer.rawMember("scene", entry.sceneJson);
    writer.endObject();
}

void writeNotification(JsonWriter& writer, const notify::Notification& notification) {
    writer.beginObject()
        .member("id", notification.id)
        .member("text", notification.text)
        .member("priority", notify::priorityName(notification.priority))
        .member("durationSeconds", notification.durationSeconds)
        .member("hold", notification.hold)
        .member("dismissible", notification.dismissible)
        .endObject();
}

void writeSettings(JsonWriter& writer, const config::Config& settings) {
    writer.beginObject()
        .member("schemaVersion", settings.schemaVersion)
        .member("deviceName", settings.deviceName)
        .key("display")
        .beginObject()
        .member("brightness", static_cast<int>(settings.display.brightness))
        .member("power", settings.display.power)
        .endObject()
        .key("audio")
        .beginObject()
        .member("volumePercent", static_cast<int>(settings.audio.volumePercent))
        .endObject()
        .key("apps")
        .beginObject()
        .member("defaultDurationSeconds", settings.apps.defaultDurationSeconds)
        .member("transitions", settings.apps.transitions)
        .endObject()
        .key("clock")
        .beginObject()
        .member("twentyFourHour", settings.clock.twentyFourHour)
        .member("utcOffsetSeconds", settings.clock.utcOffsetSeconds)
        .member("theme", settings.clock.theme)
        .member("leadingZero", settings.clock.leadingZero)
        .member("showAmPm", settings.clock.showAmPm);

    char hex[8];
    formatHexColor(fromPacked(settings.clock.color), hex);
    writer.member("color", hex);
    formatHexColor(fromPacked(settings.clock.accentColor), hex);
    writer.member("accentColor", hex);
    formatHexColor(fromPacked(settings.clock.dateColor), hex);
    writer.member("dateColor", hex);

    writer.member("dateOrder", settings.clock.dateOrder)
        .member("dateSeparator", settings.clock.dateSeparator)
        .member("dateYear", settings.clock.dateYear)
        .member("blinkPeriodMillis", static_cast<int>(settings.clock.blinkPeriodMillis))
        .endObject()
        .endObject();
}

}  // namespace

bool ApiServer::authorised(const Request& request) const {
    if (options_.authToken.empty()) {
        return true;
    }
    return request.authToken == options_.authToken;
}

Response ApiServer::handle(const Request& request, std::uint64_t nowMillis) {
    // Size is checked before anything looks at the body, so an oversized
    // payload costs a length comparison rather than a parse.
    if (request.body.size() > options_.maxBodyBytes) {
        return payloadTooLarge();
    }

    if (request.method == Method::Unknown) {
        return badRequest("unsupported HTTP method");
    }

    const RouteMatch route = matchRoute(request.path);
    if (route.resource == Resource::Unknown) {
        // A request under /api/ but outside /api/v1/ is asking for an API
        // version this device does not serve. Naming the one it does serve
        // turns a dead end into something the caller can act on.
        const std::string versionedPrefix = std::string(kApiV1Prefix) + "/";
        const bool underApi = request.path.rfind("/api/", 0) == 0;
        const bool underCurrentVersion =
            request.path == kApiV1Prefix || request.path.rfind(versionedPrefix, 0) == 0;

        if (underApi && !underCurrentVersion) {
            return notFound("unknown API version; this device serves " +
                            std::string(kApiV1Prefix) + " only");
        }
        return notFound("no such endpoint");
    }

    // Authentication is checked after routing so an unauthenticated caller
    // cannot use the 401/404 difference to enumerate valid endpoints.
    if (!authorised(request)) {
        return unauthorized();
    }

    switch (route.resource) {
        case Resource::Device: return handleDevice(request);
        case Resource::Health: return handleHealth(request, nowMillis);
        case Resource::Version: return handleVersion(request);
        case Resource::Diagnostics: return handleDiagnostics(request, nowMillis);
        case Resource::AppCollection: return handleAppCollection(request, nowMillis);
        case Resource::AppItem: return handleAppItem(request, route.id, nowMillis);
        case Resource::AppActivate: return handleAppActivate(request, route.id, nowMillis);
        case Resource::NotificationCollection:
            return handleNotificationCollection(request, nowMillis);
        case Resource::NotificationItem:
            return handleNotificationItem(request, route.id, nowMillis);
        case Resource::AssetCollection: return handleAssetCollection(request);
        case Resource::AssetItem: return handleAssetItem(request, route.id);
        case Resource::Settings: return handleSettings(request);
        case Resource::SystemReboot: return handleReboot(request);
        case Resource::Unknown: break;
    }
    return notFound("no such endpoint");
}

// --- device information ------------------------------------------------------

Response ApiServer::handleDevice(const Request& request) {
    if (request.method != Method::Get) {
        return methodNotAllowed();
    }

    JsonWriter writer;
    writer.beginObject();
    writer.member("name", context_.config != nullptr ? context_.config->deviceName : "notrix");
    writer.member("platform",
                  context_.platform != nullptr ? context_.platform->name() : "unknown");
    writer.member("version", kVersion);
    writer.member("apiVersion", kApiVersion);

    writer.key("display").beginObject();
    writer.member("width", Framebuffer::kWidth);
    writer.member("height", Framebuffer::kHeight);
    if (context_.platform != nullptr) {
        writer.member("brightness",
                      static_cast<int>(context_.platform->display().brightness()));
        writer.member("minimumFrameIntervalMillis",
                      context_.platform->display().minimumFrameIntervalMillis());
    }
    writer.endObject();

    writer.key("network");
    if (context_.platform != nullptr && context_.platform->network() != nullptr) {
        const platform::NetworkStatus status = context_.platform->network()->status();
        writer.beginObject()
            .member("connected", status.connected)
            .member("rssiDbm", status.rssiDbm)
            .member("ipv4", status.ipv4)
            .member("hostname", status.hostname)
            .endObject();
    } else {
        // Null rather than a fabricated "disconnected": this platform has no
        // network interface at all, which is different from having one that is
        // down (ADR 0013).
        writer.nullValue();
    }

    writer.endObject();
    return ok(writer.take());
}

Response ApiServer::handleHealth(const Request& request, std::uint64_t nowMillis) {
    if (request.method != Method::Get) {
        return methodNotAllowed();
    }

    JsonWriter writer;
    writer.beginObject();
    writer.member("status", "ok");

    if (context_.platform != nullptr) {
        writer.member("uptimeMillis",
                      static_cast<std::int64_t>(context_.platform->clock().monotonicMillis()));
        writer.member("wallClockValid", context_.platform->clock().wallClockValid());
    } else {
        writer.member("uptimeMillis", static_cast<std::int64_t>(nowMillis));
    }

    writer.member("apps", context_.apps != nullptr ? context_.apps->count() : 0);
    writer.member("notifications",
                  context_.notifications != nullptr ? context_.notifications->size() : 0);
    writer.endObject();
    return ok(writer.take());
}

Response ApiServer::handleVersion(const Request& request) {
    if (request.method != Method::Get) {
        return methodNotAllowed();
    }

    JsonWriter writer;
    writer.beginObject()
        .member("version", kVersion)
        .member("commit", kBuildCommit)
        .member("api", kApiVersion)
        .member("schema", config::kCurrentSchemaVersion)
        .member("target", context_.platform != nullptr ? context_.platform->name() : "unknown")
        .endObject();
    return ok(writer.take());
}

Response ApiServer::handleDiagnostics(const Request& request, std::uint64_t nowMillis) {
    if (request.method != Method::Get) {
        return methodNotAllowed();
    }

    JsonWriter writer;
    writer.beginObject();
    writer.member("uptimeMillis", static_cast<std::int64_t>(nowMillis));

    if (context_.apps != nullptr) {
        writer.key("apps").beginObject()
            .member("total", context_.apps->count())
            .member("enabled", context_.apps->enabledCount())
            .endObject();
    }

    if (context_.notifications != nullptr) {
        writer.key("notifications").beginObject()
            .member("active", context_.notifications->active() != nullptr ? 1 : 0)
            .member("pending", context_.notifications->pending())
            .member("dropped",
                    static_cast<std::int64_t>(context_.notifications->droppedCount()))
            .endObject();
    }

    if (context_.platform != nullptr) {
        writer.key("input").beginObject()
            .member("droppedEvents",
                    static_cast<std::int64_t>(context_.platform->input().droppedEventCount()))
            .endObject();
    }

    // Never expose secrets through diagnostics (§22): no tokens, no Wi-Fi
    // credentials, not even a redacted placeholder that confirms one exists.
    writer.endObject();
    return ok(writer.take());
}

// --- apps --------------------------------------------------------------------

Response ApiServer::handleAppCollection(const Request& request, std::uint64_t nowMillis) {
    if (context_.apps == nullptr) {
        return serverError("app registry unavailable");
    }

    if (request.method == Method::Get) {
        JsonWriter writer;
        writer.beginObject().key("apps").beginArray();
        for (int i = 0; i < context_.apps->count(); ++i) {
            writeApp(writer, *context_.apps->at(i), i);
        }
        writer.endArray();
        writer.member("count", context_.apps->count());
        writer.endObject();
        return ok(writer.take());
    }

    if (request.method != Method::Post) {
        return methodNotAllowed();
    }

    Body body(request.body, options_.maxJsonTokens, options_.maxBodyBytes);
    if (!body.valid()) {
        return badRequest(std::string("invalid JSON: ") + body.errorText());
    }

    const json::Value root = body.root();
    if (!root.isObject()) {
        return badRequest("body must be a JSON object");
    }

    app::App entry;
    entry.id = root["id"].toString();
    if (entry.id.empty()) {
        return unprocessable("'id' is required");
    }
    entry.name = root["name"].toString(entry.id);
    entry.durationSeconds = static_cast<int>(root["durationSeconds"].toInt(0));
    entry.enabled = root["enabled"].toBool(true);
    entry.source = app::AppSource::Remote;

    // Store the scene as the exact text the parser accepted. Because it came
    // out of a successful parse it is valid by construction, which is what lets
    // GET echo it back verbatim without a reserialise round trip.
    const json::Value scene = root["scene"];
    if (scene.valid()) {
        if (!scene.isObject()) {
            return unprocessable("'scene' must be a JSON object");
        }
        entry.sceneJson = std::string(scene.raw());
    }

    if (context_.apps->find(entry.id) != nullptr) {
        return conflict("an app with that id already exists; use PUT to replace it");
    }

    const std::string id = entry.id;
    switch (context_.apps->put(std::move(entry))) {
        case app::AppRegistry::PutResult::Added:
        case app::AppRegistry::PutResult::Replaced:
            break;
        case app::AppRegistry::PutResult::Full:
            return conflict("app registry is full");
        case app::AppRegistry::PutResult::InvalidId:
            return unprocessable("'id' is empty or too long");
        case app::AppRegistry::PutResult::SceneTooLarge:
            return payloadTooLarge("'scene' exceeds the per-app limit");
    }

    if (context_.carousel != nullptr) {
        context_.carousel->tick(nowMillis);
    }

    JsonWriter writer;
    const int position = context_.apps->indexOf(id);
    writeApp(writer, *context_.apps->find(id), position);
    return created(writer.take());
}

Response ApiServer::handleAppItem(const Request& request,
                                  const std::string& id,
                                  std::uint64_t nowMillis) {
    if (context_.apps == nullptr) {
        return serverError("app registry unavailable");
    }

    const app::App* existing = context_.apps->find(id);

    if (request.method == Method::Get) {
        if (existing == nullptr) {
            return notFound("no such app");
        }
        JsonWriter writer;
        writeApp(writer, *existing, context_.apps->indexOf(id));
        return ok(writer.take());
    }

    if (request.method == Method::Delete) {
        if (existing == nullptr) {
            return notFound("no such app");
        }
        if (existing->source == app::AppSource::System) {
            return conflict("system apps cannot be deleted");
        }
        context_.apps->remove(id);
        if (context_.carousel != nullptr) {
            context_.carousel->tick(nowMillis);
        }
        return noContent();
    }

    if (request.method != Method::Put) {
        return methodNotAllowed();
    }

    Body body(request.body, options_.maxJsonTokens, options_.maxBodyBytes);
    if (!body.valid()) {
        return badRequest(std::string("invalid JSON: ") + body.errorText());
    }
    const json::Value root = body.root();
    if (!root.isObject()) {
        return badRequest("body must be a JSON object");
    }

    app::App entry;
    entry.id = id;  // the URL is authoritative; any "id" in the body is ignored
    entry.name = root["name"].toString(existing != nullptr ? existing->name : id);
    entry.durationSeconds = static_cast<int>(
        root["durationSeconds"].toInt(existing != nullptr ? existing->durationSeconds : 0));
    entry.enabled = root["enabled"].toBool(existing == nullptr || existing->enabled);
    entry.source = existing != nullptr ? existing->source : app::AppSource::Remote;

    const json::Value scene = root["scene"];
    if (scene.valid()) {
        if (!scene.isObject()) {
            return unprocessable("'scene' must be a JSON object");
        }
        entry.sceneJson = std::string(scene.raw());
    } else if (existing != nullptr) {
        entry.sceneJson = existing->sceneJson;  // omitting 'scene' keeps the current one
    }

    const bool creating = existing == nullptr;
    switch (context_.apps->put(std::move(entry))) {
        case app::AppRegistry::PutResult::Added:
        case app::AppRegistry::PutResult::Replaced:
            break;
        case app::AppRegistry::PutResult::Full:
            return conflict("app registry is full");
        case app::AppRegistry::PutResult::InvalidId:
            return unprocessable("'id' is empty or too long");
        case app::AppRegistry::PutResult::SceneTooLarge:
            return payloadTooLarge("'scene' exceeds the per-app limit");
    }

    if (context_.carousel != nullptr) {
        context_.carousel->tick(nowMillis);
    }

    JsonWriter writer;
    writeApp(writer, *context_.apps->find(id), context_.apps->indexOf(id));
    return creating ? created(writer.take()) : ok(writer.take());
}

Response ApiServer::handleAppActivate(const Request& request,
                                      const std::string& id,
                                      std::uint64_t nowMillis) {
    if (request.method != Method::Post) {
        return methodNotAllowed();
    }
    if (context_.apps == nullptr || context_.carousel == nullptr) {
        return serverError("carousel unavailable");
    }

    const app::App* entry = context_.apps->find(id);
    if (entry == nullptr) {
        return notFound("no such app");
    }
    if (!entry->enabled) {
        return conflict("app is disabled");
    }

    if (!context_.carousel->activate(id, nowMillis)) {
        return conflict("app could not be activated");
    }

    JsonWriter writer;
    writer.beginObject()
        .member("activeApp", id)
        .member("pinned", context_.carousel->isPinned())
        .endObject();
    return ok(writer.take());
}

// --- notifications -----------------------------------------------------------

Response ApiServer::handleNotificationCollection(const Request& request,
                                                 std::uint64_t nowMillis) {
    if (context_.notifications == nullptr) {
        return serverError("notification queue unavailable");
    }

    if (request.method == Method::Get) {
        JsonWriter writer;
        writer.beginObject();
        writer.key("active");
        if (const notify::Notification* active = context_.notifications->active()) {
            writeNotification(writer, *active);
        } else {
            writer.nullValue();
        }
        writer.member("pending", context_.notifications->pending());
        writer.member("dropped",
                      static_cast<std::int64_t>(context_.notifications->droppedCount()));
        writer.endObject();
        return ok(writer.take());
    }

    if (request.method == Method::Delete) {
        const int removed = context_.notifications->dismissAll(nowMillis);
        JsonWriter writer;
        writer.beginObject().member("dismissed", removed).endObject();
        return ok(writer.take());
    }

    if (request.method != Method::Post) {
        return methodNotAllowed();
    }

    Body body(request.body, options_.maxJsonTokens, options_.maxBodyBytes);
    if (!body.valid()) {
        return badRequest(std::string("invalid JSON: ") + body.errorText());
    }
    const json::Value root = body.root();
    if (!root.isObject()) {
        return badRequest("body must be a JSON object");
    }

    notify::Notification notification;
    notification.id = root["id"].toString();
    notification.text = root["text"].toString();
    notification.priority =
        notify::priorityFromInt(static_cast<int>(root["priority"].toInt(1)));
    notification.durationSeconds = static_cast<int>(root["durationSeconds"].toInt(5));
    notification.hold = root["hold"].toBool(false);
    notification.dismissible = root["dismissible"].toBool(true);
    notification.sound = root["sound"].toString();

    if (notification.text.empty()) {
        return unprocessable("'text' is required");
    }

    switch (context_.notifications->push(std::move(notification), nowMillis)) {
        case notify::NotificationQueue::PushResult::Invalid:
            return unprocessable("notification text or id exceeds its limit");
        case notify::NotificationQueue::PushResult::DroppedLowPriority:
            // 429 is the honest answer: the request was well-formed, the device
            // is simply saturated and said so rather than pretending.
            return error(429, "queue_full",
                         "notification queue is full and nothing queued ranks lower");
        default:
            break;
    }

    JsonWriter writer;
    writer.beginObject();
    writer.key("active");
    if (const notify::Notification* active = context_.notifications->active()) {
        writeNotification(writer, *active);
    } else {
        writer.nullValue();
    }
    writer.member("pending", context_.notifications->pending());
    writer.endObject();
    return created(writer.take());
}

Response ApiServer::handleNotificationItem(const Request& request,
                                           const std::string& id,
                                           std::uint64_t nowMillis) {
    if (context_.notifications == nullptr) {
        return serverError("notification queue unavailable");
    }
    if (request.method != Method::Delete) {
        return methodNotAllowed();
    }

    if (!context_.notifications->dismiss(id, nowMillis)) {
        // Cannot distinguish "absent" from "refuses to be dismissed" without
        // leaking which ids exist, so both answer 404 and the queue's own
        // semantics are documented instead.
        return notFound("no such notification, or it cannot be dismissed");
    }
    return noContent();
}

// --- assets ------------------------------------------------------------------

namespace {

void writeIcon(JsonWriter& writer, const asset::Icon& icon) {
    writer.beginObject()
        .member("id", icon.id)
        .member("width", icon.width)
        .member("height", icon.height)
        .member("frames", icon.frameCount)
        .member("frameMillis", static_cast<std::int64_t>(icon.frameMillis))
        .member("bytes", static_cast<std::int64_t>(icon.byteSize()));
    writer.key("transparent");
    if (icon.hasTransparency) {
        writer.value(static_cast<std::int64_t>(toPacked(icon.transparent)));
    } else {
        writer.nullValue();
    }
    writer.endObject();
}

}  // namespace

Response ApiServer::handleAssetCollection(const Request& request) {
    if (context_.icons == nullptr) {
        return serverError("icon store unavailable");
    }

    if (request.method == Method::Get) {
        JsonWriter writer;
        writer.beginObject().key("assets").beginArray();
        for (int i = 0; i < context_.icons->count(); ++i) {
            writeIcon(writer, *context_.icons->at(i));
        }
        writer.endArray();
        writer.member("count", context_.icons->count());
        // Storage pressure is worth surfacing: an upload that fails because the
        // budget is full should be predictable, not a surprise.
        writer.member("bytesUsed", static_cast<std::int64_t>(context_.icons->bytesUsed()));
        writer.member("bytesFree", static_cast<std::int64_t>(context_.icons->bytesFree()));
        writer.endObject();
        return ok(writer.take());
    }

    if (request.method == Method::Delete) {
        context_.icons->clear();
        return noContent();
    }

    if (request.method != Method::Post) {
        return methodNotAllowed();
    }

    Body body(request.body, options_.maxJsonTokens, options_.maxBodyBytes);
    if (!body.valid()) {
        return badRequest(std::string("invalid JSON: ") + body.errorText());
    }
    const json::Value root = body.root();
    if (!root.isObject()) {
        return badRequest("body must be a JSON object");
    }

    asset::Icon icon;
    icon.id = root["id"].toString();
    icon.width = static_cast<int>(root["width"].toInt(0));
    icon.height = static_cast<int>(root["height"].toInt(0));
    icon.frameMillis = static_cast<std::uint32_t>(root["frameMillis"].toInt(100));

    // Packed 0xRRGGBB only. The converter that produces these is code, not a
    // person, so the several human-friendly colour forms the scene model accepts
    // would be surface for nothing.
    if (const json::Value keyed = root["transparent"]; keyed.isNumber()) {
        const std::int64_t packed = keyed.toInt(-1);
        if (packed < 0 || packed > 0xFFFFFF) {
            return unprocessable("'transparent' must be a packed 0xRRGGBB value");
        }
        icon.hasTransparency = true;
        icon.transparent = fromPacked(static_cast<std::uint32_t>(packed));
    }

    const json::Value frames = root["frames"];
    if (!frames.isArray() || frames.size() == 0) {
        return unprocessable("'frames' must be a non-empty array of pixel arrays");
    }
    icon.frameCount = frames.size();

    // Reject the geometry before reserving anything, so an absurd declared size
    // cannot make us allocate first and fail second.
    if (icon.width <= 0 || icon.height <= 0 ||
        icon.width > asset::IconStore::kMaxDimension ||
        icon.height > asset::IconStore::kMaxDimension ||
        icon.frameCount > asset::IconStore::kMaxFrames) {
        return unprocessable("width, height or frame count is out of range");
    }

    const std::size_t perFrame = icon.pixelsPerFrame();
    icon.pixels.reserve(perFrame * static_cast<std::size_t>(icon.frameCount));

    for (int f = 0; f < icon.frameCount; ++f) {
        const json::Value frame = frames[f];
        if (!frame.isArray() || frame.size() != static_cast<int>(perFrame)) {
            return unprocessable("each frame must hold exactly width x height pixels");
        }
        for (int i = 0; i < frame.size(); ++i) {
            const std::int64_t packed = frame[i].toInt(-1);
            if (packed < 0 || packed > 0xFFFFFF) {
                return unprocessable("pixels must be packed 0xRRGGBB values");
            }
            icon.pixels.push_back(fromPacked(static_cast<std::uint32_t>(packed)));
        }
    }

    const std::string id = icon.id;
    const asset::IconStore::PutResult result = context_.icons->put(std::move(icon));
    switch (result) {
        case asset::IconStore::PutResult::Added:
        case asset::IconStore::PutResult::Replaced:
            break;
        case asset::IconStore::PutResult::InvalidId:
        case asset::IconStore::PutResult::InvalidGeometry:
            return unprocessable(asset::IconStore::describe(result));
        case asset::IconStore::PutResult::TooManyIcons:
        case asset::IconStore::PutResult::BudgetExceeded:
            return conflict(asset::IconStore::describe(result));
    }

    JsonWriter writer;
    writeIcon(writer, *context_.icons->find(id));
    return result == asset::IconStore::PutResult::Added ? created(writer.take())
                                                        : ok(writer.take());
}

Response ApiServer::handleAssetItem(const Request& request, const std::string& id) {
    if (context_.icons == nullptr) {
        return serverError("icon store unavailable");
    }

    const asset::Icon* icon = context_.icons->find(id);

    if (request.method == Method::Get) {
        if (icon == nullptr) {
            return notFound("no such icon");
        }
        JsonWriter writer;
        writeIcon(writer, *icon);
        return ok(writer.take());
    }

    if (request.method != Method::Delete) {
        return methodNotAllowed();
    }
    if (!context_.icons->remove(id)) {
        return notFound("no such icon");
    }
    return noContent();
}

// --- settings ----------------------------------------------------------------

Response ApiServer::handleSettings(const Request& request) {
    if (context_.config == nullptr) {
        return serverError("configuration unavailable");
    }

    if (request.method == Method::Get) {
        JsonWriter writer;
        writeSettings(writer, *context_.config);
        return ok(writer.take());
    }

    if (request.method != Method::Patch) {
        return methodNotAllowed();
    }

    Body body(request.body, options_.maxJsonTokens, options_.maxBodyBytes);
    if (!body.valid()) {
        return badRequest(std::string("invalid JSON: ") + body.errorText());
    }
    const json::Value root = body.root();
    if (!root.isObject()) {
        return badRequest("body must be a JSON object");
    }

    // PATCH: absent fields keep their current value. Applied to a copy so a
    // rejected value cannot leave settings half-updated.
    config::Config updated = *context_.config;

    if (const json::Value name = root["deviceName"]; name.isString()) {
        const std::string text = name.toString();
        if (text.empty() || text.size() > 64) {
            return unprocessable("'deviceName' must be 1-64 characters");
        }
        updated.deviceName = text;
    }

    if (const json::Value display = root["display"]; display.isObject()) {
        if (const json::Value brightness = display["brightness"]; brightness.isNumber()) {
            const std::int64_t value = brightness.toInt(-1);
            if (value < 0 || value > 255) {
                return unprocessable("'display.brightness' must be 0-255");
            }
            updated.display.brightness = static_cast<std::uint8_t>(value);
        }
        if (const json::Value power = display["power"]; power.isBoolean()) {
            updated.display.power = power.toBool(true);
        }
    }

    if (const json::Value audio = root["audio"]; audio.isObject()) {
        if (const json::Value volume = audio["volumePercent"]; volume.isNumber()) {
            const std::int64_t value = volume.toInt(-1);
            if (value < 0 || value > 100) {
                return unprocessable("'audio.volumePercent' must be 0-100");
            }
            updated.audio.volumePercent = static_cast<std::uint8_t>(value);
        }
    }

    if (const json::Value apps = root["apps"]; apps.isObject()) {
        if (const json::Value duration = apps["defaultDurationSeconds"]; duration.isNumber()) {
            const std::int64_t value = duration.toInt(-1);
            if (value < 1 || value > 3600) {
                return unprocessable("'apps.defaultDurationSeconds' must be 1-3600");
            }
            updated.apps.defaultDurationSeconds = static_cast<int>(value);
        }
        if (const json::Value transitions = apps["transitions"]; transitions.isBoolean()) {
            updated.apps.transitions = transitions.toBool(true);
        }
    }

    if (const json::Value clock = root["clock"]; clock.isObject()) {
        if (const json::Value twentyFour = clock["twentyFourHour"]; twentyFour.isBoolean()) {
            updated.clock.twentyFourHour = twentyFour.toBool(true);
        }
        if (const json::Value theme = clock["theme"]; theme.isString()) {
            // Only accept names that round-trip. Falling back silently would
            // leave a client believing it had selected a face it had not.
            const std::string name = theme.toString();
            if (apps::clockThemeName(apps::clockThemeFromName(name)) != name) {
                return unprocessable("'clock.theme' is not a known clock face");
            }
            updated.clock.theme = name;
        }
        if (const json::Value offset = clock["utcOffsetSeconds"]; offset.isNumber()) {
            const std::int64_t value = offset.toInt(0);
            if (value < -12 * 3600 || value > 14 * 3600) {
                return unprocessable("'clock.utcOffsetSeconds' is outside any real time zone");
            }
            updated.clock.utcOffsetSeconds = static_cast<int>(value);
        }
        if (const json::Value value = clock["leadingZero"]; value.isBoolean()) {
            updated.clock.leadingZero = value.toBool(true);
        }
        if (const json::Value value = clock["showAmPm"]; value.isBoolean()) {
            updated.clock.showAmPm = value.toBool(false);
        }

        // Same contract as `theme`: only names that round-trip are accepted, so
        // a client is never left believing it selected something it did not.
        struct NameField {
            const char* key;
            std::string* target;
            std::string (*canonical)(const std::string&);
            const char* complaint;
        };
        const NameField nameFields[] = {
            {"dateOrder", &updated.clock.dateOrder,
             [](const std::string& n) {
                 return std::string(apps::dateOrderName(apps::dateOrderFromName(n)));
             },
             "'clock.dateOrder' must be dayMonthYear, monthDayYear or yearMonthDay"},
            {"dateSeparator", &updated.clock.dateSeparator,
             [](const std::string& n) {
                 return std::string(apps::dateSeparatorName(apps::dateSeparatorFromName(n)));
             },
             "'clock.dateSeparator' must be dot, slash or dash"},
            {"dateYear", &updated.clock.dateYear,
             [](const std::string& n) {
                 return std::string(apps::dateYearName(apps::dateYearFromName(n)));
             },
             "'clock.dateYear' must be none, twoDigit or fourDigit"},
        };
        for (const NameField& field : nameFields) {
            const json::Value value = clock[field.key];
            if (!value.isString()) {
                continue;
            }
            const std::string name = value.toString();
            if (field.canonical(name) != name) {
                return unprocessable(field.complaint);
            }
            *field.target = name;
        }

        const struct {
            const char* key;
            std::uint32_t* target;
            const char* complaint;
        } colorFields[] = {
            {"color", &updated.clock.color, "'clock.color' must be #RRGGBB"},
            {"accentColor", &updated.clock.accentColor, "'clock.accentColor' must be #RRGGBB"},
            {"dateColor", &updated.clock.dateColor, "'clock.dateColor' must be #RRGGBB"},
        };
        for (const auto& field : colorFields) {
            const json::Value value = clock[field.key];
            if (!value.isString()) {
                continue;
            }
            Rgb parsedColor;
            if (!parseHexColor(value.raw(), parsedColor)) {
                return unprocessable(field.complaint);
            }
            *field.target = toPacked(parsedColor);
        }

        if (const json::Value blink = clock["blinkPeriodMillis"]; blink.isNumber()) {
            const std::int64_t value = blink.toInt(-1);
            // 0 is meaningful — it holds the colon lit rather than blinking.
            if (value != 0 && (value < 100 || value > 60000)) {
                return unprocessable(
                    "'clock.blinkPeriodMillis' must be 0 to hold the colon lit, or 100-60000");
            }
            updated.clock.blinkPeriodMillis = static_cast<std::uint32_t>(value);
        }
    }

    *context_.config = updated;

    if (context_.configStore != nullptr && !context_.configStore->save(updated)) {
        return serverError("settings applied but could not be saved");
    }
    if (context_.platform != nullptr) {
        context_.platform->display().setBrightness(updated.display.brightness);
        if (context_.platform->audio() != nullptr) {
            context_.platform->audio()->setVolume(
                config::volumeToByte(updated.audio.volumePercent));
        }
    }

    JsonWriter writer;
    writeSettings(writer, *context_.config);
    return ok(writer.take());
}

Response ApiServer::handleReboot(const Request& request) {
    if (request.method != Method::Post) {
        return methodNotAllowed();
    }
    if (context_.platform == nullptr || context_.platform->rebooter() == nullptr) {
        // 501, not 500: the request was fine, this build simply cannot do it.
        return error(501, "not_supported", "this platform cannot reboot itself");
    }

    context_.platform->rebooter()->reboot();

    JsonWriter writer;
    writer.beginObject().member("status", "rebooting").endObject();

    Response response = ok(writer.take());
    response.status = 202;  // accepted: the reboot happens after we reply
    return response;
}

}  // namespace api
}  // namespace notrix
