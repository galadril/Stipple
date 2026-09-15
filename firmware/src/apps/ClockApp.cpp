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

LocalTime localTimeOf(const platform::ISystemClock& clock) noexcept {
    const std::int64_t local = clock.unixSeconds() + clock.utcOffsetSeconds();

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

    drawTwoDigits(canvas, x, y, hours, style.color, !style.twentyFourHour);
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
/// number inside. Eleven wide, which is exactly two digits plus their gap.
void drawCalendarIcon(Canvas& canvas, int x, int y, int day, const ClockStyle& style) {
    constexpr int kIconWidth = kPairWidth;  // 11
    constexpr int kIconHeight = 14;

    canvas.fillRect(Rect{x, y, kIconWidth, 3}, style.accentColor);
    canvas.rect(Rect{x, y, kIconWidth, kIconHeight}, style.accentColor);
    drawTwoDigits(canvas, x, y + 5, day, style.color);
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

void renderClock(Canvas& canvas, const platform::ISystemClock& clock, const ClockStyle& style) {
    if (!clock.wallClockValid()) {
        // Honest about not knowing, rather than confidently wrong.
        drawCentredText(canvas, "--:--", (Framebuffer::kHeight - 7) / 2, style.color);
        return;
    }

    const LocalTime time = localTimeOf(clock);
    const bool colonLit = colonIsLit(clock, style);

    switch (style.theme) {
        case ClockTheme::Minimal: {
            drawHourMinute(canvas, centreX(kTimeWidth), (Framebuffer::kHeight - 7) / 2, time,
                           style, colonLit);
            break;
        }

        case ClockTheme::Seconds: {
            drawHourMinuteSecond(canvas, centreX(kTimeWithSecondsWidth),
                                 (Framebuffer::kHeight - 7) / 2, time, style, colonLit);
            break;
        }

        case ClockTheme::DateBelow: {
            const CivilDate date = civilFromDays(time.days);
            drawHourMinute(canvas, centreX(kTimeWidth), 0, time, style, colonLit);

            // DD.MM in fixed slots, so the date does not jitter either.
            const int dateWidth = kPairWidth + kGap + kColonWidth + kGap + kPairWidth;
            const int x = centreX(dateWidth);
            drawTwoDigits(canvas, x, 9, date.day, style.accentColor);
            drawGlyphAt(canvas, x + kPairWidth + kGap, 9 + 0, '.', style.accentColor);
            drawTwoDigits(canvas, x + kPairWidth + kSeparatorWidth, 9, date.month,
                          style.accentColor);
            break;
        }

        case ClockTheme::Weekday: {
            drawHourMinute(canvas, centreX(kTimeWidth), 0, time, style, colonLit);
            drawCentredText(canvas, weekdayName(weekdayFromDays(time.days)), 9,
                            style.accentColor);
            break;
        }

        case ClockTheme::SecondsBar: {
            drawHourMinute(canvas, centreX(kTimeWidth), 2, time, style, colonLit);

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
            constexpr int kIconWidth = kPairWidth;
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
