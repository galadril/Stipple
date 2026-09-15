// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/ClockApp.h"

#include "notrix/graphics/Canvas.h"
#include "notrix/platform/Clock.h"
#include "notrix/text/Text.h"

namespace notrix {
namespace apps {
namespace {

constexpr std::int64_t kSecondsPerDay = 86400;

struct LocalTime {
    int hours = 0;
    int minutes = 0;
};

LocalTime localTimeOf(const platform::ISystemClock& clock) noexcept {
    const std::int64_t local = clock.unixSeconds() + clock.utcOffsetSeconds();
    // Positive modulo: a negative offset before the epoch must not produce a
    // negative hour.
    const std::int64_t secondsOfDay = ((local % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay;

    LocalTime time;
    time.hours = static_cast<int>(secondsOfDay / 3600);
    time.minutes = static_cast<int>((secondsOfDay % 3600) / 60);
    return time;
}

/// Writes "HH:MM" (or "HH MM" while the colon is blinked off) into `out`, which
/// must hold at least six bytes. No allocation: this runs every frame.
void formatTime(char* out, const LocalTime& time, bool twentyFourHour, bool colonLit) noexcept {
    int hours = time.hours;
    if (!twentyFourHour) {
        hours %= 12;
        if (hours == 0) {
            hours = 12;
        }
    }

    out[0] = static_cast<char>('0' + (hours / 10));
    out[1] = static_cast<char>('0' + (hours % 10));
    out[2] = colonLit ? ':' : ' ';
    out[3] = static_cast<char>('0' + (time.minutes / 10));
    out[4] = static_cast<char>('0' + (time.minutes % 10));
    out[5] = '\0';
}

bool colonIsLit(const platform::ISystemClock& clock, const ClockStyle& style) noexcept {
    if (style.blinkPeriodMillis == 0) {
        return true;
    }
    const std::uint64_t phase = clock.monotonicMillis() % style.blinkPeriodMillis;
    return phase * 2u < style.blinkPeriodMillis;
}

}  // namespace

void renderClock(Canvas& canvas, const platform::ISystemClock& clock, const ClockStyle& style) {
    text::TextStyle textStyle;
    textStyle.font = &text::font5x7();
    textStyle.color = style.color;
    textStyle.hAlign = text::HAlign::Center;
    textStyle.vAlign = text::VAlign::Middle;

    const Rect panel = Framebuffer::bounds();

    if (!clock.wallClockValid()) {
        // Honest about not knowing, rather than confidently wrong.
        text::draw(canvas, "--:--", panel, textStyle);
        return;
    }

    char buffer[6];
    formatTime(buffer, localTimeOf(clock), style.twentyFourHour, colonIsLit(clock, style));
    text::draw(canvas, std::string_view(buffer, 5), panel, textStyle);
}

bool clockChanged(const platform::ISystemClock& clock,
                  const ClockStyle& style,
                  std::uint64_t previousMillis,
                  std::uint64_t nowMillis) {
    if (!clock.wallClockValid()) {
        return false;  // "--:--" never changes
    }
    if (style.blinkPeriodMillis == 0) {
        // Only the minute matters; at one redraw per second the minute rollover
        // is caught within a second of happening.
        return (nowMillis / 1000u) != (previousMillis / 1000u);
    }
    // The colon blinks, so a redraw is needed whenever its half-period flips.
    const std::uint64_t half = style.blinkPeriodMillis / 2u;
    if (half == 0) {
        return true;
    }
    return (nowMillis / half) != (previousMillis / half);
}

}  // namespace apps
}  // namespace notrix
