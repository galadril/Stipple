// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/apps/SplashScreen.h"

#include "notrix/graphics/Canvas.h"
#include "notrix/platform/PlatformServices.h"
#include "notrix/text/Scroll.h"
#include "notrix/text/Text.h"

namespace notrix {
namespace apps {

void renderSplash(Canvas& canvas,
                  std::string_view title,
                  std::string_view detail,
                  std::uint64_t elapsedMillis,
                  const SplashStyle& style) {
    text::TextStyle titleStyle;
    titleStyle.font = &text::font5x7();
    titleStyle.color = style.titleColor;
    titleStyle.hAlign = text::HAlign::Center;
    titleStyle.vAlign = text::VAlign::Top;
    text::draw(canvas, title, Rect{0, 0, Framebuffer::kWidth, 7}, titleStyle);

    text::TextStyle detailStyle = titleStyle;
    detailStyle.color = style.detailColor;

    // Start moving sooner and faster than normal body text. The splash is only
    // on screen for a few seconds, and an address that has not finished
    // scrolling by the time it disappears has told the user nothing.
    text::ScrollConfig scroll;
    scroll.startDelayMillis = 500;
    scroll.pixelsPerSecond = 18;
    scroll.gapPixels = 10;

    text::drawScrolling(canvas, detail, Rect{0, 9, Framebuffer::kWidth, 7}, detailStyle,
                        text::ScrollMode::Auto, elapsedMillis, scroll);
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
