// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stipple/core/Rgb.h"
#include "stipple/graphics/Canvas.h"

namespace stipple {
namespace asset {

/// A small image, optionally animated.
///
/// Stored as raw RGB frames, never as PNG or GIF. Decoding a compressed image
/// format needs inflate or LZW running against untrusted input, and ADR 0012
/// already ruled that out for the firmware. Conversion happens before upload —
/// a browser decodes any image with a canvas in three lines and sends pixels —
/// so the device only ever handles a format it cannot get wrong.
///
/// Transparency is a colour key rather than an alpha channel. Alpha would cost a
/// fourth byte on every pixel of every frame for a panel that cannot blend
/// anyway: an LED is on or off. The converter maps anything below half alpha to
/// the key colour.
struct Icon {
    std::string id;
    int width = 0;
    int height = 0;
    int frameCount = 0;

    /// Milliseconds per frame. Ignored when there is only one.
    std::uint32_t frameMillis = 100;

    bool hasTransparency = false;
    Rgb transparent{0, 0, 0};

    /// frameCount * width * height, frame-major.
    std::vector<Rgb> pixels;

    bool animated() const noexcept { return frameCount > 1; }
    std::size_t pixelsPerFrame() const noexcept {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
    std::size_t byteSize() const noexcept { return pixels.size() * sizeof(Rgb); }
};

/// Bounded collection of icons.
///
/// The budget is a total byte count rather than an icon count, because the two
/// shapes people actually upload differ wildly: sixty-four static 8x8 glyphs and
/// eight eight-frame animations cost the same RAM, and a per-icon limit would
/// either forbid the first or permit far too much of the second.
class IconStore {
public:
    /// 64 KB, set against a measurement rather than a guess.
    ///
    /// This was 12 KB and said so: "deliberately modest until Phase 7
    /// measures real headroom on the device." That measurement has now been
    /// taken, on a TC002 running Stipple with scripts loaded:
    ///
    ///     MemTotal      33168 kB
    ///     MemAvailable  14304 kB
    ///     Stipple RSS    7824 kB
    ///
    /// So 64 KB is under half a percent of what is actually available, and
    /// 12 KB was refusing sixteen-pixel icons on a panel sixteen pixels tall
    /// for no reason anyone could point at.
    ///
    /// Still a total rather than a per-icon limit: sixty-four 8x8 glyphs and
    /// eight eight-frame animations cost the same RAM, and a per-icon cap
    /// would either forbid the first or permit far too much of the second.
    static constexpr std::size_t kMaxTotalBytes = 64u * 1024u;
    static constexpr int kMaxIcons = 64;
    static constexpr int kMaxDimension = 32;
    static constexpr int kMaxFrames = 16;
    static constexpr std::size_t kMaxIdBytes = 48;

    enum class PutResult {
        Added,
        Replaced,
        InvalidId,
        InvalidGeometry,
        TooManyIcons,
        BudgetExceeded,
    };

    static const char* describe(PutResult result) noexcept;

    /// Insert or replace by id. A replacement keeps its position.
    PutResult put(Icon icon);

    bool remove(std::string_view id);
    void clear();

    const Icon* find(std::string_view id) const noexcept;
    int count() const noexcept { return static_cast<int>(icons_.size()); }
    const Icon* at(int index) const noexcept;

    std::size_t bytesUsed() const noexcept { return bytesUsed_; }
    std::size_t bytesFree() const noexcept { return kMaxTotalBytes - bytesUsed_; }

    /// Bumped on every mutation, so anything holding a BitmapView into an icon
    /// knows when it may have been invalidated.
    std::uint32_t revision() const noexcept { return revision_; }

    /// Which frame is showing at `elapsedMillis`. Always in range.
    static int frameAt(const Icon& icon, std::uint64_t elapsedMillis) noexcept;

    /// A blittable view of one frame. Invalid if the index is out of range or
    /// the icon is empty.
    static BitmapView frameView(const Icon& icon, int frameIndex) noexcept;

    // --- persistence ---------------------------------------------------------
    //
    // Everything is written as one binary blob under a single key, rather than
    // a key per icon plus an index. A single write is atomic under the IStorage
    // contract, so there is no window where an index and the icons it names can
    // disagree after a power cut. Rewriting the whole set on every change costs
    // little for data that changes rarely.
    //
    // Binary rather than JSON because the difference is not marginal: 256 pixels
    // as decimal text is roughly 2 KB for 768 bytes of data, and an animation
    // would not fit in a storage value at all.

    static constexpr std::uint8_t kFormatVersion = 1;

    /// Pack every stored icon. The result is binary and may contain NUL bytes.
    std::string serialize() const;

    /// Replace the contents from a blob produced by `serialize`.
    ///
    /// Returns false and leaves the store empty if the data is unusable. Storage
    /// can be corrupt, so every field is bounds-checked before it is trusted —
    /// a declared width is never used to size a read until it has been verified
    /// against the bytes actually present.
    bool deserialize(std::string_view blob);

private:
    std::vector<Icon> icons_;
    std::size_t bytesUsed_ = 0;
    std::uint32_t revision_ = 0;
};

}  // namespace asset
}  // namespace stipple
