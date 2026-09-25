// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "stipple/core/Rgb.h"
#include "stipple/graphics/Framebuffer.h"

namespace stipple {
namespace imageio {

/// Minimal PNG encoder with no external dependencies — not even zlib.
///
/// It emits a valid zlib stream built from *stored* (uncompressed) deflate
/// blocks. Every PNG decoder handles these, and for a 52x16 panel the wasted
/// bytes are irrelevant. Pulling in a compression library for the sake of a few
/// kilobytes of debug output would have meant a vendored dependency, a licence
/// entry and a CI download, which the blueprint's reproducibility rules (§32)
/// make expensive. This is ~150 lines instead.
///
/// `scale` performs nearest-neighbour magnification. A 52x16 frame at 1:1 is
/// unreadable on a modern screen, so snapshots default to 8x.

std::vector<std::uint8_t> encodePng(const Rgb* pixels, int width, int height, int scale = 1);

std::vector<std::uint8_t> encodePng(const Framebuffer& framebuffer, int scale = 1);

/// Returns false if the file could not be opened or written.
bool writePng(const std::string& path, const Framebuffer& framebuffer, int scale = 8);

}  // namespace imageio
}  // namespace stipple
