// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/config/Config.h"

#include <string>
#include <vector>

#include "notrix/json/Json.h"

#include "notrix/app/AppRegistry.h"
#include "notrix/apps/ClockApp.h"
#include "notrix/core/Checksum.h"
#include "notrix/platform/simulator/SimulatorPlatform.h"
#include "support/TestFramework.h"

using notrix::config::AppPreference;
using notrix::config::Config;
using notrix::config::ConfigStore;
using notrix::config::kCurrentSchemaVersion;
using notrix::config::LoadReport;
using notrix::config::LoadStatus;
using notrix::platform::simulator::SimulatorPlatform;

namespace {

int status(LoadStatus value) {
    return static_cast<int>(value);
}

/// Wrap a body in a correctly-checksummed envelope, so tests can hand-write
/// stored documents — including ones from older schema versions.
std::string envelope(const std::string& body) {
    char hex[9];
    notrix::crc32ToHex(notrix::crc32(body), hex);
    return std::string("{\"checksum\":\"") + hex + "\",\"body\":" + body + "}";
}

}  // namespace

// --- round trip --------------------------------------------------------------

NOTRIX_TEST(Config, SavesAndLoads) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config written;
    written.deviceName = "kitchen-clock";
    written.display.brightness = 200;
    written.display.power = false;
    written.apps.defaultDurationSeconds = 12;
    written.apps.transitions = false;
    written.clock.twentyFourHour = false;
    written.clock.utcOffsetSeconds = 3600;

    NOTRIX_CHECK(store.save(written));

    Config read;
    const LoadReport report = store.load(read);

    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::Loaded));
    NOTRIX_CHECK_EQ(read.deviceName, std::string("kitchen-clock"));
    NOTRIX_CHECK_EQ(static_cast<int>(read.display.brightness), 200);
    NOTRIX_CHECK_FALSE(read.display.power);
    NOTRIX_CHECK_EQ(read.apps.defaultDurationSeconds, 12);
    NOTRIX_CHECK_FALSE(read.apps.transitions);
    NOTRIX_CHECK_FALSE(read.clock.twentyFourHour);
    NOTRIX_CHECK_EQ(read.clock.utcOffsetSeconds, 3600);
}

NOTRIX_TEST(Config, ClockStyleSurvivesARoundTrip) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config written;
    written.clock.theme = "calendar";
    written.clock.leadingZero = false;
    written.clock.showAmPm = true;
    written.clock.color = 0x123456u;
    written.clock.accentColor = 0xABCDEFu;
    written.clock.dateColor = 0x0000FFu;
    written.clock.dateOrder = "yearMonthDay";
    written.clock.dateSeparator = "dash";
    written.clock.dateYear = "twoDigit";
    written.clock.blinkPeriodMillis = 250;
    NOTRIX_CHECK(store.save(written));

    Config read;
    NOTRIX_CHECK_EQ(status(store.load(read).status), status(LoadStatus::Loaded));
    NOTRIX_CHECK_EQ(read.clock.theme, std::string("calendar"));
    NOTRIX_CHECK_FALSE(read.clock.leadingZero);
    NOTRIX_CHECK(read.clock.showAmPm);
    NOTRIX_CHECK_EQ(static_cast<int>(read.clock.color), 0x123456);
    NOTRIX_CHECK_EQ(static_cast<int>(read.clock.accentColor), 0xABCDEF);
    NOTRIX_CHECK_EQ(static_cast<int>(read.clock.dateColor), 0x0000FF);
    NOTRIX_CHECK_EQ(read.clock.dateOrder, std::string("yearMonthDay"));
    NOTRIX_CHECK_EQ(read.clock.dateSeparator, std::string("dash"));
    NOTRIX_CHECK_EQ(read.clock.dateYear, std::string("twoDigit"));
    NOTRIX_CHECK_EQ(static_cast<int>(read.clock.blinkPeriodMillis), 250);
}

NOTRIX_TEST(Config, EveryDefaultClockNameRoundTripsThroughTheAppLayer) {
    // The settings API accepts only names that survive name -> enum -> name. A
    // default that did not round-trip would be rejected by the same endpoint
    // that reports it, which is a maddening bug to find from the outside.
    const Config defaults;
    NOTRIX_CHECK_EQ(
        std::string(notrix::apps::clockThemeName(
            notrix::apps::clockThemeFromName(defaults.clock.theme))),
        defaults.clock.theme);
    NOTRIX_CHECK_EQ(
        std::string(notrix::apps::dateOrderName(
            notrix::apps::dateOrderFromName(defaults.clock.dateOrder))),
        defaults.clock.dateOrder);
    NOTRIX_CHECK_EQ(
        std::string(notrix::apps::dateSeparatorName(
            notrix::apps::dateSeparatorFromName(defaults.clock.dateSeparator))),
        defaults.clock.dateSeparator);
    NOTRIX_CHECK_EQ(
        std::string(notrix::apps::dateYearName(
            notrix::apps::dateYearFromName(defaults.clock.dateYear))),
        defaults.clock.dateYear);
}

NOTRIX_TEST(Config, AMalformedColourKeepsTheDefaultRatherThanFailingTheLoad) {
    // One bad field must not cost the user every other setting they have.
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    platform.storage().write(
        ConfigStore::kPrimaryKey,
        envelope(R"({"schemaVersion":2,"deviceName":"kept",
                     "clock":{"color":"not-a-colour","accentColor":"#00FF00"}})"));

    Config read;
    const LoadReport report = store.load(read);

    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::Loaded));
    NOTRIX_CHECK_EQ(read.deviceName, std::string("kept"));
    NOTRIX_CHECK_EQ(static_cast<int>(read.clock.color), 0xFFFFFF);   // default
    NOTRIX_CHECK_EQ(static_cast<int>(read.clock.accentColor), 0x00FF00);  // applied
}

NOTRIX_TEST(Config, BlinkPeriodIsClampedButZeroIsPreserved) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    const auto loadedBlink = [&](const char* raw) {
        platform.storage().write(
            ConfigStore::kPrimaryKey,
            envelope(std::string(R"({"schemaVersion":2,"clock":{"blinkPeriodMillis":)") + raw +
                     "}}"));
        Config read;
        store.load(read);
        return static_cast<int>(read.clock.blinkPeriodMillis);
    };

    // 0 is not "unset"; it means hold the colon lit, so it must survive.
    NOTRIX_CHECK_EQ(loadedBlink("0"), 0);
    NOTRIX_CHECK_EQ(loadedBlink("-5"), 0);
    NOTRIX_CHECK_EQ(loadedBlink("10"), 100);
    NOTRIX_CHECK_EQ(loadedBlink("999999"), 60000);
    NOTRIX_CHECK_EQ(loadedBlink("1500"), 1500);
}

NOTRIX_TEST(Config, FirstBootUsesDefaults) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config config;
    const LoadReport report = store.load(config);

    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::DefaultsMissing));
    NOTRIX_CHECK_EQ(config.schemaVersion, kCurrentSchemaVersion);
    NOTRIX_CHECK_EQ(config.deviceName, std::string("notrix"));
}

NOTRIX_TEST(Config, DeviceNameWithAwkwardCharactersSurvives) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config written;
    written.deviceName = "he said \"hi\"\\\n\tdone";
    NOTRIX_CHECK(store.save(written));

    Config read;
    store.load(read);
    NOTRIX_CHECK_EQ(read.deviceName, written.deviceName);
}

// --- integrity ---------------------------------------------------------------

NOTRIX_TEST(Config, DetectsCorruptedPayload) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config written;
    written.display.brightness = 77;
    store.save(written);

    // Flip a byte inside the body. The checksum exists for exactly this.
    std::string stored;
    platform.storage().read(ConfigStore::kPrimaryKey, stored);
    const std::size_t digit = stored.find("77");
    NOTRIX_CHECK(digit != std::string::npos);
    stored[digit] = '8';
    platform.storage().write(ConfigStore::kPrimaryKey, stored);

    // Nothing was ever saved before this, so there is no backup to fall back to
    // and the load lands on defaults. The point is that the tampering is caught
    // rather than loaded as if it were genuine.
    Config read;
    const LoadReport report = store.load(read);
    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::DefaultsCorrupt));
    NOTRIX_CHECK_EQ(static_cast<int>(read.display.brightness), 128);
}

NOTRIX_TEST(Config, RecoversFromBackupWhenPrimaryIsCorrupt) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config first;
    first.deviceName = "original";
    store.save(first);

    Config second;
    second.deviceName = "updated";
    store.save(second);  // pushes "original" into the backup key

    platform.storage().write(ConfigStore::kPrimaryKey, "{ this is not json");

    Config read;
    const LoadReport report = store.load(read);

    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::RecoveredFromBackup));
    NOTRIX_CHECK(report.usedBackup);
    NOTRIX_CHECK_EQ(read.deviceName, std::string("original"));
}

NOTRIX_TEST(Config, BothCopiesCorruptFallsBackToDefaults) {
    // The rule that matters: a malformed configuration must never brick the
    // clock or cause a boot loop.
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    platform.storage().write(ConfigStore::kPrimaryKey, "garbage");
    platform.storage().write(ConfigStore::kBackupKey, "also garbage");

    Config read;
    const LoadReport report = store.load(read);

    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::DefaultsCorrupt));
    NOTRIX_CHECK_EQ(read.deviceName, std::string("notrix"));
}

NOTRIX_TEST(Config, TruncatedPayloadIsRejected) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config written;
    store.save(written);

    std::string stored;
    platform.storage().read(ConfigStore::kPrimaryKey, stored);

    // Every truncation must be refused, never half-applied.
    for (std::size_t length = 1; length < stored.size(); length += 7) {
        platform.storage().write(ConfigStore::kPrimaryKey, stored.substr(0, length));
        Config read;
        const LoadReport report = store.load(read);
        NOTRIX_CHECK(report.status != LoadStatus::Loaded);
    }
}

NOTRIX_TEST(Config, ArbitraryGarbageNeverCrashes) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    const char* samples[] = {
        "", "null", "[]", "{}", "{\"checksum\":1}", R"({"checksum":"zz","body":{}})",
        R"({"body":{"schemaVersion":2}})", R"({"checksum":"00000000","body":[]})",
        "\x01\x02\x03", R"({"checksum":"00000000","body":{"schemaVersion":"two"}})",
    };

    for (const char* sample : samples) {
        platform.storage().write(ConfigStore::kPrimaryKey, sample);
        Config read;
        store.load(read);
        NOTRIX_CHECK(read.schemaVersion == kCurrentSchemaVersion);
    }
}

// --- schema versions ---------------------------------------------------------

NOTRIX_TEST(Config, MigratesBrightnessFromPercentToBytes) {
    // v1 stored brightness as 0-100. Loading one must convert, not clamp.
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    platform.storage().write(
        ConfigStore::kPrimaryKey,
        envelope(R"({"schemaVersion":1,"deviceName":"old","display":{"brightness":50}})"));

    Config read;
    const LoadReport report = store.load(read);

    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::Migrated));
    NOTRIX_CHECK_EQ(report.fromSchemaVersion, 1);
    NOTRIX_CHECK_EQ(static_cast<int>(read.display.brightness), 128);
    NOTRIX_CHECK_EQ(read.deviceName, std::string("old"));
}

NOTRIX_TEST(Config, MigrationCoversTheFullRange) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    const auto migratedBrightness = [&](int percent) {
        platform.storage().write(ConfigStore::kPrimaryKey,
                                 envelope(R"({"schemaVersion":1,"display":{"brightness":)" +
                                          std::to_string(percent) + "}}"));
        Config read;
        store.load(read);
        return static_cast<int>(read.display.brightness);
    };

    NOTRIX_CHECK_EQ(migratedBrightness(0), 0);
    NOTRIX_CHECK_EQ(migratedBrightness(100), 255);
}

NOTRIX_TEST(Config, MigratedConfigIsRewrittenAtTheCurrentSchema) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    platform.storage().write(ConfigStore::kPrimaryKey,
                             envelope(R"({"schemaVersion":1,"display":{"brightness":100}})"));

    Config read;
    store.load(read);
    NOTRIX_CHECK_EQ(read.schemaVersion, kCurrentSchemaVersion);

    // Saving it back stores the new schema, so the migration happens once.
    NOTRIX_CHECK(store.save(read));
    Config again;
    const LoadReport report = store.load(again);
    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::Loaded));
    NOTRIX_CHECK_EQ(static_cast<int>(again.display.brightness), 255);
}

NOTRIX_TEST(Config, RefusesNewerSchema) {
    // Downgraded firmware must not guess at fields whose meaning has changed.
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    platform.storage().write(ConfigStore::kPrimaryKey,
                             envelope(R"({"schemaVersion":99,"deviceName":"future"})"));

    Config read;
    const LoadReport report = store.load(read);

    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::DefaultsFutureSchema));
    NOTRIX_CHECK_EQ(report.fromSchemaVersion, 99);
    NOTRIX_CHECK_EQ(read.deviceName, std::string("notrix"));
}

NOTRIX_TEST(Config, IgnoresUnknownFields) {
    // A config written by a newer minor build should still load.
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    platform.storage().write(
        ConfigStore::kPrimaryKey,
        envelope(R"({"schemaVersion":2,"deviceName":"x","somethingNew":{"a":[1,2]},
                     "display":{"brightness":90,"futureField":true}})"));

    Config read;
    const LoadReport report = store.load(read);

    NOTRIX_CHECK_EQ(status(report.status), status(LoadStatus::Loaded));
    NOTRIX_CHECK_EQ(static_cast<int>(read.display.brightness), 90);
}

// --- validation --------------------------------------------------------------

NOTRIX_TEST(Config, ClampsOutOfRangeValues) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    platform.storage().write(
        ConfigStore::kPrimaryKey,
        envelope(R"({"schemaVersion":2,"display":{"brightness":99999},
                     "apps":{"defaultDurationSeconds":-5},
                     "clock":{"utcOffsetSeconds":999999}})"));

    Config read;
    store.load(read);

    NOTRIX_CHECK_EQ(static_cast<int>(read.display.brightness), 255);
    NOTRIX_CHECK_EQ(read.apps.defaultDurationSeconds, 1);
    NOTRIX_CHECK_EQ(read.clock.utcOffsetSeconds, 14 * 3600);
}

NOTRIX_TEST(Config, SaveRejectsOversizedPayload) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config written;
    written.deviceName = std::string(platform.storage().maxValueBytes() + 1, 'n');
    NOTRIX_CHECK_FALSE(store.save(written));
}

NOTRIX_TEST(Config, TokenBudgetHasHeadroom) {
    // Overflowing kMaxTokens makes a perfectly good configuration read as
    // corrupt, and the device silently falls back to defaults -- losing every
    // setting the user had. Each field added since this was written eats into
    // the margin, so measure it with the real parser rather than by eye.
    Config config;
    config.deviceName = std::string(64, 'n');
    config.mqtt.host = "broker.example.invalid";
    config.mqtt.clientId = "notrix-kitchen";
    config.mqtt.username = "user";
    config.mqtt.password = "secret";

    // A full registry of remembered apps, which is what a maximal document
    // actually looks like now. Each one is about seven tokens, so this is more
    // of the budget than everything above it put together - and leaving it out
    // would have made this test measure a document no real device writes.
    for (int i = 0; i < notrix::config::kMaxRememberedApps; ++i) {
        AppPreference preference;
        preference.id = std::string(notrix::app::AppRegistry::kMaxIdBytes, 'a');
        preference.enabled = (i % 2) == 0;
        preference.durationSeconds = 3600;
        config.apps.order.push_back(std::move(preference));
    }

    const std::string payload = ConfigStore::serialize(config);

    // Find what a maximal document actually costs, by parsing it at rising
    // budgets until it fits.
    int needed = 0;
    for (int budget = 1; budget <= ConfigStore::kMaxTokens; ++budget) {
        std::vector<notrix::json::Token> tokens(static_cast<std::size_t>(budget));
        notrix::json::Document document(tokens.data(), budget);
        if (document.parse(payload) == notrix::json::Error::None) {
            needed = budget;
            break;
        }
    }

    NOTRIX_CHECK(needed > 0);  // it fits at all
    // Half the budget spare. Tighter than that and the next few settings would
    // silently push a real device over.
    NOTRIX_CHECK(needed <= ConfigStore::kMaxTokens / 2);

    // And the end-to-end proof, which is what actually matters.
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());
    NOTRIX_CHECK(store.save(config));

    Config read;
    NOTRIX_CHECK_EQ(status(store.load(read).status), status(LoadStatus::Loaded));
    NOTRIX_CHECK_EQ(read.mqtt.host, config.mqtt.host);
    NOTRIX_CHECK_EQ(read.deviceName, config.deviceName);
}

NOTRIX_TEST(Config, MqttSettingsRoundTrip) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config written;
    written.mqtt.enabled = true;
    written.mqtt.host = "broker.local";
    written.mqtt.port = 8883;
    written.mqtt.clientId = "kitchen";
    written.mqtt.baseTopic = "home";
    written.mqtt.username = "user";
    written.mqtt.password = "secret";
    written.mqtt.tls = true;
    written.mqtt.keepAliveSeconds = 45;
    written.mqtt.discovery = true;
    NOTRIX_CHECK(store.save(written));

    Config read;
    NOTRIX_CHECK_EQ(status(store.load(read).status), status(LoadStatus::Loaded));
    NOTRIX_CHECK(read.mqtt.enabled);
    NOTRIX_CHECK_EQ(read.mqtt.host, std::string("broker.local"));
    NOTRIX_CHECK_EQ(read.mqtt.port, 8883);
    NOTRIX_CHECK_EQ(read.mqtt.baseTopic, std::string("home"));
    // The credential has to survive storage, or the device cannot reconnect.
    NOTRIX_CHECK_EQ(read.mqtt.password, std::string("secret"));
    NOTRIX_CHECK(read.mqtt.tls);
    NOTRIX_CHECK_EQ(read.mqtt.keepAliveSeconds, 45);
}

NOTRIX_TEST(Config, MqttValuesAreClamped) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    platform.storage().write(
        ConfigStore::kPrimaryKey,
        envelope(R"({"schemaVersion":2,"mqtt":{"port":999999,"keepAliveSeconds":0,
                     "baseTopic":""}})"));

    Config read;
    store.load(read);

    NOTRIX_CHECK_EQ(read.mqtt.port, 65535);
    NOTRIX_CHECK_EQ(read.mqtt.keepAliveSeconds, 5);
    // An empty base would publish to "/{deviceId}/status".
    NOTRIX_CHECK_EQ(read.mqtt.baseTopic, std::string("notrix"));
}

NOTRIX_TEST(Config, EveryStatusHasADescription) {
    for (int i = 0; i <= static_cast<int>(LoadStatus::DefaultsFutureSchema); ++i) {
        const char* text = notrix::config::describe(static_cast<LoadStatus>(i));
        NOTRIX_CHECK(text != nullptr && text[0] != '\0');
    }
}

NOTRIX_TEST(Config, RememberedAppsMatchWhatTheRegistryCanHold) {
    // kMaxRememberedApps is duplicated rather than included, to keep
    // configuration from depending on the app layer for one number. That is
    // only safe if something notices when the two drift.
    NOTRIX_CHECK_EQ(notrix::config::kMaxRememberedApps,
                    notrix::app::AppRegistry::kMaxApps);
}

NOTRIX_TEST(Config, AppOrderSurvivesASaveAndLoad) {
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config written;
    AppPreference first;
    first.id = "battery";
    first.enabled = false;
    first.durationSeconds = 12;
    written.apps.order.push_back(first);

    AppPreference second;
    second.id = "clock";
    written.apps.order.push_back(second);

    NOTRIX_REQUIRE(store.save(written));

    Config read;
    store.load(read);

    NOTRIX_REQUIRE(read.apps.order.size() == 2);
    NOTRIX_CHECK_EQ(read.apps.order[0].id, std::string("battery"));
    NOTRIX_CHECK_FALSE(read.apps.order[0].enabled);
    NOTRIX_CHECK_EQ(read.apps.order[0].durationSeconds, 12);
    NOTRIX_CHECK_EQ(read.apps.order[1].id, std::string("clock"));
    NOTRIX_CHECK(read.apps.order[1].enabled);
}

NOTRIX_TEST(Config, AnOrderEntryWithNoIdIsSkippedOnLoad) {
    // An entry naming nothing orders nothing, and keeping it would leave a
    // permanent no-op sitting in the user's arrangement. Driven through save()
    // rather than by hand-writing the document, because a stored document
    // carries a checksum and one written by hand is simply rejected - which
    // would have made this test pass for the wrong reason.
    SimulatorPlatform platform;
    ConfigStore store(platform.storage());

    Config written;
    AppPreference nameless;          // id left empty
    nameless.enabled = true;
    written.apps.order.push_back(nameless);

    AppPreference real;
    real.id = "clock";
    written.apps.order.push_back(real);

    NOTRIX_REQUIRE(store.save(written));

    Config read;
    store.load(read);

    NOTRIX_REQUIRE(read.apps.order.size() == 1);
    NOTRIX_CHECK_EQ(read.apps.order[0].id, std::string("clock"));
}
