// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "notrix/core/Rgb.h"

namespace notrix {

class Canvas;

namespace platform {
class ISystemClock;
}

namespace apps {

/// Clock faces.
///
/// Designed for this panel rather than borrowed: 52x16 is two 7-pixel text rows,
/// which is what every layout here is built around. Independent implementations
/// of generic pixel-clock ideas — ADR 0001 permits studying other products for
/// UX, not copying them.
enum class ClockTheme : std::uint8_t {
    /// HH:MM, centred. The default, and the most legible from across a room.
    Minimal,
    /// HH:MM:SS. Fits at 39 px, so it still centres comfortably.
    Seconds,
    /// HH:MM above, DD.MM below.
    DateBelow,
    /// HH:MM above, weekday below.
    Weekday,
    /// HH:MM with a bar along the bottom filling over the minute.
    SecondsBar,
    /// A calendar glyph with the day number, beside the time.
    Calendar,
};

ClockTheme clockThemeFromName(std::string_view name) noexcept;
const char* clockThemeName(ClockTheme theme) noexcept;

/// Number of themes, for iterating the set in a settings UI.
inline constexpr int kClockThemeCount = 6;
ClockTheme clockThemeAt(int index) noexcept;

/// Field order in a rendered date. Not a preference — 31/12 and 12/31 are the
/// same date read two ways, and getting it wrong is a correctness problem for
/// whoever is looking at the panel.
enum class DateOrder : std::uint8_t {
    DayMonthYear,
    MonthDayYear,
    YearMonthDay,
};

enum class DateSeparator : std::uint8_t { Dot, Slash, Dash };

/// Hiding the year leaves room on a 52-pixel panel, which is why it is a
/// three-way choice rather than a toggle.
enum class DateYear : std::uint8_t { Hidden, TwoDigit, FourDigit };

DateOrder dateOrderFromName(std::string_view name) noexcept;
const char* dateOrderName(DateOrder order) noexcept;
DateSeparator dateSeparatorFromName(std::string_view name) noexcept;
const char* dateSeparatorName(DateSeparator separator) noexcept;
DateYear dateYearFromName(std::string_view name) noexcept;
const char* dateYearName(DateYear year) noexcept;

struct ClockStyle {
    ClockTheme theme = ClockTheme::Minimal;
    bool twentyFourHour = true;

    /// Show 07:05 rather than 7:05. The blanked slot is still reserved either
    /// way, so the digits do not move at ten o'clock.
    bool leadingZero = true;

    /// 12-hour clock only. Suppressed by themes that show seconds, which have no
    /// room left for it.
    bool showAmPm = false;

    Rgb color = colors::kWhite;
    /// Used for the colon, the calendar header and the seconds bar.
    Rgb accentColor = rgb(0, 190, 255);
    /// Date text in the themes that show one.
    Rgb dateColor = rgb(0, 190, 255);

    DateOrder dateOrder = DateOrder::DayMonthYear;
    DateSeparator dateSeparator = DateSeparator::Dot;
    DateYear dateYear = DateYear::Hidden;

    /// Colon blink period. Zero holds it lit — the blink is the only moving part
    /// on an otherwise static face, and some people find it distracting.
    std::uint32_t blinkPeriodMillis = 1000;

    /// Offset applied to UTC for display, in seconds.
    ///
    /// Here rather than read from ISystemClock, because a time zone is a user's
    /// preference and not a fact about the hardware. ISystemClock::
    /// utcOffsetSeconds() describes what the *platform* believes, which on a
    /// device carrying no tzdata is nothing at all — Tc002Clock honestly
    /// returns zero. Taking the offset from the clock meant the stored setting
    /// was validated, persisted, and read back by the API while changing
    /// nothing on the panel.
    int utcOffsetSeconds = 0;
};

/// Local wall-clock time, drawn in the configured theme.
///
/// When the wall clock has not been set this draws `--:--` rather than a
/// plausible-looking wrong time. A device that boots before NTP and confidently
/// shows 01:00 is worse than one that admits it does not know yet (§40).
void renderClock(Canvas& canvas,
                 const platform::ISystemClock& clock,
                 const ClockStyle& style = ClockStyle{});

/// True when the face would differ between these two moments, so a static
/// minute does not force a redraw every frame.
bool clockChanged(const platform::ISystemClock& clock,
                  const ClockStyle& style,
                  std::uint64_t previousMillis,
                  std::uint64_t nowMillis);

// --- calendar ----------------------------------------------------------------

struct CivilDate {
    int year = 1970;
    int month = 1;  ///< 1-12
    int day = 1;    ///< 1-31
};

/// Convert days since 1970-01-01 to a proleptic Gregorian date.
///
/// Implements the well-known days-to-civil algorithm: shift the era so that
/// March starts the year, which makes the leap day the last day and removes
/// every special case for February. Correct for any date this device will ever
/// show, and for a few hundred thousand years either side.
CivilDate civilFromDays(std::int64_t days) noexcept;

/// 0 = Sunday. 1970-01-01 was a Thursday, which is where the +4 comes from.
int weekdayFromDays(std::int64_t days) noexcept;

/// Three-letter uppercase abbreviation, "SUN" through "SAT".
const char* weekdayName(int weekday) noexcept;

}  // namespace apps
}  // namespace notrix
