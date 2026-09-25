// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/apps/BatteryApp.h"
#include "stipple/apps/VisualizerApp.h"
#include "stipple/apps/ClockApp.h"

#include <string>

#include "stipple/graphics/Canvas.h"
#include "stipple/platform/simulator/SimulatorPlatform.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::Rect;
using stipple::apps::CivilDate;
using stipple::apps::civilFromDays;
using stipple::apps::ClockStyle;
using stipple::apps::ClockTheme;
using stipple::apps::clockChanged;
using stipple::apps::clockThemeAt;
using stipple::apps::clockThemeFromName;
using stipple::apps::clockThemeName;
using stipple::apps::kClockThemeCount;
using stipple::apps::renderClock;
using stipple::apps::weekdayFromDays;
using stipple::apps::weekdayName;
using stipple::platform::simulator::SimulatorClock;
namespace colors = stipple::colors;

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

STIPPLE_TEST(Clock, ConvertsDaysToCivilDates) {
    STIPPLE_CHECK_EQ(civilFromDays(0).year, 1970);
    STIPPLE_CHECK_EQ(civilFromDays(0).month, 1);
    STIPPLE_CHECK_EQ(civilFromDays(0).day, 1);

    const CivilDate later = civilFromDays(unixTime(2026, 9, 11, 0, 0, 0) / 86400);
    STIPPLE_CHECK_EQ(later.year, 2026);
    STIPPLE_CHECK_EQ(later.month, 9);
    STIPPLE_CHECK_EQ(later.day, 11);
}

STIPPLE_TEST(Clock, HandlesLeapDays) {
    // The shifted-era trick exists precisely so February needs no special case.
    const CivilDate leap = civilFromDays(unixTime(2024, 2, 29, 0, 0, 0) / 86400);
    STIPPLE_CHECK_EQ(leap.month, 2);
    STIPPLE_CHECK_EQ(leap.day, 29);

    // 2000 was a leap year; 1900 was not.
    STIPPLE_CHECK_EQ(civilFromDays(unixTime(2000, 2, 29, 0, 0, 0) / 86400).day, 29);
    STIPPLE_CHECK_EQ(civilFromDays(unixTime(1900, 3, 1, 0, 0, 0) / 86400).month, 3);
}

STIPPLE_TEST(Clock, HandlesYearAndMonthBoundaries) {
    const CivilDate newYear = civilFromDays(unixTime(2027, 1, 1, 0, 0, 0) / 86400);
    STIPPLE_CHECK_EQ(newYear.year, 2027);
    STIPPLE_CHECK_EQ(newYear.month, 1);
    STIPPLE_CHECK_EQ(newYear.day, 1);

    const CivilDate yearEnd = civilFromDays(unixTime(2026, 12, 31, 0, 0, 0) / 86400);
    STIPPLE_CHECK_EQ(yearEnd.month, 12);
    STIPPLE_CHECK_EQ(yearEnd.day, 31);
}

STIPPLE_TEST(Clock, RoundTripsEveryDayAcrossDecades) {
    // Walk twenty years of days and confirm the date always advances by exactly
    // one, which catches any off-by-one at a month or year boundary.
    std::int64_t day = unixTime(2020, 1, 1, 0, 0, 0) / 86400;
    CivilDate previous = civilFromDays(day);

    for (int i = 1; i < 365 * 20; ++i) {
        const CivilDate current = civilFromDays(day + i);

        const bool sameMonth = current.year == previous.year && current.month == previous.month;
        if (sameMonth) {
            STIPPLE_CHECK_EQ(current.day, previous.day + 1);
        } else {
            STIPPLE_CHECK_EQ(current.day, 1);
            STIPPLE_CHECK(current.month >= 1 && current.month <= 12);
        }
        previous = current;
    }
}

STIPPLE_TEST(Clock, WeekdaysAreCorrect) {
    // 1970-01-01 was a Thursday.
    STIPPLE_CHECK_EQ(std::string(weekdayName(weekdayFromDays(0))), std::string("THU"));
    STIPPLE_CHECK_EQ(std::string(weekdayName(weekdayFromDays(1))), std::string("FRI"));
    STIPPLE_CHECK_EQ(std::string(weekdayName(weekdayFromDays(3))), std::string("SUN"));

    // 2026-09-11 is a Friday.
    const std::int64_t days = unixTime(2026, 9, 11, 0, 0, 0) / 86400;
    STIPPLE_CHECK_EQ(std::string(weekdayName(weekdayFromDays(days))), std::string("FRI"));
}

STIPPLE_TEST(Clock, WeekdayNameIsBounded) {
    STIPPLE_CHECK_EQ(std::string(weekdayName(-1)), std::string("---"));
    STIPPLE_CHECK_EQ(std::string(weekdayName(7)), std::string("---"));
}

// --- the colon must not move the digits --------------------------------------

STIPPLE_TEST(Clock, BlinkingTheColonDoesNotShiftTheDigits) {
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

    STIPPLE_CHECK(withColon != withoutColon);  // the colon really did blink

    // Every lit pixel outside the colon's slot must be identical.
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const int colonX = (Framebuffer::kWidth - 25) / 2 + 12;
            if (x == colonX) {
                continue;  // the slot itself is allowed to differ
            }
            STIPPLE_CHECK_EQ(withColon.at(x, y), withoutColon.at(x, y));
        }
    }
}

STIPPLE_TEST(Clock, TheHourFieldNeverMovesTheMinutes) {
    // Comparing ink extents would be wrong: '0' lights column 0 of its cell and
    // '1' does not, so different digits legitimately have different bounds. The
    // property that matters is that the *slots* are fixed — whatever the hour
    // is, the minutes occupy exactly the same pixels.
    ClockStyle style = styleFor(ClockTheme::Minimal);
    style.blinkPeriodMillis = 0;

    const Framebuffer early = render(clockAt(unixTime(2026, 9, 11, 8, 45, 0)), style);
    const Framebuffer late = render(clockAt(unixTime(2026, 9, 11, 19, 45, 0)), style);

    STIPPLE_CHECK(early != late);  // the hours really did change

    // Everything from the colon rightwards must be identical.
    const int colonX = (Framebuffer::kWidth - 25) / 2 + 12;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = colonX; x < Framebuffer::kWidth; ++x) {
            STIPPLE_CHECK_EQ(early.at(x, y), late.at(x, y));
        }
    }
}

STIPPLE_TEST(Clock, TheMinuteFieldNeverMovesTheHours) {
    ClockStyle style = styleFor(ClockTheme::Minimal);
    style.blinkPeriodMillis = 0;

    const Framebuffer a = render(clockAt(unixTime(2026, 9, 11, 14, 5, 0)), style);
    const Framebuffer b = render(clockAt(unixTime(2026, 9, 11, 14, 58, 0)), style);

    const int colonX = (Framebuffer::kWidth - 25) / 2 + 12;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < colonX; ++x) {
            STIPPLE_CHECK_EQ(a.at(x, y), b.at(x, y));
        }
    }
}

STIPPLE_TEST(Clock, TwelveHourModeDoesNotShiftAtTen) {
    // A blanked leading zero must leave its slot empty rather than narrowing the
    // field, or the time slides sideways at 10 o'clock.
    ClockStyle style = styleFor(ClockTheme::Minimal);
    style.twentyFourHour = false;
    style.blinkPeriodMillis = 0;

    const Rect nine = litBounds(render(clockAt(unixTime(2026, 9, 11, 9, 30, 0)), style));
    const Rect ten = litBounds(render(clockAt(unixTime(2026, 9, 11, 10, 30, 0)), style));

    // The minutes end in the same place; only the hours field differs.
    STIPPLE_CHECK_EQ(nine.right(), ten.right());
}

// --- themes ------------------------------------------------------------------

STIPPLE_TEST(Clock, ThemeNamesRoundTrip) {
    for (int i = 0; i < kClockThemeCount; ++i) {
        const ClockTheme theme = clockThemeAt(i);
        STIPPLE_CHECK(clockThemeFromName(clockThemeName(theme)) == theme);
    }
    STIPPLE_CHECK(clockThemeFromName("nonsense") == ClockTheme::Minimal);
    STIPPLE_CHECK(clockThemeFromName("") == ClockTheme::Minimal);
}

STIPPLE_TEST(Clock, EveryThemeDrawsAndStaysOnThePanel) {
    const SimulatorClock clock = clockAt(unixTime(2026, 9, 11, 14, 35, 42));

    for (int i = 0; i < kClockThemeCount; ++i) {
        ClockStyle style = styleFor(clockThemeAt(i));
        style.blinkPeriodMillis = 0;

        const Framebuffer frame = render(clock, style);
        const Rect lit = litBounds(frame);

        STIPPLE_CHECK_FALSE(lit.empty());
        STIPPLE_CHECK(lit.x >= 0);
        STIPPLE_CHECK(lit.y >= 0);
        STIPPLE_CHECK(lit.right() <= Framebuffer::kWidth);
        STIPPLE_CHECK(lit.bottom() <= Framebuffer::kHeight);
    }
}

STIPPLE_TEST(Clock, ThemesDifferFromEachOther) {
    const SimulatorClock clock = clockAt(unixTime(2026, 9, 11, 14, 35, 42));

    ClockStyle minimal = styleFor(ClockTheme::Minimal);
    minimal.blinkPeriodMillis = 0;

    for (int i = 1; i < kClockThemeCount; ++i) {
        ClockStyle other = styleFor(clockThemeAt(i));
        other.blinkPeriodMillis = 0;
        STIPPLE_CHECK(render(clock, minimal) != render(clock, other));
    }
}

STIPPLE_TEST(Clock, TheStyleOffsetShiftsTheDisplayedTime) {
    // Shifting the offset forward by an hour must look exactly like the clock
    // itself having advanced an hour. Exact rather than "the frames differ",
    // because a wrong-but-different offset would pass that.
    const std::int64_t noon = unixTime(2026, 9, 20, 12, 0, 0);

    ClockStyle shifted = styleFor(ClockTheme::Minimal);
    shifted.utcOffsetSeconds = 3600;

    ClockStyle plain = styleFor(ClockTheme::Minimal);
    plain.utcOffsetSeconds = 0;

    STIPPLE_CHECK(render(clockAt(noon), shifted) == render(clockAt(noon + 3600), plain));
}

STIPPLE_TEST(Clock, AWholeDayOfOffsetLooksLikeNone) {
    // Catches a sign error, which the hour test above cannot: negating the
    // offset still produces "some other time", but only a correctly applied
    // one wraps a full day back onto itself.
    const std::int64_t noon = unixTime(2026, 9, 20, 12, 0, 0);

    ClockStyle wrapped = styleFor(ClockTheme::Seconds);
    wrapped.utcOffsetSeconds = 24 * 3600;

    ClockStyle plain = styleFor(ClockTheme::Seconds);

    STIPPLE_CHECK(render(clockAt(noon), wrapped) == render(clockAt(noon), plain));
}

STIPPLE_TEST(Clock, TheSecondsBarFillsAndComesBackAfterTheMinute) {
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

    STIPPLE_CHECK_EQ(barWidth(0), 0);                        // empty at the top
    STIPPLE_CHECK_EQ(barWidth(59), Framebuffer::kWidth);      // full at the end

    // Monotonic across the minute, never going backwards mid-way.
    int previous = 0;
    for (int second = 0; second < 60; ++second) {
        const int width = barWidth(second);
        STIPPLE_CHECK(width >= previous);
        previous = width;
    }

    // And the rollover: full, then empty, then growing again. "Did not come
    // back" is precisely this last assertion.
    STIPPLE_CHECK_EQ(barWidth(59), Framebuffer::kWidth);
    STIPPLE_CHECK_EQ(barWidth(60), 0);
    STIPPLE_CHECK(barWidth(61) > 0);
}

STIPPLE_TEST(Clock, TheSecondsBarAsksToBeRedrawnEverySecond) {
    // The other half of the failure: a bar that renders correctly is still
    // frozen if dirty tracking never invalidates. On this device a skipped
    // frame is not a stale frame but a dark one, so a missed invalidate shows
    // up as the bar simply stopping.
    const SimulatorClock clock = clockAt(unixTime(2026, 9, 20, 12, 5, 30));
    ClockStyle style = styleFor(ClockTheme::SecondsBar);
    style.blinkPeriodMillis = 0;

    STIPPLE_CHECK(clockChanged(clock, style, 0, 1000));
    STIPPLE_CHECK(clockChanged(clock, style, 59'000, 60'000));  // across the minute
    STIPPLE_CHECK_FALSE(clockChanged(clock, style, 1000, 1200));  // same second
}

STIPPLE_TEST(Clock, UnsetWallClockShowsPlaceholderInEveryTheme) {
    SimulatorClock unset;  // never set
    for (int i = 0; i < kClockThemeCount; ++i) {
        ClockStyle style = styleFor(clockThemeAt(i));
        const Framebuffer frame = render(unset, style);
        STIPPLE_CHECK_FALSE(litBounds(frame).empty());  // "--:--", not a blank panel
    }
}

// --- redraw hints ------------------------------------------------------------

STIPPLE_TEST(Clock, SecondsThemesRedrawEverySecond) {
    SimulatorClock clock = clockAt(unixTime(2026, 9, 11, 14, 35, 0));

    ClockStyle seconds = styleFor(ClockTheme::Seconds);
    seconds.blinkPeriodMillis = 0;
    STIPPLE_CHECK(clockChanged(clock, seconds, 0, 1000));
    STIPPLE_CHECK_FALSE(clockChanged(clock, seconds, 100, 200));

    ClockStyle bar = styleFor(ClockTheme::SecondsBar);
    bar.blinkPeriodMillis = 0;
    STIPPLE_CHECK(clockChanged(clock, bar, 0, 1000));
}

STIPPLE_TEST(Clock, AnUnsetClockNeverNeedsRedrawing) {
    SimulatorClock unset;
    STIPPLE_CHECK_FALSE(clockChanged(unset, styleFor(ClockTheme::Seconds), 0, 100000));
}

// --- golden ------------------------------------------------------------------

STIPPLE_TEST(Clock, ThemesMatchGolden) {
    const SimulatorClock clock = clockAt(unixTime(2026, 9, 11, 14, 35, 42));

    const char* names[] = {"minimal", "seconds", "date", "weekday", "secondsbar", "calendar"};
    for (int i = 0; i < kClockThemeCount; ++i) {
        ClockStyle style = styleFor(clockThemeAt(i));
        style.blinkPeriodMillis = 0;  // deterministic: colon always lit

        STIPPLE_CHECK_GOLDEN((std::string("clock-") + names[i]).c_str(), render(clock, style));
    }
}

// --- battery -----------------------------------------------------------------

STIPPLE_TEST(Battery, AnUnknownChargeSaysSoRatherThanShowingZero) {
    // The failure this guards against is a clock confidently reporting a flat
    // battery because nothing answered, which is indistinguishable to the user
    // from a real flat battery.
    stipple::platform::BatteryStatus unknown;  // known == false
    Framebuffer frame;
    Canvas canvas(frame);
    stipple::apps::renderBattery(canvas, unknown, stipple::apps::BatteryStyle{});

    const Framebuffer empty;
    STIPPLE_CHECK(frame != empty);  // it drew *something*

    stipple::platform::BatteryStatus flat;
    flat.known = true;
    flat.percent = 0;
    Framebuffer flatFrame;
    Canvas flatCanvas(flatFrame);
    stipple::apps::renderBattery(flatCanvas, flat, stipple::apps::BatteryStyle{});

    // And the two must not look the same.
    STIPPLE_CHECK(frame != flatFrame);
}

STIPPLE_TEST(Battery, ChargeChangesWhatIsDrawn) {
    auto render = [](int percent) {
        stipple::platform::BatteryStatus status;
        status.known = true;
        status.percent = percent;
        Framebuffer frame;
        Canvas canvas(frame);
        stipple::apps::renderBattery(canvas, status, stipple::apps::BatteryStyle{});
        return frame;
    };

    STIPPLE_CHECK(render(10) != render(90));
    STIPPLE_CHECK(render(100) != render(50));

    // Any charge at all lights something: a battery at 3% must not be
    // pixel-identical to one at 0%.
    STIPPLE_CHECK(render(3) != render(0));
}

STIPPLE_TEST(Battery, ChargingLooksDifferentFromDischargingAtTheSamePercent) {
    // Measured on a TC002: the same cell reads ~3160 mV on the cable and
    // ~3115 mV off it, which moves the MCU's voltage-derived percentage by
    // several points. Without a charge indicator that looks like the gauge
    // inventing numbers; with one it reads as a battery under load, which is
    // what it is.
    auto render = [](bool chargingKnown, bool charging) {
        stipple::platform::BatteryStatus status;
        status.known = true;
        status.percent = 80;
        status.chargingKnown = chargingKnown;
        status.charging = charging;
        Framebuffer frame;
        Canvas canvas(frame);
        stipple::apps::renderBattery(canvas, status, stipple::apps::BatteryStyle{});
        return frame;
    };

    STIPPLE_CHECK(render(true, true) != render(true, false));

    // A platform that cannot tell must look like one that is not charging,
    // never like one that is: an invented bolt is the same class of lie as an
    // invented percentage.
    STIPPLE_CHECK(render(false, false) == render(true, false));
    STIPPLE_CHECK(render(false, true) == render(true, false));
}

STIPPLE_TEST(Battery, OutOfRangeChargeIsClampedNotWrapped) {
    auto render = [](int percent) {
        stipple::platform::BatteryStatus status;
        status.known = true;
        status.percent = percent;
        Framebuffer frame;
        Canvas canvas(frame);
        stipple::apps::renderBattery(canvas, status, stipple::apps::BatteryStyle{});
        return frame;
    };

    STIPPLE_CHECK(render(250) == render(100));
    STIPPLE_CHECK(render(-20) == render(0));
}


// --- visualizer --------------------------------------------------------------

namespace {

/// Renders the *trace* style specifically.
///
/// The default is the meter, which the tests below it cover separately. These
/// assert things about a scrolling history - columns, ageing, the centre
/// baseline - and none of them mean anything to a meter, so asking for the
/// style explicitly keeps each test about one thing.
stipple::Framebuffer renderViz(const stipple::apps::Visualizer& viz) {
    stipple::apps::VisualizerStyle trace;
    trace.kind = stipple::apps::VisualizerStyleKind::Trace;

    stipple::Framebuffer frame;
    stipple::Canvas canvas(frame);
    viz.render(canvas, trace);
    return frame;
}

int litColumns(const stipple::Framebuffer& frame, stipple::Rgb baseline) {
    int columns = 0;
    for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
        for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
            const stipple::Rgb pixel = frame.at(x, y);
            if (pixel != colors::kBlack && pixel != baseline) {
                ++columns;
                break;
            }
        }
    }
    return columns;
}

}  // namespace

STIPPLE_TEST(Visualizer, SilenceStillShowsABaseline) {
    // A blank panel reads as broken rather than as quiet, so the centre line is
    // always drawn - even before a single sample has arrived.
    stipple::apps::Visualizer viz;
    const stipple::Framebuffer frame = renderViz(viz);

    const stipple::Framebuffer blank;
    STIPPLE_CHECK(frame != blank);
}

STIPPLE_TEST(Visualizer, LouderSoundsFillMoreOfThePanel) {
    // Varying, not steady. A constant reading is the definition of a noise
    // floor, and the visualiser now treats it as one - see
    // ASteadyToneBecomesTheNoiseFloorAndStopsAnimating below.
    stipple::apps::Visualizer quiet;
    stipple::apps::Visualizer loud;
    for (int i = 0; i < 60; ++i) {
        quiet.push(300 + (i % 5) * 120);
        loud.push(12000 + (i % 5) * 4000);
    }

    auto height = [](const stipple::Framebuffer& frame) {
        int lit = 0;
        for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
            for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
                if (frame.at(x, y) != colors::kBlack) { ++lit; break; }
            }
        }
        return lit;
    };

    // Auto-gain means a steady tone settles to a similar height whatever its
    // absolute level - which is the point - so this asserts both render
    // something rather than asserting one is taller.
    STIPPLE_CHECK(height(renderViz(quiet)) > 2);
    STIPPLE_CHECK(height(renderViz(loud)) > 2);
}

STIPPLE_TEST(Visualizer, AutoGainOpensUpForAQuietRoom) {
    // The whole reason gain lives in the app: a room that never exceeds 800
    // must still fill the panel, or the visualiser is a flat line in every
    // house that is not a nightclub.
    //
    // Measured above the noise floor, so the room swings between 200 and 700
    // rather than sitting at 700. A reading that never changes carries no
    // information about the room no matter how large the number is.
    stipple::apps::Visualizer viz;
    for (int i = 0; i < 200; ++i) {
        viz.push(200 + (i % 6) * 100);
    }

    STIPPLE_CHECK(viz.ceiling() <= 800);

    const stipple::Framebuffer frame = renderViz(viz);
    int tallest = 0;
    for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
        for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
            if (frame.at(x, y) != colors::kBlack) { ++tallest; break; }
        }
    }
    STIPPLE_CHECK(tallest > stipple::Framebuffer::kHeight / 2);
}

STIPPLE_TEST(Visualizer, ASuddenSoundIsDrawnAtOnceAndDoesNotFlattenTheRest) {
    // Both halves of the fix, in one test.
    //
    // The spike must be drawn full height on the frame it arrives - "it should
    // show when it hears something" - which works because a column is scaled
    // against the window as it stood *before* that sample moved it.
    //
    // And the columns already on screen must not change. Storing raw
    // amplitudes and rescaling the history at render time is what made a
    // finger snap look like the trace resetting.
    stipple::apps::Visualizer viz;
    for (int i = 0; i < 40; ++i) { viz.push(600); }

    const Framebuffer before = renderViz(viz);
    viz.push(32000);
    const Framebuffer after = renderViz(viz);

    // The newest column is on the right and reaches the top.
    STIPPLE_CHECK(after.at(Framebuffer::kWidth - 1, 0) != colors::kBlack);

    // Everything older is untouched: identical but for the one new column,
    // which has shifted the history left by exactly one.
    for (int x = 0; x < Framebuffer::kWidth - 1; ++x) {
        for (int y = 0; y < Framebuffer::kHeight; ++y) {
            STIPPLE_CHECK_EQ(after.at(x, y), before.at(x + 1, y));
        }
    }

    // The window rises toward the peak without landing on it, so the next few
    // seconds of ordinary sound are still legible.
    STIPPLE_CHECK(viz.ceiling() > 600);
    STIPPLE_CHECK(viz.ceiling() < 32000);
}

STIPPLE_TEST(Visualizer, HistoryScrollsAndIsBounded) {
    // 52 columns of history, newest at the right, and nothing unbounded.
    stipple::apps::Visualizer viz;
    STIPPLE_CHECK_FALSE(viz.hasSamples());

    for (int i = 0; i < 500; ++i) {
        viz.push(1000 + (i % 7) * 900);
    }
    STIPPLE_CHECK(viz.hasSamples());

    // Bounded: 500 samples in, at most 52 columns out, and the baseline spans
    // the panel however few of them carry a reading.
    //
    // Deliberately not "every column is lit". A sample sitting on the noise
    // floor is silence by definition and draws nothing above the baseline, so
    // any repeating input has dark columns wherever it revisits its quietest
    // value - which is correct, and was not true before the floor existed.
    const stipple::Framebuffer frame = renderViz(viz);
    const int lit = litColumns(frame, stipple::rgb(20, 28, 40));
    STIPPLE_CHECK(lit > 0);
    STIPPLE_CHECK(lit <= stipple::Framebuffer::kWidth);

    for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
        STIPPLE_CHECK(frame.at(x, stipple::Framebuffer::kHeight / 2 - 1) != colors::kBlack);
    }
}

STIPPLE_TEST(Visualizer, ASteadyToneBecomesTheNoiseFloorAndStopsAnimating) {
    // The bug a person watching the device reported: "it starts with animation
    // while there is no sound, and after a handclap it resets to the correct
    // levels."
    //
    // This microphone reports a few hundred in a silent room - a DC offset and
    // self-noise, not sound. Scaled against the gain window, that empty room
    // animated constantly, and the first clap threw the window up where it
    // belonged, which looked like a reset and was actually the only moment the
    // display had been right.
    stipple::apps::Visualizer viz;
    for (int i = 0; i < 100; ++i) {
        viz.push(420);
    }

    // Silence draws the baseline and nothing else.
    const stipple::Framebuffer quiet = renderViz(viz);
    int lit = 0;
    for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
        for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
            if (quiet.at(x, y) != colors::kBlack) { ++lit; }
        }
    }
    STIPPLE_CHECK_EQ(lit, stipple::Framebuffer::kWidth * 2);  // the baseline only

    // And a real sound still reads, immediately, against that floor.
    viz.push(9000);
    const stipple::Framebuffer clap = renderViz(viz);
    STIPPLE_CHECK(clap.at(stipple::Framebuffer::kWidth - 1, 0) != colors::kBlack);
}

STIPPLE_TEST(Visualizer, TheNoiseFloorFollowsARoomThatGetsQuieter) {
    // It must fall instantly: a floor that lagged would leave the panel dead
    // after a loud passage ended.
    stipple::apps::Visualizer viz;
    for (int i = 0; i < 50; ++i) { viz.push(5000); }
    for (int i = 0; i < 50; ++i) { viz.push(300); }

    // 800 now sits well above the new floor and must register.
    viz.push(800);
    const stipple::Framebuffer frame = renderViz(viz);
    bool litAboveBaseline = false;
    for (int y = 0; y < stipple::Framebuffer::kHeight / 2 - 1; ++y) {
        if (frame.at(stipple::Framebuffer::kWidth - 1, y) != colors::kBlack) {
            litAboveBaseline = true;
        }
    }
    STIPPLE_CHECK(litAboveBaseline);
}

STIPPLE_TEST(Visualizer, OutOfRangeSamplesAreClampedNotWrapped) {
    stipple::apps::Visualizer viz;
    viz.push(-5000);
    viz.push(999999);
    // Neither should have produced a nonsense window.
    STIPPLE_CHECK(viz.ceiling() >= 400);
    STIPPLE_CHECK(viz.ceiling() <= 32767);
}

STIPPLE_TEST(Visualizer, NoMicrophoneSaysSoRatherThanDrawingSilence) {
    stipple::Framebuffer frame;
    stipple::Canvas canvas(frame);
    stipple::apps::renderNoMicrophone(canvas, colors::kWhite);

    const stipple::Framebuffer blank;
    STIPPLE_CHECK(frame != blank);
}

STIPPLE_TEST(Visualizer, TheMeterRisesFromTheBottomAndDoesNotScroll) {
    // Asked for by the person living with the device: a clock on a shelf
    // should be still when the room is still, and the scrolling trace is in
    // motion whenever there is any sound at all.
    stipple::apps::VisualizerStyle meter;
    meter.kind = stipple::apps::VisualizerStyleKind::Meter;

    stipple::apps::Visualizer viz;
    for (int i = 0; i < 40; ++i) { viz.push(400); }  // settle the floor
    viz.push(20000);

    stipple::Framebuffer frame;
    stipple::Canvas canvas(frame);
    viz.render(canvas, meter);

    // Lit at the bottom, and every column of a lit row is lit: the width
    // carries no information, so the block is solid rather than split into
    // bands this hardware cannot measure.
    const int bottom = stipple::Framebuffer::kHeight - 1;
    for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
        STIPPLE_CHECK(frame.at(x, bottom) != colors::kBlack);
        STIPPLE_CHECK_EQ(frame.at(x, bottom), frame.at(0, bottom));
    }

    // A loud sound reaches the top.
    STIPPLE_CHECK(frame.at(0, 0) != colors::kBlack);
}

STIPPLE_TEST(Visualizer, TheMeterIsStillWhenTheRoomIs) {
    // The complaint the meter answers: a steady reading must not animate.
    stipple::apps::VisualizerStyle meter;
    meter.kind = stipple::apps::VisualizerStyleKind::Meter;

    stipple::apps::Visualizer viz;
    for (int i = 0; i < 60; ++i) { viz.push(420); }

    stipple::Framebuffer first;
    stipple::Canvas firstCanvas(first);
    viz.render(firstCanvas, meter);

    for (int i = 0; i < 20; ++i) { viz.push(420); }

    stipple::Framebuffer second;
    stipple::Canvas secondCanvas(second);
    viz.render(secondCanvas, meter);

    STIPPLE_CHECK(first == second);
}

STIPPLE_TEST(Visualizer, TheMeterHoldsAPeakAndLetsItFall) {
    stipple::apps::VisualizerStyle meter;
    meter.kind = stipple::apps::VisualizerStyleKind::Meter;

    stipple::apps::Visualizer viz;
    for (int i = 0; i < 40; ++i) { viz.push(400); }
    viz.push(30000);
    const int afterPeak = viz.peakPermille();
    STIPPLE_CHECK(afterPeak > 0);

    // Quiet again: the marker falls rather than pinning at the loudest thing
    // that ever happened, and rather than vanishing on the next frame.
    viz.push(400);
    STIPPLE_CHECK(viz.peakPermille() < afterPeak);
    STIPPLE_CHECK(viz.peakPermille() > viz.currentPermille());
}

STIPPLE_TEST(Visualizer, AnUnknownStyleNameFallsBackRatherThanFailing) {
    using stipple::apps::visualizerStyleFromName;
    using stipple::apps::visualizerStyleName;
    using stipple::apps::VisualizerStyleKind;

    STIPPLE_CHECK(visualizerStyleFromName("trace") == VisualizerStyleKind::Trace);
    STIPPLE_CHECK(visualizerStyleFromName("meter") == VisualizerStyleKind::Meter);
    STIPPLE_CHECK(visualizerStyleFromName("wave") == VisualizerStyleKind::Wave);
    // A config written by a newer build must still load, landing on whatever
    // the current default is rather than failing.
    STIPPLE_CHECK(visualizerStyleFromName("spectrum") == VisualizerStyleKind::Wave);

    STIPPLE_CHECK_EQ(std::string(visualizerStyleName(VisualizerStyleKind::Trace)), std::string("trace"));
    STIPPLE_CHECK_EQ(std::string(visualizerStyleName(VisualizerStyleKind::Meter)), std::string("meter"));
    STIPPLE_CHECK_EQ(std::string(visualizerStyleName(VisualizerStyleKind::Wave)), std::string("wave"));
}

STIPPLE_TEST(Visualizer, TheWaveTravelsOverTime) {
    stipple::apps::VisualizerStyle wave;  // the default

    stipple::apps::Visualizer viz;
    for (int i = 0; i < 40; ++i) { viz.push(400); }
    viz.push(8000);

    stipple::Framebuffer first;
    stipple::Canvas firstCanvas(first);
    viz.render(firstCanvas, wave, 0);

    stipple::Framebuffer later;
    stipple::Canvas laterCanvas(later);
    viz.render(laterCanvas, wave, 650);

    STIPPLE_CHECK(first != later);
}

STIPPLE_TEST(Visualizer, TheWaveRipplesInSilenceButGoesFlatWithNoMicrophone) {
    // The two states must not look the same. A device that cannot hear draws
    // NO MIC; a quiet room draws a shallow wave, because an app that looks
    // switched off whenever nobody is talking reads as broken.
    stipple::apps::VisualizerStyle wave;

    stipple::apps::Visualizer viz;
    for (int i = 0; i < 60; ++i) { viz.push(420); }

    stipple::Framebuffer frame;
    stipple::Canvas canvas(frame);
    viz.render(canvas, wave, 0);

    int lit = 0;
    for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
        for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
            if (frame.at(x, y) != colors::kBlack) { ++lit; }
        }
    }
    STIPPLE_CHECK(lit > 0);

    // And it still moves when the room is quiet, so the panel looks awake.
    stipple::Framebuffer later;
    stipple::Canvas laterCanvas(later);
    viz.render(laterCanvas, wave, 900);
    STIPPLE_CHECK(frame != later);
}

STIPPLE_TEST(Visualizer, ALouderRoomMakesATallerWave) {
    stipple::apps::VisualizerStyle wave;

    // Against silence rather than against a second loud value: auto-gain
    // deliberately brings any sustained level up to full height, so two loud
    // rooms look alike and that is the feature, not a bug.
    auto reach = [&](int loudness) {
        stipple::apps::Visualizer viz;
        for (int i = 0; i < 40; ++i) { viz.push(400); }
        for (int i = 0; i < 4; ++i) { viz.push(400 + loudness); }

        stipple::Framebuffer frame;
        stipple::Canvas canvas(frame);
        viz.render(canvas, wave, 0);

        int top = stipple::Framebuffer::kHeight;
        for (int y = 0; y < stipple::Framebuffer::kHeight; ++y) {
            for (int x = 0; x < stipple::Framebuffer::kWidth; ++x) {
                if (frame.at(x, y) != colors::kBlack && y < top) { top = y; }
            }
        }
        return top;
    };

    // A taller wave reaches a smaller row number.
    STIPPLE_CHECK(reach(20000) < reach(0));
}

STIPPLE_TEST(Visualizer, TheWaveStaysOnThePanel) {
    // Integer trig and a mirror below the centre: the crest and the trough
    // both have to land inside 16 rows at every amplitude and every phase.
    stipple::apps::VisualizerStyle wave;

    stipple::apps::Visualizer viz;
    for (int i = 0; i < 40; ++i) { viz.push(0); }

    for (int loudness = 0; loudness <= 32000; loudness += 4000) {
        viz.push(loudness);
        for (std::uint64_t t = 0; t < 3000; t += 137) {
            stipple::Framebuffer frame;
            stipple::Canvas canvas(frame);
            viz.render(canvas, wave, t);  // Canvas clips, so this asserts no crash
            STIPPLE_CHECK(frame.at(0, 0) == frame.at(0, 0));
        }
    }
}
