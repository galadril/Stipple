// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "notrix/core/Assert.h"
#include "notrix/core/Geometry.h"
#include "notrix/core/Rgb.h"

namespace notrix {

/// The canonical 52x16 RGB framebuffer that NOTRIX owns end to end (blueprint
/// §9.1). Storage is a fixed inline array — no heap, no resizing — so the render
/// path allocates nothing and the memory cost is knowable at compile time:
/// 52 * 16 * 3 = 2496 bytes.
///
/// Nothing above the platform boundary knows how these pixels reach a panel.
/// The TC002 adapter forwards them to PageBase::sendLedData; the simulator
/// adapter hands them to a browser canvas.
class Framebuffer {
public:
    static constexpr int kWidth = 52;
    static constexpr int kHeight = 16;
    static constexpr int kPixelCount = kWidth * kHeight;
    static constexpr std::size_t kByteSize = static_cast<std::size_t>(kPixelCount) * 3u;

    static constexpr Rect bounds() noexcept { return Rect{0, 0, kWidth, kHeight}; }

    static constexpr bool inBounds(int x, int y) noexcept {
        return x >= 0 && x < kWidth && y >= 0 && y < kHeight;
    }

    /// Out-of-range writes are ignored rather than fatal. Canvas clips before it
    /// ever gets here, so reaching this guard means a bug — the assert catches it
    /// in development while release builds keep the clock running.
    void set(int x, int y, Rgb color) noexcept {
        NOTRIX_ASSERT(inBounds(x, y));
        if (!inBounds(x, y)) {
            return;
        }
        pixels_[index(x, y)] = color;
    }

    Rgb at(int x, int y) const noexcept {
        NOTRIX_ASSERT(inBounds(x, y));
        if (!inBounds(x, y)) {
            return colors::kBlack;
        }
        return pixels_[index(x, y)];
    }

    void fill(Rgb color) noexcept { pixels_.fill(color); }

    void clear() noexcept { fill(colors::kBlack); }

    const Rgb* data() const noexcept { return pixels_.data(); }
    Rgb* data() noexcept { return pixels_.data(); }

    /// Raw RGB888 bytes in row-major order — the wire format for golden-image
    /// fixtures and for handing frames to the emulator.
    const std::uint8_t* bytes() const noexcept {
        return reinterpret_cast<const std::uint8_t*>(pixels_.data());
    }

    friend bool operator==(const Framebuffer& lhs, const Framebuffer& rhs) noexcept {
        return lhs.pixels_ == rhs.pixels_;
    }

    friend bool operator!=(const Framebuffer& lhs, const Framebuffer& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    static constexpr std::size_t index(int x, int y) noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) +
               static_cast<std::size_t>(x);
    }

    std::array<Rgb, static_cast<std::size_t>(kPixelCount)> pixels_{};
};

static_assert(sizeof(Rgb) == 3, "Rgb must stay tightly packed for raw byte access");

}  // namespace notrix
