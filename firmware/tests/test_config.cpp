// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/config/Config.h"

#include <string>

#include "notrix/core/Checksum.h"
#include "notrix/platform/simulator/SimulatorPlatform.h"
#include "support/TestFramework.h"

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
    written.display.autoBrightness = true;
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
    NOTRIX_CHECK(read.display.autoBrightness);
    NOTRIX_CHECK_EQ(read.apps.defaultDurationSeconds, 12);
    NOTRIX_CHECK_FALSE(read.apps.transitions);
    NOTRIX_CHECK_FALSE(read.clock.twentyFourHour);
    NOTRIX_CHECK_EQ(read.clock.utcOffsetSeconds, 3600);
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

NOTRIX_TEST(Config, EveryStatusHasADescription) {
    for (int i = 0; i <= static_cast<int>(LoadStatus::DefaultsFutureSchema); ++i) {
        const char* text = notrix::config::describe(static_cast<LoadStatus>(i));
        NOTRIX_CHECK(text != nullptr && text[0] != '\0');
    }
}
