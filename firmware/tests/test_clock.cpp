// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/ClockApp.h"

#include <string>

#include "notrix/graphics/Canvas.h"
#include "notrix/platform/simulator/SimulatorPlatform.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using notrix::Canvas;
using notrix::Framebuffer;
using notrix::Rect;
using notrix::apps::CivilDate;
using notrix::apps::civilFromDays;
using notrix::apps::ClockStyle;
using notrix::apps::ClockTheme;
using notrix::apps::clockChanged;
using notrix::apps::clockThemeAt;
using notrix::apps::clockThemeFromName;
using notrix::apps::clockThemeName;
using notrix::apps::kClockThemeCount;
using notrix::apps::renderClock;
using notrix::apps::weekdayFromDays;
using notrix::apps::weekdayName;
using notrix::platform::simulator::SimulatorClock;
namespace colors = notrix::colors;

namespace {

/// Seconds since the epoch for a UTC date and time, computed independently of
/// the code under test so the tests are not checking an algorithm against
/// itself.
std::int64_t unixTime(int year, int month, int day, int hour, int minute, int second) {
    // Days-from-civil: the inverse of the algorithm being tested, written out
    // separately on purpose.
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

SimulatorClock clockAt(std::int64_t unixSeconds, std::uint64_t monotonic = 0) {
    SimulatorClock clock;
    clock.setWallClock(unixSeconds);
    clock.advance(monotonic);
    return clock;
}

Framebuffer render(const SimulatorClock& clock, const ClockStyle& style) {
    Framebuffer framebuffer;
    Canvas canvas(framebuffer);
    renderClock(canvas, clock, style);
    return framebuffer;
}

/// Bounding box of the lit pixels.
Rect litBounds(const Framebuffer& framebuffer) {
    int minX = Framebuffer::kWidth, minY = Framebuffer::kHeight, maxX = -1, maxY = -1;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (framebuffer.at(x, y) != colors::kBlack) {
                if (x < minX) { minX = x; }
                if (y < minY) { minY = y; }
                if (x > maxX) { maxX = x; }
                if (y > maxY) { maxY = y; }
            }
        }
    }
    return maxX < 0 ? Rect{} : Rect{minX, minY, maxX - minX + 1, maxY - minY + 1};
}

ClockStyle styleFor(ClockTheme theme) {
    ClockStyle style;
    style.theme = theme;
    return style;
}

}  // namespace

// --- calendar ----------------------------------------------------------------

NOTRIX_TEST(Clock, ConvertsDaysToCivilDates) {
    NOTRIX_CHECK_EQ(civilFromDays(0).year, 1970);
    NOTRIX_CHECK_EQ(civilFromDays(0).month, 1);
    NOTRIX_CHECK_EQ(civilFromDays(0).day, 1);

    const CivilDate later = civilFromDays(unixTime(2026, 9, 11, 0, 0, 0) / 86400);
    NOTRIX_CHECK_EQ(later.year, 2026);
    NOTRIX_CHECK_EQ(later.month, 9);
    NOTRIX_CHECK_EQ(later.day, 11);
}

NOTRIX_TEST(Clock, HandlesLeapDays) {
    // The shifted-era trick exists precisely so February needs no special case.
    const CivilDate leap = civilFromDays(unixTime(2024, 2, 29, 0, 0, 0) / 86400);
    NOTRIX_CHECK_EQ(leap.month, 2);
    NOTRIX_CHECK_EQ(leap.day, 29);

    // 2000 was a leap year; 1900 was not.
    NOTRIX_CHECK_EQ(civilFromDays(unixTime(2000, 2, 29, 0, 0, 0) / 86400).day, 29);
    NOTRIX_CHECK_EQ(civilFromDays(unixTime(1900, 3, 1, 0, 0, 0) / 86400).month, 3);
}

NOTRIX_TEST(Clock, HandlesYearAndMonthBoundaries) {
    const CivilDate newYear = civilFromDays(unixTime(2027, 1, 1, 0, 0, 0) / 86400);
    NOTRIX_CHECK_EQ(newYear.year, 2027);
    NOTRIX_CHECK_EQ(newYear.month, 1);
    NOTRIX_CHECK_EQ(newYear.day, 1);

    const CivilDate yearEnd = civilFromDays(unixTime(2026, 12, 31, 0, 0, 0) / 86400);
    NOTRIX_CHECK_EQ(yearEnd.month, 12);
    NOTRIX_CHECK_EQ(yearEnd.day, 31);
}

NOTRIX_TEST(Clock, RoundTripsEveryDayAcrossDecades) {
    // Walk twenty years of days and confirm the date always advances by exactly
    // one, which catches any off-by-one at a month or year boundary.
    std::int64_t day = unixTime(2020, 1, 1, 0, 0, 0) / 86400;
    CivilDate previous = civilFromDays(day);

    for (int i = 1; i < 365 * 20; ++i) {
        const CivilDate current = civilFromDays(day + i);

        const bool sameMonth = current.year == previous.year && current.month == previous.month;
        if (sameMonth) {
            NOTRIX_CHECK_EQ(current.day, previous.day + 1);
        } else {
            NOTRIX_CHECK_EQ(current.day, 1);
            NOTRIX_CHECK(current.month >= 1 && current.month <= 12);
        }
        previous = current;
    }
}

NOTRIX_TEST(Clock, WeekdaysAreCorrect) {
    // 1970-01-01 was a Thursday.
    NOTRIX_CHECK_EQ(std::string(weekdayName(weekdayFromDays(0))), std::string("THU"));
    NOTRIX_CHECK_EQ(std::string(weekdayName(weekdayFromDays(1))), std::string("FRI"));
    NOTRIX_CHECK_EQ(std::string(weekdayName(weekdayFromDays(3))), std::string("SUN"));

    // 2026-09-11 is a Friday.
    const std::int64_t days = unixTime(2026, 9, 11, 0, 0, 0) / 86400;
    NOTRIX_CHECK_EQ(std::string(weekdayName(weekdayFromDays(days))), std::string("FRI"));
}

NOTRIX_TEST(Clock, WeekdayNameIsBounded) {
    NOTRIX_CHECK_EQ(std::string(weekdayName(-1)), std::string("---"));
    NOTRIX_CHECK_EQ(std::string(weekdayName(7)), std::string("---"));
}

// --- the colon must not move the digits --------------------------------------

NOTRIX_TEST(Clock, BlinkingTheColonDoesNotShiftTheDigits) {
    // The colon is one pixel wide and the space glyph is two, so blinking by
    // swapping characters moved the minutes sideways twice a second. The colon
    // now occupies a reserved slot whether lit or not.
    const std::int64_t when = unixTime(2026, 9, 11, 14, 35, 0);

    ClockStyle style = styleFor(ClockTheme::Minimal);
    style.blinkPeriodMillis = 1000;

    const SimulatorClock lit = clockAt(when, 0);     // first half: colon on
    const SimulatorClock dark = clockAt(when, 600);  // second half: colon off

    const Framebuffer withColon = render(lit, style);
    const Framebuffer withoutColon = render(dark, style);

    NOTRIX_CHECK(withColon != withoutColon);  // the colon really did blink

    // Every lit pixel outside the colon's slot must be identical.
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const int colonX = (Framebuffer::kWidth - 25) / 2 + 12;
            if (x == colonX) {
                continue;  // the slot itself is allowed to differ
            }
            NOTRIX_CHECK_EQ(withColon.at(x, y), withoutColon.at(x, y));
        }
    }
}

NOTRIX_TEST(Clock, TheHourFieldNeverMovesTheMinutes) {
    // Comparing ink extents would be wrong: '0' lights column 0 of its cell and
    // '1' does not, so different digits legitimately have different bounds. The
    // property that matters is that the *slots* are fixed — whatever the hour
    // is, the minutes occupy exactly the same pixels.
    ClockStyle style = styleFor(ClockTheme::Minimal);
    style.blinkPeriodMillis = 0;

    const Framebuffer early = render(clockAt(unixTime(2026, 9, 11, 8, 45, 0)), style);
    const Framebuffer late = render(clockAt(unixTime(2026, 9, 11, 19, 45, 0)), style);

    NOTRIX_CHECK(early != late);  // the hours really did change

    // Everything from the colon rightwards must be identical.
    const int colonX = (Framebuffer::kWidth - 25) / 2 + 12;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = colonX; x < Framebuffer::kWidth; ++x) {
            NOTRIX_CHECK_EQ(early.at(x, y), late.at(x, y));
        }
    }
}

NOTRIX_TEST(Clock, TheMinuteFieldNeverMovesTheHours) {
    ClockStyle style = styleFor(ClockTheme::Minimal);
    style.blinkPeriodMillis = 0;

    const Framebuffer a = render(clockAt(unixTime(2026, 9, 11, 14, 5, 0)), style);
    const Framebuffer b = render(clockAt(unixTime(2026, 9, 11, 14, 58, 0)), style);

    const int colonX = (Framebuffer::kWidth - 25) / 2 + 12;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < colonX; ++x) {
            NOTRIX_CHECK_EQ(a.at(x, y), b.at(x, y));
        }
    }
}

NOTRIX_TEST(Clock, TwelveHourModeDoesNotShiftAtTen) {
    // A blanked leading zero must leave its slot empty rather than narrowing the
    // field, or the time slides sideways at 10 o'clock.
    ClockStyle style = styleFor(ClockTheme::Minimal);
    style.twentyFourHour = false;
    style.blinkPeriodMillis = 0;

    const Rect nine = litBounds(render(clockAt(unixTime(2026, 9, 11, 9, 30, 0)), style));
    const Rect ten = litBounds(render(clockAt(unixTime(2026, 9, 11, 10, 30, 0)), style));

    // The minutes end in the same place; only the hours field differs.
    NOTRIX_CHECK_EQ(nine.right(), ten.right());
}

// --- themes ------------------------------------------------------------------

NOTRIX_TEST(Clock, ThemeNamesRoundTrip) {
    for (int i = 0; i < kClockThemeCount; ++i) {
        const ClockTheme theme = clockThemeAt(i);
        NOTRIX_CHECK(clockThemeFromName(clockThemeName(theme)) == theme);
    }
    NOTRIX_CHECK(clockThemeFromName("nonsense") == ClockTheme::Minimal);
    NOTRIX_CHECK(clockThemeFromName("") == ClockTheme::Minimal);
}

NOTRIX_TEST(Clock, EveryThemeDrawsAndStaysOnThePanel) {
    const SimulatorClock clock = clockAt(unixTime(2026, 9, 11, 14, 35, 42));

    for (int i = 0; i < kClockThemeCount; ++i) {
        ClockStyle style = styleFor(clockThemeAt(i));
        style.blinkPeriodMillis = 0;

        const Framebuffer frame = render(clock, style);
        const Rect lit = litBounds(frame);

        NOTRIX_CHECK_FALSE(lit.empty());
        NOTRIX_CHECK(lit.x >= 0);
        NOTRIX_CHECK(lit.y >= 0);
        NOTRIX_CHECK(lit.right() <= Framebuffer::kWidth);
        NOTRIX_CHECK(lit.bottom() <= Framebuffer::kHeight);
    }
}

NOTRIX_TEST(Clock, ThemesDifferFromEachOther) {
    const SimulatorClock clock = clockAt(unixTime(2026, 9, 11, 14, 35, 42));

    ClockStyle minimal = styleFor(ClockTheme::Minimal);
    minimal.blinkPeriodMillis = 0;

    for (int i = 1; i < kClockThemeCount; ++i) {
        ClockStyle other = styleFor(clockThemeAt(i));
        other.blinkPeriodMillis = 0;
        NOTRIX_CHECK(render(clock, minimal) != render(clock, other));
    }
}

NOTRIX_TEST(Clock, TheStyleOffsetShiftsTheDisplayedTime) {
    // Shifting the offset forward by an hour must look exactly like the clock
    // itself having advanced an hour. Exact rather than "the frames differ",
    // because a wrong-but-different offset would pass that.
    const std::int64_t noon = unixTime(2026, 9, 20, 12, 0, 0);

    ClockStyle shifted = styleFor(ClockTheme::Minimal);
    shifted.utcOffsetSeconds = 3600;

    ClockStyle plain = styleFor(ClockTheme::Minimal);
    plain.utcOffsetSeconds = 0;

    NOTRIX_CHECK(render(clockAt(noon), shifted) == render(clockAt(noon + 3600), plain));
}

NOTRIX_TEST(Clock, AWholeDayOfOffsetLooksLikeNone) {
    // Catches a sign error, which the hour test above cannot: negating the
    // offset still produces "some other time", but only a correctly applied
    // one wraps a full day back onto itself.
    const std::int64_t noon = unixTime(2026, 9, 20, 12, 0, 0);

    ClockStyle wrapped = styleFor(ClockTheme::Seconds);
    wrapped.utcOffsetSeconds = 24 * 3600;

    ClockStyle plain = styleFor(ClockTheme::Seconds);

    NOTRIX_CHECK(render(clockAt(noon), wrapped) == render(clockAt(noon), plain));
}

NOTRIX_TEST(Clock, TheSecondsBarFillsAndComesBackAfterTheMinute) {
    // Reported from hardware: the bar filled, vanished at the minute, and did
    // not come back. Walk an actual rollover rather than sampling one instant.
    ClockStyle style = styleFor(ClockTheme::SecondsBar);
    style.blinkPeriodMillis = 0;  // keep the colon out of the comparison

    const std::int64_t minuteStart = unixTime(2026, 9, 20, 12, 5, 0);

    auto barWidth = [&](int second) {
        const Framebuffer frame = render(clockAt(minuteStart + second), style);
        int lit = 0;
        const int row = Framebuffer::kHeight - 1;
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (frame.at(x, row) == style.accentColor) {
                ++lit;
            }
        }
        return lit;
    };

    NOTRIX_CHECK_EQ(barWidth(0), 0);                        // empty at the top
    NOTRIX_CHECK_EQ(barWidth(59), Framebuffer::kWidth);      // full at the end

    // Monotonic across the minute, never going backwards mid-way.
    int previous = 0;
    for (int second = 0; second < 60; ++second) {
        const int width = barWidth(second);
        NOTRIX_CHECK(width >= previous);
        previous = width;
    }

    // And the rollover: full, then empty, then growing again. "Did not come
    // back" is precisely this last assertion.
    NOTRIX_CHECK_EQ(barWidth(59), Framebuffer::kWidth);
    NOTRIX_CHECK_EQ(barWidth(60), 0);
    NOTRIX_CHECK(barWidth(61) > 0);
}

NOTRIX_TEST(Clock, TheSecondsBarAsksToBeRedrawnEverySecond) {
    // The other half of the failure: a bar that renders correctly is still
    // frozen if dirty tracking never invalidates. On this device a skipped
    // frame is not a stale frame but a dark one, so a missed invalidate shows
    // up as the bar simply stopping.
    const SimulatorClock clock = clockAt(unixTime(2026, 9, 20, 12, 5, 30));
    ClockStyle style = styleFor(ClockTheme::SecondsBar);
    style.blinkPeriodMillis = 0;

    NOTRIX_CHECK(clockChanged(clock, style, 0, 1000));
    NOTRIX_CHECK(clockChanged(clock, style, 59'000, 60'000));  // across the minute
    NOTRIX_CHECK_FALSE(clockChanged(clock, style, 1000, 1200));  // same second
}

NOTRIX_TEST(Clock, UnsetWallClockShowsPlaceholderInEveryTheme) {
    SimulatorClock unset;  // never set
    for (int i = 0; i < kClockThemeCount; ++i) {
        ClockStyle style = styleFor(clockThemeAt(i));
        const Framebuffer frame = render(unset, style);
        NOTRIX_CHECK_FALSE(litBounds(frame).empty());  // "--:--", not a blank panel
    }
}

// --- redraw hints ------------------------------------------------------------

NOTRIX_TEST(Clock, SecondsThemesRedrawEverySecond) {
    SimulatorClock clock = clockAt(unixTime(2026, 9, 11, 14, 35, 0));

    ClockStyle seconds = styleFor(ClockTheme::Seconds);
    seconds.blinkPeriodMillis = 0;
    NOTRIX_CHECK(clockChanged(clock, seconds, 0, 1000));
    NOTRIX_CHECK_FALSE(clockChanged(clock, seconds, 100, 200));

    ClockStyle bar = styleFor(ClockTheme::SecondsBar);
    bar.blinkPeriodMillis = 0;
    NOTRIX_CHECK(clockChanged(clock, bar, 0, 1000));
}

NOTRIX_TEST(Clock, AnUnsetClockNeverNeedsRedrawing) {
    SimulatorClock unset;
    NOTRIX_CHECK_FALSE(clockChanged(unset, styleFor(ClockTheme::Seconds), 0, 100000));
}

// --- golden ------------------------------------------------------------------

NOTRIX_TEST(Clock, ThemesMatchGolden) {
    const SimulatorClock clock = clockAt(unixTime(2026, 9, 11, 14, 35, 42));

    const char* names[] = {"minimal", "seconds", "date", "weekday", "secondsbar", "calendar"};
    for (int i = 0; i < kClockThemeCount; ++i) {
        ClockStyle style = styleFor(clockThemeAt(i));
        style.blinkPeriodMillis = 0;  // deterministic: colon always lit

        NOTRIX_CHECK_GOLDEN((std::string("clock-") + names[i]).c_str(), render(clock, style));
    }
}
