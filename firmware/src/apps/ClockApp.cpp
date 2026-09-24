// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/ClockApp.h"

#include "notrix/graphics/Canvas.h"
#include "notrix/platform/Clock.h"
#include "notrix/text/Text.h"

namespace notrix {
namespace apps {
namespace {

constexpr std::int64_t kSecondsPerDay = 86400;

/// Every glyph in the 5x7 face is five wide, which is what makes a fixed-slot
/// layout possible.
constexpr int kDigitWidth = 5;
constexpr int kColonWidth = 1;
constexpr int kGap = 1;

struct LocalTime {
    std::int64_t days = 0;  ///< days since the epoch, local
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
};

LocalTime localTimeOf(const platform::ISystemClock& clock, int utcOffsetSeconds) noexcept {
    const std::int64_t local = clock.unixSeconds() + utcOffsetSeconds;

    // Floor division, not truncation: before 1970 a truncating divide would land
    // on the wrong day.
    std::int64_t days = local / kSecondsPerDay;
    std::int64_t secondsOfDay = local % kSecondsPerDay;
    if (secondsOfDay < 0) {
        secondsOfDay += kSecondsPerDay;
        --days;
    }

    LocalTime time;
    time.days = days;
    time.hours = static_cast<int>(secondsOfDay / 3600);
    time.minutes = static_cast<int>((secondsOfDay % 3600) / 60);
    time.seconds = static_cast<int>(secondsOfDay % 60);
    return time;
}

char separatorChar(DateSeparator separator) noexcept {
    switch (separator) {
        case DateSeparator::Slash: return '/';
        case DateSeparator::Dash: return '-';
        case DateSeparator::Dot: break;
    }
    return '.';
}

int displayHours(const LocalTime& time, bool twentyFourHour) noexcept {
    if (twentyFourHour) {
        return time.hours;
    }
    const int hours = time.hours % 12;
    return hours == 0 ? 12 : hours;
}

bool colonIsLit(const platform::ISystemClock& clock, const ClockStyle& style) noexcept {
    if (style.blinkPeriodMillis == 0) {
        return true;
    }
    const std::uint64_t phase = clock.monotonicMillis() % style.blinkPeriodMillis;
    return phase * 2u < style.blinkPeriodMillis;
}

text::TextStyle baseStyle(Rgb color) noexcept {
    text::TextStyle style;
    style.font = &text::font5x7();
    style.color = color;
    style.hAlign = text::HAlign::Left;
    style.vAlign = text::VAlign::Top;
    return style;
}

/// Draw one glyph at an exact position, bypassing alignment entirely.
void drawGlyphAt(Canvas& canvas, int x, int y, char c, Rgb color) {
    const char text[2] = {c, '\0'};
    text::drawLine(canvas, std::string_view(text, 1), x, y, text::font5x7(), color);
}

/// Two digits in fixed slots. `blankLeadingZero` leaves the tens slot empty
/// rather than narrowing the field, so 12-hour times do not shift at 10 o'clock.
void drawTwoDigits(Canvas& canvas, int x, int y, int value, Rgb color,
                   bool blankLeadingZero = false) {
    const int tens = (value / 10) % 10;
    const int units = value % 10;

    if (!(blankLeadingZero && tens == 0)) {
        drawGlyphAt(canvas, x, y, static_cast<char>('0' + tens), color);
    }
    drawGlyphAt(canvas, x + kDigitWidth + kGap, y, static_cast<char>('0' + units), color);
}

constexpr int kPairWidth = kDigitWidth * 2 + kGap;                 // 11
/// Frame thickness for icons that contain content. DESIGN.md section 4: the
/// interior is width - 2 * this, and the interior is what has to fit.
constexpr int kFrameBorder = 1;
constexpr int kSeparatorWidth = kGap + kColonWidth + kGap;         // 3
constexpr int kTimeWidth = kPairWidth * 2 + kSeparatorWidth;       // 25
constexpr int kTimeWithSecondsWidth = kPairWidth * 3 + kSeparatorWidth * 2;  // 39

/// HH:MM in fixed slots.
///
/// The colon occupies a reserved slot whether or not it is lit. Blinking it by
/// swapping in a space would shift the minutes by a pixel twice a second,
/// because the space glyph is two wide and the colon is one.
void drawHourMinute(Canvas& canvas, int x, int y, const LocalTime& time,
                    const ClockStyle& style, bool colonLit) {
    const int hours = displayHours(time, style.twentyFourHour);

    // A blanked leading zero leaves its slot empty rather than narrowing the
    // field, so the time never slides sideways at ten o'clock.
    drawTwoDigits(canvas, x, y, hours, style.color, !style.leadingZero);
    if (colonLit) {
        drawGlyphAt(canvas, x + kPairWidth + kGap, y, ':', style.accentColor);
    }
    drawTwoDigits(canvas, x + kPairWidth + kSeparatorWidth, y, time.minutes, style.color);
}

void drawHourMinuteSecond(Canvas& canvas, int x, int y, const LocalTime& time,
                          const ClockStyle& style, bool colonLit) {
    drawHourMinute(canvas, x, y, time, style, colonLit);

    const int secondsX = x + kPairWidth * 2 + kSeparatorWidth;
    if (colonLit) {
        drawGlyphAt(canvas, secondsX + kGap, y, ':', style.accentColor);
    }
    drawTwoDigits(canvas, secondsX + kSeparatorWidth, y, time.seconds, style.color);
}

int centreX(int contentWidth) noexcept {
    return (Framebuffer::kWidth - contentWidth) / 2;
}

void drawCentredText(Canvas& canvas, std::string_view text, int y, Rgb color) {
    text::TextStyle style = baseStyle(color);
    style.hAlign = text::HAlign::Center;
    text::draw(canvas, text, Rect{0, y, Framebuffer::kWidth, 7}, style);
}

/// A calendar glyph: a filled header bar over an outlined body, with the day
/// number inside. Thirteen wide, so the 11-wide digit pair fits the interior.
void drawCalendarIcon(Canvas& canvas, int x, int y, int day, const ClockStyle& style) {
    // 13 x 14, and the 13 is the whole point (DESIGN.md section 4).
    //
    // This was kPairWidth (11) with a 1px border, leaving a 9-column interior
    // for an 11-column digit pair - so the day overlapped both borders, on
    // every face, for every date. Content inside a frame is measured against
    // the interior, never against the frame.
    constexpr int kIconWidth = kFrameBorder * 2 + kPairWidth;  // 13
    constexpr int kIconHeight = 14;
    constexpr int kHeaderRows = 3;

    // Header band first, then the outline over it, so the corners stay square.
    canvas.fillRect(Rect{x, y, kIconWidth, kHeaderRows}, style.accentColor);
    canvas.rect(Rect{x, y, kIconWidth, kIconHeight}, style.accentColor);

    // Inset by the border, below the header band.
    drawTwoDigits(canvas, x + kFrameBorder, y + kHeaderRows + 1, day, style.color);
}

}  // namespace

// --- calendar ----------------------------------------------------------------

CivilDate civilFromDays(std::int64_t days) noexcept {
    // Shift the epoch to 0000-03-01 so that March begins the year. The leap day
    // then falls at the end, which removes every February special case.
    days += 719468;

    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t dayOfEra = days - era * 146097;  // [0, 146096]
    const std::int64_t yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;  // [0, 399]

    const std::int64_t year = yearOfEra + era * 400;
    const std::int64_t dayOfYear =
        dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);  // [0, 365]
    const std::int64_t shiftedMonth = (5 * dayOfYear + 2) / 153;        // [0, 11], March = 0

    CivilDate date;
    date.day = static_cast<int>(dayOfYear - (153 * shiftedMonth + 2) / 5 + 1);
    date.month = static_cast<int>(shiftedMonth < 10 ? shiftedMonth + 3 : shiftedMonth - 9);
    date.year = static_cast<int>(year + (date.month <= 2 ? 1 : 0));
    return date;
}

int weekdayFromDays(std::int64_t days) noexcept {
    // 1970-01-01 was a Thursday, which is index 4 counting from Sunday.
    const std::int64_t weekday = (days + 4) % 7;
    return static_cast<int>(weekday < 0 ? weekday + 7 : weekday);
}

const char* weekdayName(int weekday) noexcept {
    static const char* const kNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (weekday < 0 || weekday > 6) {
        return "---";
    }
    return kNames[weekday];
}

// --- themes ------------------------------------------------------------------

ClockTheme clockThemeFromName(std::string_view name) noexcept {
    if (name == "seconds") return ClockTheme::Seconds;
    if (name == "date") return ClockTheme::DateBelow;
    if (name == "weekday") return ClockTheme::Weekday;
    if (name == "secondsbar") return ClockTheme::SecondsBar;
    if (name == "calendar") return ClockTheme::Calendar;
    return ClockTheme::Minimal;
}

const char* clockThemeName(ClockTheme theme) noexcept {
    switch (theme) {
        case ClockTheme::Seconds: return "seconds";
        case ClockTheme::DateBelow: return "date";
        case ClockTheme::Weekday: return "weekday";
        case ClockTheme::SecondsBar: return "secondsbar";
        case ClockTheme::Calendar: return "calendar";
        case ClockTheme::Minimal: break;
    }
    return "minimal";
}

DateOrder dateOrderFromName(std::string_view name) noexcept {
    if (name == "monthDayYear") return DateOrder::MonthDayYear;
    if (name == "yearMonthDay") return DateOrder::YearMonthDay;
    return DateOrder::DayMonthYear;
}

const char* dateOrderName(DateOrder order) noexcept {
    switch (order) {
        case DateOrder::MonthDayYear: return "monthDayYear";
        case DateOrder::YearMonthDay: return "yearMonthDay";
        case DateOrder::DayMonthYear: break;
    }
    return "dayMonthYear";
}

DateSeparator dateSeparatorFromName(std::string_view name) noexcept {
    if (name == "slash") return DateSeparator::Slash;
    if (name == "dash") return DateSeparator::Dash;
    return DateSeparator::Dot;
}

const char* dateSeparatorName(DateSeparator separator) noexcept {
    switch (separator) {
        case DateSeparator::Slash: return "slash";
        case DateSeparator::Dash: return "dash";
        case DateSeparator::Dot: break;
    }
    return "dot";
}

DateYear dateYearFromName(std::string_view name) noexcept {
    if (name == "twoDigit") return DateYear::TwoDigit;
    if (name == "fourDigit") return DateYear::FourDigit;
    return DateYear::Hidden;
}

const char* dateYearName(DateYear year) noexcept {
    switch (year) {
        case DateYear::TwoDigit: return "twoDigit";
        case DateYear::FourDigit: return "fourDigit";
        case DateYear::Hidden: break;
    }
    return "none";
}

ClockTheme clockThemeAt(int index) noexcept {
    switch (index) {
        case 1: return ClockTheme::Seconds;
        case 2: return ClockTheme::DateBelow;
        case 3: return ClockTheme::Weekday;
        case 4: return ClockTheme::SecondsBar;
        case 5: return ClockTheme::Calendar;
        default: return ClockTheme::Minimal;
    }
}

// --- rendering ---------------------------------------------------------------

namespace {

void drawDate(Canvas& canvas, const CivilDate& date, int y, const ClockStyle& style) {
    const char separator = separatorChar(style.dateSeparator);
    const int twoDigitYear = ((date.year % 100) + 100) % 100;

    // Build the field order once, so each layout is described rather than
    // special-cased three times over.
    int values[3] = {date.day, date.month, twoDigitYear};
    bool fourDigit[3] = {false, false, false};
    int count = 2;

    switch (style.dateOrder) {
        case DateOrder::MonthDayYear:
            values[0] = date.month;
            values[1] = date.day;
            break;
        case DateOrder::YearMonthDay:
            values[0] = twoDigitYear;
            values[1] = date.month;
            values[2] = date.day;
            fourDigit[0] = style.dateYear == DateYear::FourDigit;
            break;
        case DateOrder::DayMonthYear:
            break;
    }

    if (style.dateYear != DateYear::Hidden) {
        count = 3;
        if (style.dateOrder != DateOrder::YearMonthDay) {
            fourDigit[2] = style.dateYear == DateYear::FourDigit;
            if (fourDigit[2]) {
                values[2] = date.year;
            }
        } else if (fourDigit[0]) {
            values[0] = date.year;
        }
    } else if (style.dateOrder == DateOrder::YearMonthDay) {
        // No year to lead with, so fall back to month then day.
        values[0] = date.month;
        values[1] = date.day;
    }

    int width = 0;
    for (int i = 0; i < count; ++i) {
        width += fourDigit[i] ? kPairWidth * 2 + kGap : kPairWidth;
        if (i + 1 < count) {
            width += kSeparatorWidth;
        }
    }

    int x = centreX(width);
    for (int i = 0; i < count; ++i) {
        if (fourDigit[i]) {
            drawTwoDigits(canvas, x, y, (values[i] / 100) % 100, style.dateColor);
            drawTwoDigits(canvas, x + kPairWidth + kGap, y, values[i] % 100, style.dateColor);
            x += kPairWidth * 2 + kGap;
        } else {
            drawTwoDigits(canvas, x, y, values[i], style.dateColor);
            x += kPairWidth;
        }
        if (i + 1 < count) {
            drawGlyphAt(canvas, x + kGap, y, separator, style.dateColor);
            x += kSeparatorWidth;
        }
    }
}

/// "AM"/"PM" beside the time. Only meaningful on a 12-hour clock, and only where
/// the layout has room left.
void drawMeridiem(Canvas& canvas, int x, int y, const LocalTime& time,
                  const ClockStyle& style) {
    const char letter = time.hours < 12 ? 'A' : 'P';
    drawGlyphAt(canvas, x, y, letter, style.accentColor);
    drawGlyphAt(canvas, x + kDigitWidth + kGap, y, 'M', style.accentColor);
}

}  // namespace

void renderClock(Canvas& canvas, const platform::ISystemClock& clock, const ClockStyle& style) {
    if (!clock.wallClockValid()) {
        // Honest about not knowing, rather than confidently wrong.
        drawCentredText(canvas, "--:--", (Framebuffer::kHeight - 7) / 2, style.color);
        return;
    }

    const LocalTime time = localTimeOf(clock, style.utcOffsetSeconds);
    const bool colonLit = colonIsLit(clock, style);

    // AM/PM used to be handled inside the Minimal case and nowhere else, so the
    // setting silently did nothing on four of the six faces. It applies wherever
    // there is room for the extra 14 columns: HH:MM is 25, and 25 + 14 = 39,
    // which fits. HH:MM:SS is already 39 and the calendar face is 41, so both
    // stay suppressed - there is no honest way to show it on either.
    const bool meridiem = style.showAmPm && !style.twentyFourHour;
    const int meridiemWidth = kPairWidth + kSeparatorWidth;
    const int timeBlockWidth = kTimeWidth + (meridiem ? meridiemWidth : 0);

    switch (style.theme) {
        case ClockTheme::Minimal: {
            const int x = centreX(timeBlockWidth);
            const int y = (Framebuffer::kHeight - 7) / 2;

            drawHourMinute(canvas, x, y, time, style, colonLit);
            if (meridiem) {
                drawMeridiem(canvas, x + kTimeWidth + kSeparatorWidth, y, time, style);
            }
            break;
        }

        case ClockTheme::Seconds: {
            drawHourMinuteSecond(canvas, centreX(kTimeWithSecondsWidth),
                                 (Framebuffer::kHeight - 7) / 2, time, style, colonLit);
            break;
        }

        case ClockTheme::DateBelow: {
            const int x = centreX(timeBlockWidth);
            drawHourMinute(canvas, x, 0, time, style, colonLit);
            if (meridiem) {
                drawMeridiem(canvas, x + kTimeWidth + kSeparatorWidth, 0, time, style);
            }
            drawDate(canvas, civilFromDays(time.days), 9, style);
            break;
        }

        case ClockTheme::Weekday: {
            const int x = centreX(timeBlockWidth);
            drawHourMinute(canvas, x, 0, time, style, colonLit);
            if (meridiem) {
                drawMeridiem(canvas, x + kTimeWidth + kSeparatorWidth, 0, time, style);
            }
            drawCentredText(canvas, weekdayName(weekdayFromDays(time.days)), 9,
                            style.dateColor);
            break;
        }

        case ClockTheme::SecondsBar: {
            const int barX = centreX(timeBlockWidth);
            drawHourMinute(canvas, barX, 2, time, style, colonLit);
            if (meridiem) {
                drawMeridiem(canvas, barX + kTimeWidth + kSeparatorWidth, 2, time, style);
            }

            // A bar filling over the minute. Rounded up so it is visible from
            // the first second rather than staying dark for most of it.
            const int filled = (time.seconds * Framebuffer::kWidth + 59) / 60;
            canvas.fillRect(Rect{0, Framebuffer::kHeight - 2, Framebuffer::kWidth, 2},
                            rgb(18, 18, 18));
            if (filled > 0) {
                canvas.fillRect(Rect{0, Framebuffer::kHeight - 2, filled, 2}, style.accentColor);
            }
            break;
        }

        case ClockTheme::Calendar: {
            const CivilDate date = civilFromDays(time.days);
            constexpr int kIconWidth = kFrameBorder * 2 + kPairWidth;  // 13
            constexpr int kSpacing = 3;
            const int total = kIconWidth + kSpacing + kTimeWidth;
            const int x = centreX(total);

            drawCalendarIcon(canvas, x, 1, date.day, style);
            drawHourMinute(canvas, x + kIconWidth + kSpacing, (Framebuffer::kHeight - 7) / 2,
                           time, style, colonLit);
            break;
        }
    }
}

bool clockChanged(const platform::ISystemClock& clock,
                  const ClockStyle& style,
                  std::uint64_t previousMillis,
                  std::uint64_t nowMillis) {
    if (!clock.wallClockValid()) {
        return false;  // "--:--" never changes
    }

    // Themes showing seconds change every second regardless of the blink.
    const bool showsSeconds =
        style.theme == ClockTheme::Seconds || style.theme == ClockTheme::SecondsBar;
    if (showsSeconds && (nowMillis / 1000u) != (previousMillis / 1000u)) {
        return true;
    }

    if (style.blinkPeriodMillis == 0) {
        // Only the minute matters; checking once a second catches the rollover
        // within a second of it happening.
        return (nowMillis / 1000u) != (previousMillis / 1000u);
    }

    const std::uint64_t half = style.blinkPeriodMillis / 2u;
    if (half == 0) {
        return true;
    }
    return (nowMillis / half) != (previousMillis / half);
}

}  // namespace apps
}  // namespace notrix
