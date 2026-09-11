// SPDX-License-Identifier: GPL-3.0-or-later
//
// Emscripten entry point for the browser emulator.
//
// Everything here is glue. The browser owns the frame loop and calls in; the
// core owns every pixel. Keeping this file boring is the point — it is the
// emulator's half of the §53 platform boundary, and its device counterpart in
// Phase 7 will be the same handful of functions talking to sendLedData instead.

#include <emscripten/emscripten.h>

#include <cstdint>

#include "notrix/demo/TestPattern.h"
#include "notrix/graphics/Canvas.h"

namespace {

notrix::Framebuffer g_framebuffer;
std::uint8_t g_brightness = 255u;

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void notrix_init() {
    g_framebuffer.clear();
    g_brightness = 255u;
}

/// Global brightness is applied as a post-pass over the finished frame, which is
/// how the device will do it too: apps draw in true colour and never have to
/// know the current brightness setting.
EMSCRIPTEN_KEEPALIVE void notrix_set_brightness(int value) {
    if (value < 0) {
        value = 0;
    }
    if (value > 255) {
        value = 255;
    }
    g_brightness = static_cast<std::uint8_t>(value);
}

EMSCRIPTEN_KEEPALIVE void notrix_render(int frame) {
    notrix::Canvas canvas(g_framebuffer);
    notrix::demo::drawTestPattern(canvas, frame);

    if (g_brightness == 255u) {
        return;
    }
    notrix::Rgb* pixels = g_framebuffer.data();
    for (int i = 0; i < notrix::Framebuffer::kPixelCount; ++i) {
        pixels[i] = notrix::scale(pixels[i], g_brightness);
    }
}

/// Pointer into the WASM heap; JS reads kWidth * kHeight * 3 bytes from here.
EMSCRIPTEN_KEEPALIVE const unsigned char* notrix_framebuffer() {
    return g_framebuffer.bytes();
}

EMSCRIPTEN_KEEPALIVE int notrix_width() {
    return notrix::Framebuffer::kWidth;
}

EMSCRIPTEN_KEEPALIVE int notrix_height() {
    return notrix::Framebuffer::kHeight;
}

}  // extern "C"
