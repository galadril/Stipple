// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/apps/SplashScreen.h"

#include <cmath>

#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/platform/PlatformServices.h"
#include "stipple/text/Scroll.h"
#include "stipple/text/Text.h"

namespace stipple {
namespace apps {
namespace {

// The whole 16 rows, accounted for. DESIGN.md §2 allows two 7-row lines with
// one row spare; spending that row on the progress rule is what makes the
// layout add up exactly.
constexpr int kTitleY = 0;       // rows 0-6
constexpr int kDetailY = 8;      // rows 8-14
constexpr int kRuleY = Framebuffer::kHeight - 1;  // row 15

/// Fake-bold: the same glyphs drawn twice, one column apart.
///
/// There is one font and there will only ever be one (DESIGN.md §8) - a 2x
/// scale would be 14 rows tall and leave no room for anything else. Drawing
/// twice turns every 1px stroke into 2px, which is the only way to give the
/// product name more weight than the line beneath it.
void drawBold(Canvas& canvas, std::string_view text, int y, Rgb color) {
    const text::BitmapFont& font = text::font5x7();

    // The emboldened run is one column wider than measured, and centring on the
    // measured width would sit it a pixel left of centre.
    const int width = text::measureLine(text, font) + 1;
    const int x = (Framebuffer::kWidth - width) / 2;

    text::drawLine(canvas, text, x, y, font, color);
    text::drawLine(canvas, text, x + 1, y, font, color);
}

/// Two travelling sines, one pixel per column each.
///
/// Shallow on purpose. A tall wave under the product name reads as a chart
/// with a meaning; a shallow one reads as the device breathing while it gets
/// ready, which is all this is claiming. The grey line trails the blue one by
/// a fraction of a period so the pair has depth without a second colour
/// fighting the first.
///
/// Integer-free trigonometry would be kinder to a device with no FPU, but
/// this runs for five seconds at boot and never again, so a readable
/// expression beats a lookup table nobody can check.
void drawWave(Canvas& canvas, int top, int rows, std::uint64_t elapsedMillis,
              const SplashStyle& style) {
    if (rows < 1) {
        return;
    }
    const double centre = static_cast<double>(top) + static_cast<double>(rows - 1) / 2.0;

    // Deliberately less than the band allows: at full amplitude the crests
    // touch the title and the whole thing looks like it is clipping.
    const double amplitude = static_cast<double>(rows - 1) / 2.0 - 0.5;

    // Period and speed chosen so about two crests sit on the panel at once
    // and one takes roughly a second to cross it. Faster reads as urgency,
    // slower reads as a hang.
    const double phase = static_cast<double>(elapsedMillis) / 260.0;

    // Trailing line first, so the blue one wins wherever they overlap.
    const double trail = 0.9;
    for (int pass = 0; pass < 2; ++pass) {
        const bool leading = pass == 1;
        const Rgb color = leading ? style.waveColor : style.waveTrailColor;
        const double offset = leading ? 0.0 : trail;
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const double angle = phase - offset - static_cast<double>(x) / 6.5;
            const double row = centre + amplitude * std::sin(angle);
            canvas.pixel(x, static_cast<int>(row + 0.5), color);
        }
    }
}

}  // namespace

void renderSplash(Canvas& canvas,
                  std::string_view title,
                  std::string_view detail,
                  std::uint64_t elapsedMillis,
                  std::uint64_t durationMillis,
                  const SplashStyle& style,
                  std::string_view address) {
    // A duration means this is the splash, which has two pages. Without one
    // it is a notice, which has always been a title over a single line and
    // stays that way.
    const bool paged = durationMillis > 0;
    const std::uint64_t pageTurn = durationMillis / 2;
    const bool firstPage = paged && elapsedMillis < pageTurn;

    // The name earns the top line for the first five seconds and then gets
    // out of the way. By the second page it has been read, and the panel is
    // better spent on the two things somebody is waiting for.
    if (!paged || firstPage) {
        drawBold(canvas, title, kTitleY, style.titleColor);
    }

    text::TextStyle detailStyle;
    detailStyle.font = &text::font5x7();
    detailStyle.color = style.detailColor;
    detailStyle.hAlign = text::HAlign::Center;
    detailStyle.vAlign = text::VAlign::Top;

    // Start moving sooner and faster than normal body text. Each page is only
    // on screen for a few seconds, and an address that has not finished
    // scrolling by the time it disappears has told the user nothing.
    text::ScrollConfig scroll;
    scroll.startDelayMillis = 500;
    scroll.pixelsPerSecond = 18;
    scroll.gapPixels = 10;

    if (firstPage) {
        drawWave(canvas, kDetailY, 7, elapsedMillis, style);
    } else if (!paged) {
        text::drawScrolling(canvas, detail, Rect{0, kDetailY, Framebuffer::kWidth, 7},
                            detailStyle, text::ScrollMode::Auto, elapsedMillis, scroll);
    } else {
        // Measured from the page turn, not from boot, or the lines would
        // arrive already half-scrolled off the panel.
        const std::uint64_t onPage =
            elapsedMillis > pageTurn ? elapsedMillis - pageTurn : 0;

        // Version above, address below - and the address gets the brighter
        // colour. The version is what you report in a bug; the address is
        // what you are squinting at the panel to find.
        text::drawScrolling(canvas, detail, Rect{0, kTitleY, Framebuffer::kWidth, 7},
                            detailStyle, text::ScrollMode::Auto, onPage, scroll);

        text::TextStyle addressStyle = detailStyle;
        addressStyle.color = style.titleColor;
        text::drawScrolling(canvas, address, Rect{0, kDetailY, Framebuffer::kWidth, 7},
                            addressStyle, text::ScrollMode::Auto, onPage, scroll);
    }

    // A single row that drains as the splash runs out.
    //
    // This is the only other animation here, and it earns its place under
    // DESIGN.md section 5.1 by carrying information rather than decorating: it
    // says how long is left to read the address before the clock takes over.
    // Someone squinting at an IP can see whether to hurry.
    if (durationMillis == 0) {
        return;
    }

    const std::uint64_t elapsed =
        elapsedMillis > durationMillis ? durationMillis : elapsedMillis;
    const std::uint64_t left = durationMillis - elapsed;

    // Rounded up, so the rule is still one pixel wide at the very end rather
    // than vanishing a moment early and looking like a glitch.
    const int remaining = static_cast<int>(
        (left * static_cast<std::uint64_t>(Framebuffer::kWidth) + durationMillis - 1) /
        durationMillis);

    if (remaining > 0) {
        canvas.fillRect(Rect{0, kRuleY, remaining, 1}, style.ruleColor);
    }
}

std::string splashAddress(const platform::INetworkManager* network) {
    if (network == nullptr) {
        return "no network";
    }
    const platform::NetworkStatus status = network->status();
    if (!status.connected) {
        return "no Wi-Fi";
    }
    if (!status.ipv4.empty()) {
        return status.ipv4;
    }
    if (!status.hostname.empty()) {
        return status.hostname;
    }
    return "connected";
}

std::string splashDetail(std::string_view version, const platform::INetworkManager* network) {
    std::string detail(version);

    if (network == nullptr) {
        // No interface at all, as opposed to one that is down (ADR 0013).
        detail += " - no network";
        return detail;
    }

    const platform::NetworkStatus status = network->status();
    if (!status.connected) {
        detail += " - no Wi-Fi";
        return detail;
    }

    if (!status.ipv4.empty()) {
        detail += " - ";
        detail += status.ipv4;
    } else if (!status.hostname.empty()) {
        detail += " - ";
        detail += status.hostname;
    } else {
        detail += " - connected";
    }
    return detail;
}

}  // namespace apps
}  // namespace stipple
