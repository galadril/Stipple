// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/time/Timezone.h"

#include <string>

#include "support/TestFramework.h"

using stipple::timezone_::CivilDate;
using stipple::timezone_::civilFromUnix;
using stipple::timezone_::daysFromCivil;
using stipple::timezone_::Timezone;

namespace {

/// A unix timestamp for a UTC civil date and time, built from the same
/// primitive the timezone uses. Circular only in the trivial direction: the
/// civil conversions are checked independently below against dates anybody can
/// verify by hand.
std::int64_t utc(int year, int month, int day, int hour = 0, int minute = 0) {
    return daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60;
}

Timezone zone(const char* spec) {
    Timezone out;
    STIPPLE_CHECK(Timezone::parse(spec, out));
    return out;
}

// The rules as the IANA database states them, written the way a device would
// store them.
constexpr const char* kAmsterdam = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr const char* kNewYork = "EST5EDT,M3.2.0,M11.1.0";
constexpr const char* kSydney = "AEST-10AEDT,M10.1.0,M4.1.0/3";

}  // namespace

// --- civil dates -------------------------------------------------------------

STIPPLE_TEST(Timezone, KnownDatesConvertBothWays) {
    // Dates anybody can check without running the code, which is the point of
    // having them: everything else here is built on these two functions.
    STIPPLE_CHECK_EQ(daysFromCivil(1970, 1, 1), 0);
    STIPPLE_CHECK_EQ(daysFromCivil(2000, 1, 1), 10957);
    STIPPLE_CHECK_EQ(daysFromCivil(2026, 9, 22), 20718);

    const CivilDate epoch = civilFromUnix(0);
    STIPPLE_CHECK_EQ(epoch.year, 1970);
    STIPPLE_CHECK_EQ(epoch.month, 1);
    STIPPLE_CHECK_EQ(epoch.day, 1);
    STIPPLE_CHECK_EQ(epoch.weekday, 4);  // a Thursday

    const CivilDate millennium = civilFromUnix(946684800);
    STIPPLE_CHECK_EQ(millennium.year, 2000);
    STIPPLE_CHECK_EQ(millennium.month, 1);
    STIPPLE_CHECK_EQ(millennium.day, 1);
    STIPPLE_CHECK_EQ(millennium.weekday, 6);  // a Saturday
}

STIPPLE_TEST(Timezone, LeapDaysAreRealDays) {
    // 2000 was a leap year and 1900 was not, which is the rule most naive
    // implementations get wrong.
    const CivilDate leap = civilFromUnix(utc(2000, 2, 29));
    STIPPLE_CHECK_EQ(leap.month, 2);
    STIPPLE_CHECK_EQ(leap.day, 29);

    STIPPLE_CHECK_EQ(daysFromCivil(2000, 3, 1) - daysFromCivil(2000, 2, 1), 29);
    STIPPLE_CHECK_EQ(daysFromCivil(1900, 3, 1) - daysFromCivil(1900, 2, 1), 28);
    STIPPLE_CHECK_EQ(daysFromCivil(2024, 3, 1) - daysFromCivil(2024, 2, 1), 29);
}

STIPPLE_TEST(Timezone, DatesBeforeTheEpochStillWork) {
    // Floor division rather than truncation. Getting this wrong puts the whole
    // twentieth century a day out, silently, and nothing on a clock would ever
    // show it.
    const CivilDate before = civilFromUnix(-86400);
    STIPPLE_CHECK_EQ(before.year, 1969);
    STIPPLE_CHECK_EQ(before.month, 12);
    STIPPLE_CHECK_EQ(before.day, 31);

    // Midway through that day, not just at midnight.
    const CivilDate midday = civilFromUnix(-43200);
    STIPPLE_CHECK_EQ(midday.day, 31);
    STIPPLE_CHECK_EQ(midday.year, 1969);
}

STIPPLE_TEST(Timezone, EveryDayOfAYearRoundTrips) {
    // Exhaustive over a leap year, because an off-by-one in the month tables
    // shows up on exactly one day and a spot check would miss it.
    for (int day = 0; day < 366; ++day) {
        const std::int64_t stamp = utc(2024, 1, 1) + static_cast<std::int64_t>(day) * 86400;
        const CivilDate date = civilFromUnix(stamp);
        STIPPLE_CHECK_EQ(daysFromCivil(date.year, date.month, date.day) * 86400, stamp);
    }
}

// --- parsing -----------------------------------------------------------------

STIPPLE_TEST(Timezone, TheOffsetSignIsInvertedExactlyOnce) {
    // POSIX writes the offset as a correction to reach UTC, so "CET-1" means
    // one hour *ahead*. Flipping it twice, or not at all, puts a European clock
    // two hours out - and the mistake looks perfectly reasonable in the source.
    STIPPLE_CHECK_EQ(zone(kAmsterdam).standardOffsetSeconds(), 3600);
    STIPPLE_CHECK_EQ(zone(kAmsterdam).daylightOffsetSeconds(), 7200);

    STIPPLE_CHECK_EQ(zone(kNewYork).standardOffsetSeconds(), -5 * 3600);
    STIPPLE_CHECK_EQ(zone(kNewYork).daylightOffsetSeconds(), -4 * 3600);

    STIPPLE_CHECK_EQ(zone(kSydney).standardOffsetSeconds(), 10 * 3600);
    STIPPLE_CHECK_EQ(zone(kSydney).daylightOffsetSeconds(), 11 * 3600);
}

STIPPLE_TEST(Timezone, AFixedOffsetZoneNeedsNoRules) {
    Timezone india;
    STIPPLE_REQUIRE(Timezone::parse("IST-5:30", india));
    STIPPLE_CHECK_FALSE(india.observesDaylight());
    STIPPLE_CHECK_EQ(india.standardOffsetSeconds(), 5 * 3600 + 1800);

    // And the same offset all year, which is the whole claim.
    STIPPLE_CHECK_EQ(india.offsetSeconds(utc(2026, 1, 15)), 5 * 3600 + 1800);
    STIPPLE_CHECK_EQ(india.offsetSeconds(utc(2026, 7, 15)), 5 * 3600 + 1800);
}

STIPPLE_TEST(Timezone, RubbishIsRefusedRatherThanApproximated) {
    // A clock confidently showing the wrong time is worse than one showing UTC,
    // so anything unparseable has to fail rather than half-parse.
    const char* nonsense[] = {
        "",
        "5",                        // no abbreviation
        "CE-1",                     // abbreviation too short
        "CET",                      // no offset
        "CET-1CEST",                // daylight with no rules to apply it by
        "CET-1CEST,M3.5.0",         // only one rule
        "CET-1CEST,M3.5.0,M10.5.0,",  // trailing rubbish
        "CET-1CEST,J80,J300",       // day-of-year rules, deliberately unsupported
        "CET-1CEST,M13.5.0,M10.5.0",  // month 13
        "CET-1CEST,M3.9.0,M10.5.0",   // week 9
        "CET-1CEST,M3.5.9,M10.5.0",   // weekday 9
        "CET-99",                   // absurd offset
        "<GMT+3",                   // unterminated bracket
    };

    for (const char* spec : nonsense) {
        Timezone parsed;
        STIPPLE_CHECK_FALSE(Timezone::parse(spec, parsed));
        // And it must be left at UTC rather than half-filled.
        STIPPLE_CHECK_EQ(parsed.offsetSeconds(utc(2026, 7, 1)), 0);
        STIPPLE_CHECK_FALSE(parsed.observesDaylight());
    }
}

STIPPLE_TEST(Timezone, ABracketedAbbreviationIsAccepted) {
    // What the database uses for zones whose name is not letters, such as
    // <+04>-4. Refusing it would silently drop a chunk of the world.
    Timezone gulf;
    STIPPLE_REQUIRE(Timezone::parse("<+04>-4", gulf));
    STIPPLE_CHECK_EQ(gulf.standardOffsetSeconds(), 4 * 3600);
}

// --- daylight saving ---------------------------------------------------------

STIPPLE_TEST(Timezone, EuropeSpringsForwardOnTheLastSundayInMarch) {
    // 2026: 01:00 UTC on Sunday 29 March. The reason this class exists - a
    // stored offset is simply wrong from this instant until October.
    const Timezone amsterdam = zone(kAmsterdam);
    const std::int64_t change = utc(2026, 3, 29, 1);

    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(change - 1), 3600);
    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(change), 7200);
    STIPPLE_CHECK_FALSE(amsterdam.daylightActive(change - 1));
    STIPPLE_CHECK(amsterdam.daylightActive(change));
}

STIPPLE_TEST(Timezone, EuropeFallsBackOnTheLastSundayInOctober) {
    // 01:00 UTC on Sunday 25 October 2026. The rule says 03:00 local, and local
    // is summer time at that moment - so the instant is 01:00 UTC, not 02:00.
    // Using the wrong offset here puts the changeover an hour out.
    const Timezone amsterdam = zone(kAmsterdam);
    const std::int64_t change = utc(2026, 10, 25, 1);

    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(change - 1), 7200);
    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(change), 3600);
}

STIPPLE_TEST(Timezone, NewYorkUsesTheSecondSundayInMarch) {
    // 07:00 UTC on Sunday 8 March 2026: 02:00 local standard time.
    const Timezone newYork = zone(kNewYork);
    const std::int64_t change = utc(2026, 3, 8, 7);

    STIPPLE_CHECK_EQ(newYork.offsetSeconds(change - 1), -5 * 3600);
    STIPPLE_CHECK_EQ(newYork.offsetSeconds(change), -4 * 3600);

    // And back on the first Sunday in November, at 06:00 UTC.
    const std::int64_t back = utc(2026, 11, 1, 6);
    STIPPLE_CHECK_EQ(newYork.offsetSeconds(back - 1), -4 * 3600);
    STIPPLE_CHECK_EQ(newYork.offsetSeconds(back), -5 * 3600);
}

STIPPLE_TEST(Timezone, TheSouthernHemisphereHasSummerAtNewYear) {
    // Sydney's daylight window wraps the year end, so the naive "between the
    // two dates" test reports it backwards - summer in June and winter in
    // January. Worth its own test because it is the case a northern-hemisphere
    // author never sees.
    const Timezone sydney = zone(kSydney);

    STIPPLE_CHECK(sydney.daylightActive(utc(2026, 1, 15)));    // January: summer
    STIPPLE_CHECK_EQ(sydney.offsetSeconds(utc(2026, 1, 15)), 11 * 3600);

    STIPPLE_CHECK_FALSE(sydney.daylightActive(utc(2026, 6, 15)));  // June: winter
    STIPPLE_CHECK_EQ(sydney.offsetSeconds(utc(2026, 6, 15)), 10 * 3600);

    STIPPLE_CHECK(sydney.daylightActive(utc(2026, 12, 15)));   // December: summer again
}

STIPPLE_TEST(Timezone, LastSundayIsNotAlwaysTheFifth) {
    // "M10.5.0" means the last Sunday, and some Octobers have four. A rule that
    // counted to five blindly would land in November.
    const Timezone amsterdam = zone(kAmsterdam);

    // 2021: the last Sunday in October was the 31st, the fifth.
    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(utc(2021, 10, 31, 0)), 7200);
    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(utc(2021, 10, 31, 1)), 3600);

    // 2027: the last Sunday is the 31st again, but March that year has its last
    // Sunday on the 28th - the fourth, not the fifth.
    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(utc(2027, 3, 28, 0)), 3600);
    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(utc(2027, 3, 28, 1)), 7200);
}

STIPPLE_TEST(Timezone, AYearOfOffsetsChangesExactlyTwice) {
    // Sampled hourly across a whole year. A rule that fired on the wrong day,
    // or twice, or drifted would show up here and nowhere else.
    const Timezone amsterdam = zone(kAmsterdam);

    int changes = 0;
    int previous = amsterdam.offsetSeconds(utc(2026, 1, 1));
    for (std::int64_t hour = 1; hour < 365 * 24; ++hour) {
        const int now = amsterdam.offsetSeconds(utc(2026, 1, 1) + hour * 3600);
        if (now != previous) {
            ++changes;
            previous = now;
        }
    }
    STIPPLE_CHECK_EQ(changes, 2);

    // Starts and ends the year on standard time.
    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(utc(2026, 1, 1)), 3600);
    STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(utc(2026, 12, 31)), 3600);
}

STIPPLE_TEST(Timezone, TheRuleHoldsAcrossManyYears) {
    // Not just the year it was written in. A transition computed from the wrong
    // year's calendar would drift a day at a time and look fine at first.
    const Timezone amsterdam = zone(kAmsterdam);

    for (int year = 2020; year <= 2035; ++year) {
        // Deep summer and deep winter, which no plausible rule shift reaches.
        STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(utc(year, 7, 1)), 7200);
        STIPPLE_CHECK_EQ(amsterdam.offsetSeconds(utc(year, 1, 15)), 3600);
    }
}

STIPPLE_TEST(Timezone, UtcIsTheDefaultAndSaysSo) {
    const Timezone unset;
    STIPPLE_CHECK_EQ(unset.offsetSeconds(utc(2026, 7, 1)), 0);
    STIPPLE_CHECK_EQ(unset.offsetSeconds(utc(2026, 1, 1)), 0);
    STIPPLE_CHECK_FALSE(unset.observesDaylight());
}
