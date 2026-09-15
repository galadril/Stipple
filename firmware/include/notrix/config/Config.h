// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "notrix/platform/Storage.h"

namespace notrix {
namespace config {

/// Bumped whenever the stored shape changes. Every bump needs migration code
/// and a test that loads a document written by the previous version.
///
/// v1 -> v2: display brightness moved from 0-100 percent to 0-255, matching the
/// panel's actual range instead of forcing a conversion at every use.
inline constexpr int kCurrentSchemaVersion = 2;

struct DisplaySettings {
    std::uint8_t brightness = 128;
    bool autoBrightness = false;
};

struct AppSettings {
    int defaultDurationSeconds = 8;
    bool transitions = true;
};

struct ClockSettings {
    bool twentyFourHour = true;
    int utcOffsetSeconds = 0;

    /// Clock face, by name (see apps::clockThemeFromName). Stored as a string
    /// rather than an enum so that configuration does not depend on the app
    /// layer, and so an unrecognised value from a newer build degrades to the
    /// default instead of failing the whole load.
    ///
    /// Adding this needed no schema bump: an absent field takes its default, so
    /// a v2 document still loads unchanged.
    std::string theme = "minimal";
};

struct Config {
    int schemaVersion = kCurrentSchemaVersion;
    std::string deviceName = "notrix";
    DisplaySettings display;
    AppSettings apps;
    ClockSettings clock;
};

enum class LoadStatus : std::uint8_t {
    Loaded,               ///< primary read cleanly at the current schema
    Migrated,             ///< primary read cleanly and was upgraded
    RecoveredFromBackup,  ///< primary unusable, backup was good
    DefaultsMissing,      ///< nothing stored yet; first boot
    DefaultsCorrupt,      ///< stored data failed checksum or parsing
    DefaultsFutureSchema, ///< written by newer firmware; refused rather than guessed
};

const char* describe(LoadStatus status) noexcept;

struct LoadReport {
    LoadStatus status = LoadStatus::DefaultsMissing;
    int fromSchemaVersion = 0;
    bool usedBackup = false;
};

/// Persistent configuration with recovery (blueprint §21).
///
/// Stored as `{"checksum":"...","body":{...}}`, where the checksum covers the
/// body text exactly as written. Layout and behaviour follow from one rule: a
/// malformed or truncated configuration must never brick the clock or cause a
/// boot loop. So every failure path ends at usable settings — backup first,
/// then defaults — and the caller is told which happened rather than having to
/// guess from behaviour.
///
/// Unknown fields are ignored, so a config written by a newer *minor* build
/// still loads. A newer *schema* is refused outright: misinterpreting fields
/// whose meaning has changed is worse than starting from defaults.
class ConfigStore {
public:
    static constexpr std::string_view kPrimaryKey = "config";
    static constexpr std::string_view kBackupKey = "config.bak";
    static constexpr int kMaxTokens = 128;

    explicit ConfigStore(platform::IStorage& storage) noexcept : storage_(storage) {}

    /// Always leaves `out` usable, whatever the stored data looks like.
    LoadReport load(Config& out) const;

    /// Copies the current value to the backup key before replacing the primary,
    /// so an interrupted save leaves the previous configuration recoverable.
    bool save(const Config& config);

    static std::string serialize(const Config& config);

    /// Returns false if the payload is unparseable, fails its checksum, or
    /// carries a newer schema — in which case `futureSchema` is set.
    static bool deserialize(std::string_view payload,
                            Config& out,
                            int& fromSchemaVersion,
                            bool& futureSchema);

private:
    platform::IStorage& storage_;
};

}  // namespace config
}  // namespace notrix
