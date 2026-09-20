// SPDX-License-Identifier: GPL-3.0-or-later
//
// NOTRIX rendering on a real TC002.
//
// Not a test pattern written by hand against the wire - this is the actual
// core: a Framebuffer NOTRIX owns, a Canvas drawing into it, and
// demo::drawTestPattern, which is the same code the browser emulator and the
// golden-image tests run. Only IFrameBufferDisplay differs, which is the whole
// point of the blueprint §53 boundary.
//
// The vendor application must not be running. Both write to /dev/spidev0.0 and
// neither arbitrates, so the panel ends up alternating between two owners:
//
//     adb shell setprop ctl.stop zkswe      # take the panel
//     adb shell /tmp/notrix_device_panel 20
//     adb shell setprop ctl.start zkswe     # give it back
//
// Nothing here is persistent. /tmp is tmpfs, so a power cycle restores the
// stock application no matter how this exits.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "notrix/demo/TestPattern.h"
#include "notrix/graphics/Canvas.h"
#include "notrix/graphics/Framebuffer.h"
#include "notrix/platform/tc002/Tc002Display.h"

namespace {

notrix::platform::tc002::Tc002Display* g_display = nullptr;

// Blank on the way out, including on Ctrl-C. A clock left holding a half-drawn
// frame looks broken in a way that has nothing to do with what went wrong.
extern "C" void onSignal(int) {
    if (g_display != nullptr) {
        g_display->close();
    }
    std::_Exit(1);
}

notrix::platform::tc002::ChannelOrder parseOrder(const char* text) {
    using notrix::platform::tc002::ChannelOrder;
    if (std::strcmp(text, "grb") == 0) {
        return ChannelOrder::Grb;
    }
    if (std::strcmp(text, "bgr") == 0) {
        return ChannelOrder::Bgr;
    }
    return ChannelOrder::Rgb;
}

void sleepMillis(int millis) {
    // nanosleep rather than usleep: the device busybox has no sleep(1), and
    // usleep(3) is deprecated. This is the loop's only timing primitive.
    struct timespec request;
    request.tv_sec = millis / 1000;
    request.tv_nsec = static_cast<long>(millis % 1000) * 1000000L;
    nanosleep(&request, nullptr);
}

}  // namespace

int main(int argc, char** argv) {
    const int seconds = (argc > 1) ? std::atoi(argv[1]) : 20;
    const int brightness = (argc > 2) ? std::atoi(argv[2]) : 64;
    const auto order = (argc > 3) ? parseOrder(argv[3])
                                  : notrix::platform::tc002::ChannelOrder::Rgb;

    notrix::platform::tc002::Tc002Display display(order);
    g_display = &display;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    if (!display.open()) {
        std::fprintf(stderr,
                     "could not open /dev/spidev0.0 - is zkgui still running?\n");
        return 1;
    }

    display.setBrightness(static_cast<std::uint8_t>(brightness));

    const int intervalMillis = display.minimumFrameIntervalMillis();
    const int frames = (seconds * 1000) / intervalMillis;

    std::printf("NOTRIX on the panel\n");
    std::printf("  framebuffer : %dx%d, addressed as %dx%d\n",
                notrix::Framebuffer::kWidth, notrix::Framebuffer::kHeight,
                notrix::platform::tc002::Tc002Display::kAddressedWidth,
                notrix::Framebuffer::kHeight);
    std::printf("  brightness  : %d/255\n", brightness);
    std::printf("  channels    : %s\n",
                (argc > 3) ? argv[3] : "rgb (assumed, unconfirmed)");
    std::printf("  pacing      : %d ms per frame, %d frames\n", intervalMillis,
                frames);
    std::printf("\nrendering - look at the panel\n");

    notrix::Framebuffer framebuffer;
    notrix::Canvas canvas(framebuffer);

    for (int frame = 0; frame < frames; ++frame) {
        notrix::demo::drawTestPattern(canvas, frame);

        // Unconditionally, every tick. The driver chips hold an image only
        // while being fed, so "nothing changed, skip the write" would make the
        // panel go dark rather than stay still.
        display.present(framebuffer);

        sleepMillis(intervalMillis);
    }

    std::printf("blanking\n");
    display.close();
    g_display = nullptr;
    return 0;
}
