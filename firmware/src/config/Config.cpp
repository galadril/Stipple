// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/config/Config.h"

#include "notrix/core/Checksum.h"
#include "notrix/core/Rgb.h"
#include "notrix/json/Json.h"

namespace notrix {
namespace config {
namespace {

std::uint8_t clampToByte(std::int64_t value) noexcept {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<std::uint8_t>(value);
}

int clampDuration(std::int64_t value) noexcept {
    if (value < 1) {
        return 1;
    }
    if (value > 3600) {
        return 3600;
    }
    return static_cast<int>(value);
}

std::uint8_t clampPercent(std::int64_t value) noexcept {
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return static_cast<std::uint8_t>(value);
}

/// 0 means "hold the colon lit", so it has to survive clamping. Anything faster
/// than 100 ms is a strobe rather than a blink, and the ceiling keeps the
/// scheduler's next-due arithmetic in comfortable range.
std::uint32_t clampBlinkPeriod(std::int64_t value) noexcept {
    if (value <= 0) {
        return 0;
    }
    if (value < 100) {
        return 100;
    }
    if (value > 60000) {
        return 60000;
    }
    return static_cast<std::uint32_t>(value);
}

int clampUtcOffset(std::int64_t value) noexcept {
    // Real zones span UTC-12 to UTC+14.
    if (value < -12 * 3600) {
        return -12 * 3600;
    }
    if (value > 14 * 3600) {
        return 14 * 3600;
    }
    return static_cast<int>(value);
}

void appendEscaped(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20u) {
                    static const char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0xFu]);
                    out.push_back(kHex[static_cast<unsigned char>(c) & 0xFu]);
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

std::string buildBody(const Config& config) {
    std::string body = "{\"schemaVersion\":";
    body += std::to_string(kCurrentSchemaVersion);

    body += ",\"deviceName\":";
    appendEscaped(body, config.deviceName);

    body += ",\"display\":{\"brightness\":";
    body += std::to_string(static_cast<int>(config.display.brightness));
    body += ",\"power\":";
    body += config.display.power ? "true" : "false";
    body += '}';

    body += ",\"audio\":{\"volumePercent\":";
    body += std::to_string(static_cast<int>(config.audio.volumePercent));
    body += '}';

    body += ",\"apps\":{\"defaultDurationSeconds\":";
    body += std::to_string(config.apps.defaultDurationSeconds);
    body += ",\"transitions\":";
    body += config.apps.transitions ? "true" : "false";
    body += '}';

    body += ",\"clock\":{\"twentyFourHour\":";
    body += config.clock.twentyFourHour ? "true" : "false";
    body += ",\"utcOffsetSeconds\":";
    body += std::to_string(config.clock.utcOffsetSeconds);
    body += ",\"theme\":";
    appendEscaped(body, config.clock.theme);
    body += ",\"leadingZero\":";
    body += config.clock.leadingZero ? "true" : "false";
    body += ",\"showAmPm\":";
    body += config.clock.showAmPm ? "true" : "false";

    // Colours go out as #RRGGBB so a stored document stays readable by whoever
    // has to debug one over ADB.
    char hex[8];
    formatHexColor(fromPacked(config.clock.color), hex);
    body += ",\"color\":";
    appendEscaped(body, hex);
    formatHexColor(fromPacked(config.clock.accentColor), hex);
    body += ",\"accentColor\":";
    appendEscaped(body, hex);
    formatHexColor(fromPacked(config.clock.dateColor), hex);
    body += ",\"dateColor\":";
    appendEscaped(body, hex);

    body += ",\"dateOrder\":";
    appendEscaped(body, config.clock.dateOrder);
    body += ",\"dateSeparator\":";
    appendEscaped(body, config.clock.dateSeparator);
    body += ",\"dateYear\":";
    appendEscaped(body, config.clock.dateYear);
    body += ",\"blinkPeriodMillis\":";
    body += std::to_string(config.clock.blinkPeriodMillis);
    body += '}';

    body += '}';
    return body;
}

}  // namespace

const char* describe(LoadStatus status) noexcept {
    switch (status) {
        case LoadStatus::Loaded: return "loaded";
        case LoadStatus::Migrated: return "migrated from an older schema";
        case LoadStatus::RecoveredFromBackup: return "recovered from backup";
        case LoadStatus::DefaultsMissing: return "no configuration stored; using defaults";
        case LoadStatus::DefaultsCorrupt: return "configuration corrupt; using defaults";
        case LoadStatus::DefaultsFutureSchema:
            return "configuration written by newer firmware; using defaults";
    }
    return "unknown";
}

std::string ConfigStore::serialize(const Config& config) {
    const std::string body = buildBody(config);

    char hex[9];
    crc32ToHex(crc32(body), hex);

    std::string payload = "{\"checksum\":\"";
    payload += hex;
    payload += "\",\"body\":";
    payload += body;
    payload += '}';
    return payload;
}

bool ConfigStore::deserialize(std::string_view payload,
                              Config& out,
                              int& fromSchemaVersion,
                              bool& futureSchema) {
    fromSchemaVersion = 0;
    futureSchema = false;

    json::Token tokens[kMaxTokens];
    json::Document document(tokens, kMaxTokens);
    if (document.parse(payload) != json::Error::None) {
        return false;
    }

    const json::Value root = document.root();
    const json::Value checksum = root["checksum"];
    const json::Value body = root["body"];
    if (!checksum.isString() || !body.isObject()) {
        return false;
    }

    // The checksum covers the body text byte for byte, so a truncated or
    // partially-rewritten record is rejected rather than half-applied.
    const std::string expected = checksum.toString();
    char actual[9];
    crc32ToHex(crc32(body.raw()), actual);
    if (expected != actual) {
        return false;
    }

    const json::Value version = body["schemaVersion"];
    if (!version.isNumber()) {
        return false;
    }
    const std::int64_t schemaVersion = version.toInt(0);
    if (schemaVersion < 1) {
        return false;
    }
    if (schemaVersion > kCurrentSchemaVersion) {
        fromSchemaVersion = static_cast<int>(schemaVersion);
        futureSchema = true;
        return false;
    }
    fromSchemaVersion = static_cast<int>(schemaVersion);

    Config parsed;
    parsed.schemaVersion = kCurrentSchemaVersion;
    parsed.deviceName = body["deviceName"].toString(parsed.deviceName);

    const json::Value display = body["display"];
    const std::int64_t rawBrightness =
        display["brightness"].toInt(static_cast<std::int64_t>(parsed.display.brightness));

    // Migration v1 -> v2: brightness used to be a percentage.
    parsed.display.brightness = fromSchemaVersion < 2
                                    ? clampToByte((rawBrightness * 255 + 50) / 100)
                                    : clampToByte(rawBrightness);
    parsed.display.power = display["power"].toBool(parsed.display.power);

    const json::Value audio = body["audio"];
    parsed.audio.volumePercent =
        clampPercent(audio["volumePercent"].toInt(parsed.audio.volumePercent));

    const json::Value apps = body["apps"];
    parsed.apps.defaultDurationSeconds = clampDuration(
        apps["defaultDurationSeconds"].toInt(parsed.apps.defaultDurationSeconds));
    parsed.apps.transitions = apps["transitions"].toBool(parsed.apps.transitions);

    const json::Value clock = body["clock"];
    parsed.clock.twentyFourHour = clock["twentyFourHour"].toBool(parsed.clock.twentyFourHour);
    parsed.clock.utcOffsetSeconds =
        clampUtcOffset(clock["utcOffsetSeconds"].toInt(parsed.clock.utcOffsetSeconds));
    parsed.clock.theme = clock["theme"].toString(parsed.clock.theme);
    parsed.clock.leadingZero = clock["leadingZero"].toBool(parsed.clock.leadingZero);
    parsed.clock.showAmPm = clock["showAmPm"].toBool(parsed.clock.showAmPm);

    // A colour that will not parse keeps the default rather than failing the
    // load. Configuration recovery exists so one bad field cannot cost the user
    // every other setting they have.
    const auto colorOr = [&clock](const char* key, std::uint32_t fallback) {
        Rgb parsedColor;
        const json::Value value = clock[key];
        if (value.isString() && parseHexColor(value.raw(), parsedColor)) {
            return toPacked(parsedColor);
        }
        return fallback;
    };
    parsed.clock.color = colorOr("color", parsed.clock.color);
    parsed.clock.accentColor = colorOr("accentColor", parsed.clock.accentColor);
    parsed.clock.dateColor = colorOr("dateColor", parsed.clock.dateColor);

    parsed.clock.dateOrder = clock["dateOrder"].toString(parsed.clock.dateOrder);
    parsed.clock.dateSeparator = clock["dateSeparator"].toString(parsed.clock.dateSeparator);
    parsed.clock.dateYear = clock["dateYear"].toString(parsed.clock.dateYear);
    parsed.clock.blinkPeriodMillis =
        clampBlinkPeriod(clock["blinkPeriodMillis"].toInt(parsed.clock.blinkPeriodMillis));

    out = std::move(parsed);
    return true;
}

LoadReport ConfigStore::load(Config& out) const {
    LoadReport report;

    std::string payload;
    const bool primaryExists = storage_.read(kPrimaryKey, payload);

    if (primaryExists) {
        Config parsed;
        int version = 0;
        bool future = false;
        if (deserialize(payload, parsed, version, future)) {
            out = std::move(parsed);
            report.fromSchemaVersion = version;
            report.status =
                version < kCurrentSchemaVersion ? LoadStatus::Migrated : LoadStatus::Loaded;
            return report;
        }
        report.fromSchemaVersion = version;
        report.status = future ? LoadStatus::DefaultsFutureSchema : LoadStatus::DefaultsCorrupt;
    } else {
        report.status = LoadStatus::DefaultsMissing;
    }

    // The primary is unusable. The backup holds the last value that was good
    // enough to be replaced, which is the whole point of writing it.
    std::string backup;
    if (storage_.read(kBackupKey, backup)) {
        Config parsed;
        int version = 0;
        bool future = false;
        if (deserialize(backup, parsed, version, future)) {
            out = std::move(parsed);
            report.status = LoadStatus::RecoveredFromBackup;
            report.fromSchemaVersion = version;
            report.usedBackup = true;
            return report;
        }
    }

    out = Config{};
    return report;
}

bool ConfigStore::save(const Config& config) {
    const std::string payload = serialize(config);
    if (payload.size() > storage_.maxValueBytes()) {
        return false;
    }

    // Back up the current value first. IStorage guarantees each write is atomic,
    // so a power cut leaves either the old primary intact or the new one
    // complete, with the previous value still under the backup key either way.
    std::string current;
    if (storage_.read(kPrimaryKey, current) && current != payload) {
        storage_.write(kBackupKey, current);
    }

    return storage_.write(kPrimaryKey, payload);
}

}  // namespace config
}  // namespace notrix
