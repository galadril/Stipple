// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native frame previewer.
//
// Renders real frames through the real core and writes them as PNGs, plus a
// small page that plays them back. It is NOT the emulator: there is no input,
// no interactivity, and it is a frame dump rather than a running device. What it
// is good for is seeing output without an Emscripten toolchain, and eyeballing a
// render while iterating on it.
//
// It drives the simulator platform adapter rather than talking to the canvas
// directly, so it exercises the §53 boundary the same way the real application
// loop will.

#include <cstdio>
#include <fstream>
#include <string>

#include "stipple/demo/TestPattern.h"
#include "stipple/graphics/Canvas.h"
#include "stipple/imageio/Png.h"
#include "stipple/platform/simulator/SimulatorPlatform.h"
#include "stipple/text/Text.h"

namespace {

constexpr int kFrameCount = 98;  // one full scan-bar period
constexpr int kScale = 10;
constexpr int kFrameIntervalMillis = 33;  // ~30 FPS, per blueprint §9.4

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::Rect;
using stipple::platform::simulator::SimulatorPlatform;

/// Composes a frame: the bring-up pattern with a text overlay on top, which
/// together exercise every primitive plus the font engine.
void renderFrame(Canvas& canvas, int frame) {
    stipple::demo::drawTestPattern(canvas, frame);

    stipple::text::TextStyle style;
    style.font = &stipple::text::font5x7();
    style.color = stipple::rgb(255, 255, 255);
    style.hAlign = stipple::text::HAlign::Center;
    style.vAlign = stipple::text::VAlign::Middle;

    // Box is the full font height; a shorter one would clip the last glyph row.
    stipple::text::draw(canvas, "STIPPLE", Rect{0, 0, 52, 7}, style);
}

bool writeIndexPage(const std::string& directory) {
    std::ofstream page(directory + "/index.html", std::ios::binary | std::ios::trunc);
    if (!page) {
        return false;
    }

    page << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
            "<title>STIPPLE preview</title>\n<style>\n"
            "body{background:#0a0b0d;color:#e8eaed;font-family:system-ui,sans-serif;"
            "display:flex;min-height:100vh;margin:0;align-items:center;"
            "justify-content:center;flex-direction:column;gap:18px}\n"
            "img{image-rendering:pixelated;border-radius:8px;"
            "box-shadow:0 18px 44px rgba(0,0,0,.55)}\n"
            "p{color:#868d98;font-size:13px;margin:0;text-align:center;max-width:34em;"
            "line-height:1.5}\n"
            "code{color:#ffb347}\n</style></head><body>\n"
            "<img id=\"panel\" src=\"frame-000.png\" alt=\"STIPPLE panel preview\">\n"
            "<p>Frame dump rendered by <code>stipple_core</code> on the host. "
            "This is a preview, not the emulator &mdash; there is no input and nothing "
            "is interactive. Build the real emulator with <code>.\\dev.ps1 emulator</code>.</p>\n"
            "<script>\n"
            "let i=0;const img=document.getElementById('panel');\n"
            "setInterval(()=>{i=(i+1)%"
         << kFrameCount
         << ";img.src='frame-'+String(i).padStart(3,'0')+'.png';},"
         << kFrameIntervalMillis
         << ");\n</script>\n</body></html>\n";

    return static_cast<bool>(page);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string directory = argc > 1 ? argv[1] : "preview";

    SimulatorPlatform platform;
    Framebuffer framebuffer;

    for (int frame = 0; frame < kFrameCount; ++frame) {
        Canvas canvas(framebuffer);
        renderFrame(canvas, frame);

        // Through the platform boundary, exactly as the application loop will.
        platform.display().present(framebuffer);
        platform.simulatedClock().advance(kFrameIntervalMillis);

        char name[64];
        std::snprintf(name, sizeof(name), "/frame-%03d.png", frame);

        const std::string path = directory + name;
        if (!stipple::imageio::writePng(path, platform.simulatedDisplay().lastFrame(), kScale)) {
            std::fprintf(stderr,
                         "stipple_preview: could not write %s\n"
                         "  (does the output directory exist?)\n",
                         path.c_str());
            return 1;
        }
    }

    if (!writeIndexPage(directory)) {
        std::fprintf(stderr, "stipple_preview: could not write index.html\n");
        return 1;
    }

    std::printf("Rendered %d frames to %s\n", kFrameCount, directory.c_str());
    std::printf("Presented through the '%s' platform adapter.\n", platform.name());
    return 0;
}
