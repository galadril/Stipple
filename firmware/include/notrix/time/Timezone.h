// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace notrix {
namespace timezone_ {

/// A timezone, as POSIX describes one.
///
/// The device stored a fixed UTC offset, which is correct for about half the
/// year anywhere that observes daylight saving. Twice a year every clock in
/// Europe and North America would have been an hour wrong until somebody
/// noticed and edited a number - and "wrong twice a year" is not a clock.
///
/// **Rules rather than a database.** There is no /usr/share/zoneinfo on this
/// hardware and no /etc/localtime; the device runs in UTC. Shipping a copy of
/// the IANA database would cost megabytes on a device with an 8 MiB partition
/// and would go stale the moment a government moved a date. A POSIX TZ string
/// is one line, describes the rule rather than its consequences, and is the
/// same format every Unix already understands:
///
///     CET-1CEST,M3.5.0,M10.5.0/3      central Europe
///     EST5EDT,M3.2.0,M11.1.0          US eastern
///     AEST-10AEDT,M10.1.0,M4.1.0/3    Sydney, where summer is in January
///     UTC0                            no daylight saving at all
///
/// It lives in core rather than in the platform adapter because what time it
/// is locally is not a fact about SigmaStar hardware, and because the emulator
/// has to agree with the device about it.
///
/// **The offset sign is inverted, and that is POSIX's doing, not ours.**
/// "CET-1" means one hour *ahead* of UTC. It reads as a correction to apply to
/// local time to reach UTC. Everything below stores the sane direction -
/// seconds to add to UTC - and flips it once, at parse time.
class Timezone {
public:
    /// UTC with no daylight saving. What an unconfigured device uses, and what
    /// anything unparseable falls back to rather than guessing.
    Timezone() = default;

    /// Parse a POSIX TZ string. Returns false and leaves `out` at UTC if the
    /// string is not one - a clock showing the wrong time confidently is worse
    /// than one showing UTC and saying so.
    static bool parse(std::string_view spec, Timezone& out) noexcept;

    /// Seconds to add to UTC at this instant.
    int offsetSeconds(std::int64_t unixSeconds) const noexcept;

    /// Whether daylight saving is in effect at this instant.
    bool daylightActive(std::int64_t unixSeconds) const noexcept;

    /// True when this zone has a daylight rule at all.
    bool observesDaylight() const noexcept { return hasDaylight_; }

    int standardOffsetSeconds() const noexcept { return standardOffset_; }
    int daylightOffsetSeconds() const noexcept { return daylightOffset_; }

private:
    /// When a changeover happens, in the Mm.w.d form POSIX uses: the `week`th
    /// `weekday` of `month`, where week 5 means the last one.
    ///
    /// Only this form is supported. POSIX also allows Jn and n day-of-year
    /// rules, which no real zone uses for daylight saving; accepting them would
    /// be code nothing exercises.
    struct Rule {
        int month = 0;       ///< 1-12
        int week = 0;        ///< 1-5, 5 meaning last
        int weekday = 0;     ///< 0 = Sunday
        int secondsAfterMidnight = 2 * 3600;
    };

    /// The instant a rule fires, as a unix timestamp, given the offset that is
    /// in effect just before it fires.
    static std::int64_t transitionUtc(const Rule& rule, int year, int offsetBefore) noexcept;

    int standardOffset_ = 0;
    int daylightOffset_ = 0;
    bool hasDaylight_ = false;
    Rule start_;
    Rule end_;
};

/// Year, month, day and weekday for a unix timestamp, in UTC.
///
/// Exposed because the clock needs it and because a civil-date conversion that
/// only exists inside a timezone is a civil-date conversion nobody can test.
struct CivilDate {
    int year = 1970;
    int month = 1;      ///< 1-12
    int day = 1;        ///< 1-31
    int weekday = 4;    ///< 0 = Sunday; 1 Jan 1970 was a Thursday
};

CivilDate civilFromUnix(std::int64_t unixSeconds) noexcept;

/// Days since 1970-01-01 for a civil date. The inverse of the above.
std::int64_t daysFromCivil(int year, int month, int day) noexcept;

}  // namespace timezone_
}  // namespace notrix
