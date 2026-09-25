// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/time/Timezone.h"

namespace stipple {
namespace timezone_ {
namespace {

/// Days from 1970-01-01 to the given civil date.
///
/// Hinnant's civil calendar algorithm, exact for any year in range and needing
/// no table. Shifting the era to start in March is the whole trick: it puts the
/// leap day at the end of a year rather than in the middle of one.
std::int64_t daysFromCivilImpl(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);                   // 0-399
    const unsigned shifted = m > 2u ? m - 3u : m + 9u;  // March-based month, 0-11
    const unsigned doy = (153u * shifted + 2u) / 5u + d - 1u;                    // 0-365
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;               // 0-146096
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

/// Floor division, because C++ truncates toward zero and dates before 1970 are
/// negative. Getting this wrong puts the whole twentieth century a day out,
/// silently.
std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

/// Read an unsigned integer, advancing `at`. False if there is none.
bool readNumber(std::string_view text, std::size_t& at, int& out) noexcept {
    if (at >= text.size() || !isDigit(text[at])) {
        return false;
    }
    int value = 0;
    while (at < text.size() && isDigit(text[at])) {
        // Bounded rather than allowed to overflow: this parses a stored string
        // that arrived over the network.
        if (value > 100000) {
            return false;
        }
        value = value * 10 + (text[at] - '0');
        ++at;
    }
    out = value;
    return true;
}

/// Read a POSIX offset: [+|-]hh[:mm[:ss]].
///
/// Returned already flipped into "seconds to add to UTC", because POSIX states
/// it the other way round and carrying that inversion any further than this
/// function would guarantee somebody eventually forgets it.
bool readOffset(std::string_view text, std::size_t& at, int& out) noexcept {
    bool negative = false;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
        negative = text[at] == '-';
        ++at;
    }

    int hours = 0;
    if (!readNumber(text, at, hours) || hours > 24) {
        return false;
    }
    int minutes = 0;
    int seconds = 0;
    if (at < text.size() && text[at] == ':') {
        ++at;
        if (!readNumber(text, at, minutes) || minutes > 59) {
            return false;
        }
        if (at < text.size() && text[at] == ':') {
            ++at;
            if (!readNumber(text, at, seconds) || seconds > 59) {
                return false;
            }
        }
    }

    const int magnitude = hours * 3600 + minutes * 60 + seconds;
    out = negative ? magnitude : -magnitude;
    return true;
}

/// Skip a zone abbreviation: letters, or anything inside angle brackets.
bool readAbbreviation(std::string_view text, std::size_t& at) noexcept {
    if (at < text.size() && text[at] == '<') {
        ++at;
        while (at < text.size() && text[at] != '>') {
            ++at;
        }
        if (at >= text.size()) {
            return false;  // unterminated
        }
        ++at;
        return true;
    }

    const std::size_t begin = at;
    while (at < text.size() &&
           ((text[at] >= 'A' && text[at] <= 'Z') || (text[at] >= 'a' && text[at] <= 'z'))) {
        ++at;
    }
    // POSIX wants three or more. Shorter is a malformed string rather than an
    // exotic zone, and accepting it would let "5EDT" parse as a zone named "".
    return at - begin >= 3;
}

}  // namespace

std::int64_t daysFromCivil(int year, int month, int day) noexcept {
    return daysFromCivilImpl(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
}

CivilDate civilFromUnix(std::int64_t unixSeconds) noexcept {
    const std::int64_t days = floorDiv(unixSeconds, 86400);

    CivilDate out;
    // 1970-01-01 was a Thursday, which is 4 counting Sunday as 0.
    out.weekday = static_cast<int>(((days % 7) + 11) % 7);

    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    // Back from the March-based month to a calendar one.
    const unsigned m = mp < 10u ? mp + 3u : mp - 9u;

    out.year = static_cast<int>(y + (m <= 2u ? 1 : 0));
    out.month = static_cast<int>(m);
    out.day = static_cast<int>(d);
    return out;
}

std::int64_t Timezone::transitionUtc(const Rule& rule, int year, int offsetBefore) noexcept {
    // First day of the month, and which weekday it lands on.
    const std::int64_t firstDay = daysFromCivil(year, rule.month, 1);
    const int firstWeekday = static_cast<int>(((firstDay % 7) + 11) % 7);

    // Days from the 1st to the first occurrence of the wanted weekday.
    const int lead = ((rule.weekday - firstWeekday) + 7) % 7;
    std::int64_t day = 1 + lead + static_cast<std::int64_t>(rule.week - 1) * 7;

    // Week 5 means "the last one", which is not always the fifth: step back a
    // week until the date is still inside the month.
    const std::int64_t nextMonthDay =
        rule.month == 12 ? daysFromCivil(year + 1, 1, 1) : daysFromCivil(year, rule.month + 1, 1);
    const std::int64_t monthLength = nextMonthDay - firstDay;
    while (day > monthLength) {
        day -= 7;
    }

    const std::int64_t localMidnight = (firstDay + day - 1) * 86400;
    // The rule's time is local, expressed in whatever offset applies just
    // before the changeover: standard time going into summer, daylight time
    // coming out of it. Subtracting that offset turns it into UTC.
    return localMidnight + rule.secondsAfterMidnight - offsetBefore;
}

bool Timezone::parse(std::string_view spec, Timezone& out) noexcept {
    out = Timezone{};
    if (spec.empty()) {
        return false;
    }

    std::size_t at = 0;
    if (!readAbbreviation(spec, at)) {
        return false;
    }
    if (!readOffset(spec, at, out.standardOffset_)) {
        return false;
    }

    // No daylight section: a fixed-offset zone, which is a perfectly good
    // answer and what most of the world uses.
    if (at >= spec.size()) {
        return true;
    }

    if (!readAbbreviation(spec, at)) {
        return false;
    }

    // The daylight offset is optional and defaults to an hour ahead of
    // standard, which is what almost every zone does.
    if (at < spec.size() && spec[at] != ',') {
        if (!readOffset(spec, at, out.daylightOffset_)) {
            return false;
        }
    } else {
        out.daylightOffset_ = out.standardOffset_ + 3600;
    }

    // A daylight abbreviation with no rules means "daylight saving applies,
    // work out when yourself". Nothing here can, so it is refused rather than
    // guessed at.
    if (at >= spec.size() || spec[at] != ',') {
        out = Timezone{};
        return false;
    }

    const auto readRule = [&spec, &at](Rule& rule) noexcept {
        if (at >= spec.size() || spec[at] != ',') {
            return false;
        }
        ++at;
        if (at >= spec.size() || spec[at] != 'M') {
            return false;  // Jn and n forms are not used by any real zone
        }
        ++at;
        if (!readNumber(spec, at, rule.month) || rule.month < 1 || rule.month > 12) {
            return false;
        }
        if (at >= spec.size() || spec[at] != '.') {
            return false;
        }
        ++at;
        if (!readNumber(spec, at, rule.week) || rule.week < 1 || rule.week > 5) {
            return false;
        }
        if (at >= spec.size() || spec[at] != '.') {
            return false;
        }
        ++at;
        if (!readNumber(spec, at, rule.weekday) || rule.weekday < 0 || rule.weekday > 6) {
            return false;
        }

        rule.secondsAfterMidnight = 2 * 3600;
        if (at < spec.size() && spec[at] == '/') {
            ++at;
            int signedTime = 0;
            if (!readOffset(spec, at, signedTime)) {
                return false;
            }
            // readOffset returns POSIX's inverted sense; a rule time is a plain
            // wall clock, so flip it back.
            rule.secondsAfterMidnight = -signedTime;
        }
        return true;
    };

    if (!readRule(out.start_) || !readRule(out.end_)) {
        out = Timezone{};
        return false;
    }
    if (at != spec.size()) {
        out = Timezone{};
        return false;  // trailing rubbish means this was not the string it looks like
    }

    out.hasDaylight_ = true;
    return true;
}

bool Timezone::daylightActive(std::int64_t unixSeconds) const noexcept {
    if (!hasDaylight_) {
        return false;
    }

    const int year = civilFromUnix(unixSeconds).year;
    const std::int64_t begins = transitionUtc(start_, year, standardOffset_);
    const std::int64_t ends = transitionUtc(end_, year, daylightOffset_);

    if (begins <= ends) {
        // Northern hemisphere: summer sits inside the year.
        return unixSeconds >= begins && unixSeconds < ends;
    }
    // Southern hemisphere: summer wraps around new year, so the window is
    // everything outside the two instants rather than between them.
    return unixSeconds >= begins || unixSeconds < ends;
}

int Timezone::offsetSeconds(std::int64_t unixSeconds) const noexcept {
    return daylightActive(unixSeconds) ? daylightOffset_ : standardOffset_;
}

}  // namespace timezone_
}  // namespace stipple
