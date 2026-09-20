// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/SplashScreen.h"

#include "notrix/graphics/Canvas.h"
#include "notrix/graphics/Framebuffer.h"
#include "notrix/platform/PlatformServices.h"
#include "notrix/text/Scroll.h"
#include "notrix/text/Text.h"

namespace notrix {
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

}  // namespace

void renderSplash(Canvas& canvas,
                  std::string_view title,
                  std::string_view detail,
                  std::uint64_t elapsedMillis,
                  std::uint64_t durationMillis,
                  const SplashStyle& style) {
    drawBold(canvas, title, kTitleY, style.titleColor);

    text::TextStyle detailStyle;
    detailStyle.font = &text::font5x7();
    detailStyle.color = style.detailColor;
    detailStyle.hAlign = text::HAlign::Center;
    detailStyle.vAlign = text::VAlign::Top;

    // Start moving sooner and faster than normal body text. The splash is only
    // on screen for a few seconds, and an address that has not finished
    // scrolling by the time it disappears has told the user nothing.
    text::ScrollConfig scroll;
    scroll.startDelayMillis = 500;
    scroll.pixelsPerSecond = 18;
    scroll.gapPixels = 10;

    text::drawScrolling(canvas, detail, Rect{0, kDetailY, Framebuffer::kWidth, 7},
                        detailStyle, text::ScrollMode::Auto, elapsedMillis, scroll);

    // A single row that drains as the splash runs out.
    //
    // This is the only animation here, and it earns its place under DESIGN.md
    // §5.1 by carrying information rather than decorating: it says how long is
    // left to read the address before the clock takes over. Someone squinting
    // at an IP can see whether to hurry.
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
}  // namespace notrix
