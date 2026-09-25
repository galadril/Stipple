// SPDX-License-Identifier: GPL-3.0-or-later
//
// STIPPLE rendering on a real TC002.
//
// Not a test pattern written by hand against the wire - this is the actual
// core: a Framebuffer STIPPLE owns, a Canvas drawing into it, and
// demo::drawTestPattern, which is the same code the browser emulator and the
// golden-image tests run. Only IFrameBufferDisplay differs, which is the whole
// point of the blueprint §53 boundary.
//
// The vendor application must not be running. Both write to /dev/spidev0.0 and
// neither arbitrates, so the panel ends up alternating between two owners:
//
//     adb shell setprop ctl.stop zkswe      # take the panel
//     adb shell /tmp/stipple_device_panel 20
//     adb shell setprop ctl.start zkswe     # give it back
//
// Nothing here is persistent. /tmp is tmpfs, so a power cycle restores the
// stock application no matter how this exits.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "stipple/demo/TestPattern.h"
#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/platform/tc002/Tc002Display.h"
#include "stipple/platform/tc002/Tc002Input.h"

namespace {

stipple::platform::tc002::Tc002Display* g_display = nullptr;

// Blank on the way out, including on Ctrl-C. A clock left holding a half-drawn
// frame looks broken in a way that has nothing to do with what went wrong.
extern "C" void onSignal(int) {
    if (g_display != nullptr) {
        g_display->close();
    }
    std::_Exit(1);
}

stipple::platform::tc002::ChannelOrder parseOrder(const char* text) {
    using stipple::platform::tc002::ChannelOrder;
    if (std::strcmp(text, "grb") == 0) {
        return ChannelOrder::Grb;
    }
    if (std::strcmp(text, "bgr") == 0) {
        return ChannelOrder::Bgr;
    }
    return ChannelOrder::Rgb;
}

const char* sourceName(stipple::platform::RawInput source) {
    using stipple::platform::RawInput;
    switch (source) {
        case RawInput::KeyMinus: return "-";
        case RawInput::KeyMiddle: return "middle";
        case RawInput::KeyPlus: return "+";
        case RawInput::RotaryPress: return "knob press";
        case RawInput::RotaryLeft: return "knob left";
        case RawInput::RotaryRight: return "knob right";
    }
    return "?";
}

const char* phaseName(stipple::platform::ButtonPhase phase) {
    using stipple::platform::ButtonPhase;
    switch (phase) {
        case ButtonPhase::Down: return "down";
        case ButtonPhase::Up: return "up";
        case ButtonPhase::Tick: return "tick";
    }
    return "?";
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
                                  : stipple::platform::tc002::ChannelOrder::Rgb;

    stipple::platform::tc002::Tc002Display display(order);
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

    std::printf("STIPPLE on the panel\n");
    std::printf("  framebuffer : %dx%d, addressed as %dx%d\n",
                stipple::Framebuffer::kWidth, stipple::Framebuffer::kHeight,
                stipple::platform::tc002::Tc002Display::kAddressedWidth,
                stipple::Framebuffer::kHeight);
    std::printf("  brightness  : %d/255\n", brightness);
    std::printf("  channels    : %s\n",
                (argc > 3) ? argv[3] : "rgb (assumed, unconfirmed)");
    std::printf("  pacing      : %d ms per frame, %d frames\n", intervalMillis,
                frames);

    // Input is optional here on purpose. The panel is the point of this tool,
    // and a device whose evdev nodes moved should still render rather than
    // refuse to start.
    stipple::platform::tc002::Tc002Input input;
    const bool haveInput = input.open();
    std::printf("  input       : %s\n",
                haveInput ? "event67 + event68" : "unavailable");
    std::printf("\nrendering - look at the panel, and press the controls\n");

    stipple::Framebuffer framebuffer;
    stipple::Canvas canvas(framebuffer);

    for (int frame = 0; frame < frames; ++frame) {
        stipple::platform::InputEvent event;
        while (haveInput && input.poll(event)) {
            std::printf("  input: %-11s %s\n", sourceName(event.source),
                        phaseName(event.phase));
            std::fflush(stdout);
        }

        stipple::demo::drawTestPattern(canvas, frame);

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
