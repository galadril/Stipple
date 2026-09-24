// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "notrix/graphics/Framebuffer.h"
#include "notrix/platform/Display.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// Channel order on the wire.
///
/// Still unconfirmed. Every lit pixel in the vendor capture was white, and
/// white is the same bytes under all three orders, so this could not be read
/// off the wire - it needs a person to look at the panel while
/// firmware/tools/panel_test draws three deliberately unequal bands. Until then
/// Rgb is the assumption, not the finding.
enum class ChannelOrder { Rgb, Grb, Bgr };

/// The TC002 LED matrix, driven over raw SPI.
///
/// Decoded from the vendor application rather than guessed: an LD_PRELOAD shim
/// over zkgui recorded the whole conversation, and it is short. The panel is
/// /dev/spidev0.0 at mode 0, 8 bits, MSB first, 10 MHz, and a frame is a single
/// 3072-byte write with no header, no addressing and no chunking.
///
/// `libzkhw.so` and its ledc_* API were a detour - that library targets a
/// sibling board with an LED class driver, and /sys/class/leds does not exist
/// here. There is nothing to dlopen, so this adapter links statically.
///
/// The panel is **52 columns wide but addressed as 64**. Columns 52-63 are
/// padding the vendor app never lights.
///
///     3072 = 64 columns x 16 rows x 3 bytes
///     offset(col, row) = (row * 64 + col) * 3
class Tc002Display final : public IFrameBufferDisplay {
public:
    static constexpr int kAddressedWidth = 64;
    static constexpr std::size_t kFrameBytes =
        static_cast<std::size_t>(kAddressedWidth) * Framebuffer::kHeight * 3u;

    explicit Tc002Display(ChannelOrder order = ChannelOrder::Rgb);
    ~Tc002Display() override;

    Tc002Display(const Tc002Display&) = delete;
    Tc002Display& operator=(const Tc002Display&) = delete;

    /// The latch line, and the reason a perfectly good frame lights nothing.
    ///
    /// Writing 3072 bytes to spidev only loads the driver chips' shift
    /// registers. GPIO 35 is what latches them onto the panel: it must go low
    /// before the write and high after. Without it every frame is accepted,
    /// acknowledged, and invisible - which is precisely what we spent an
    /// afternoon observing.
    ///
    /// It also explains why frames flickered through while the vendor app was
    /// running: zkgui was strobing this line on its own schedule, and our data
    /// was whatever happened to be in the registers when it did.
    static constexpr int kLatchGpio = 35;

    /// Opens and configures spidev and the latch GPIO. Returns false and leaves
    /// the object unusable rather than throwing - a clock that cannot reach its
    /// panel should report that and keep running, not abort.
    bool open(const char* devicePath = "/dev/spidev0.0");
    bool isOpen() const noexcept { return fd_ >= 0; }

    /// Blanks the panel and closes the descriptor. Safe to call twice.
    void close() noexcept;

    void present(const Framebuffer& frame) override;

    /// Re-sends the last frame unchanged.
    ///
    /// Needed because these driver chips hold an image only while something
    /// keeps feeding them - stop writing and the panel goes dark, regardless of
    /// what was last sent. Dirty-rectangle rendering must therefore never skip
    /// the write; when core has nothing new, the adapter repeats itself.
    void refresh();

    void setBrightness(std::uint8_t brightness) override;
    std::uint8_t brightness() const override { return brightness_; }

    /// 15 ms, the blueprint §9.4 floor. The bus itself is far faster - 3072
    /// bytes at 10 MHz is about 2.5 ms - so this is a deliberate ceiling on
    /// how hard we drive the panel, not a limit we ran into.
    int minimumFrameIntervalMillis() const override { return 15; }

private:
    void encode(const Framebuffer& frame) noexcept;
    bool writeFrame() noexcept;
    bool openLatch() noexcept;
    /// Drive the latch line. False means the panel did not get the edge,
    /// which is indistinguishable from a dark panel at the other end - so
    /// the result is folded into writeFrame rather than dropped.
    bool strobe(char level) noexcept;

    int fd_ = -1;
    int latchFd_ = -1;
    std::uint8_t brightness_ = 255;
    ChannelOrder order_;
    std::uint8_t buffer_[kFrameBytes] = {};
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
