// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/tc002/Tc002Display.h"

#include "notrix/platform/tc002/WriteAll.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace notrix {
namespace platform {
namespace tc002 {
namespace {

// Taken from the ioctl requests the vendor application actually issued, not
// from <linux/spi/spidev.h>.
//
// The capture is ground truth for this kernel; a header on the build host is an
// assumption about it. They agree today - magic 'k', nr 1-4 - and if they ever
// stop agreeing, the device is right and the header is wrong.
constexpr unsigned long kWriteMode = 0x40016b01u;
constexpr unsigned long kWriteLsbFirst = 0x40016b02u;
constexpr unsigned long kWriteBitsPerWord = 0x40016b03u;
constexpr unsigned long kWriteMaxSpeedHz = 0x40046b04u;

constexpr std::uint8_t kMode = 0;
constexpr std::uint8_t kBitsPerWord = 8;
constexpr std::uint8_t kLsbFirst = 0;
constexpr std::uint32_t kSpeedHz = 10000000;

/// Brightness is applied here rather than by apps, so scenes always describe
/// true colour and the panel decides how hard to drive it. Integer maths on
/// purpose: this runs per channel, 3072 times a frame, on a Cortex-A7.
inline std::uint8_t scale(std::uint8_t value, std::uint8_t brightness) noexcept {
    return static_cast<std::uint8_t>((static_cast<unsigned>(value) *
                                      static_cast<unsigned>(brightness)) /
                                     255u);
}

}  // namespace

Tc002Display::Tc002Display(ChannelOrder order) : order_(order) {}

Tc002Display::~Tc002Display() { close(); }

bool Tc002Display::openLatch() noexcept {
    // Export is idempotent in effect but not in return value: a GPIO already
    // exported fails with EBUSY, which is success for our purposes. Nothing
    // here unexports on close - another process may be using the same line, and
    // tearing it out from under them is worse than leaking an export.
    const int exportFd = ::open("/sys/class/gpio/export", O_WRONLY);
    if (exportFd >= 0) {
        char number[8];
        const int length = std::snprintf(number, sizeof(number), "%d", kLatchGpio);
        if (length > 0) {
            // Allowed to fail: the pin may already be exported by a previous
            // run, which is the documented reason this does not unexport on
            // close. The open below is the real test.
            const bool exported = writeAll(exportFd, number, static_cast<std::size_t>(length));
            static_cast<void>(exported);
        }
        ::close(exportFd);
    }

    char path[64];
    std::snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", kLatchGpio);
    const int directionFd = ::open(path, O_WRONLY);
    if (directionFd < 0) {
        return false;
    }
    const bool wrote = ::write(directionFd, "out", 3) == 3;
    ::close(directionFd);
    if (!wrote) {
        return false;
    }

    // Held open for the lifetime of the display. This is written twice per
    // frame, and re-opening a sysfs file 120 times a second would be the most
    // expensive thing in the render path.
    std::snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", kLatchGpio);
    latchFd_ = ::open(path, O_WRONLY);
    return latchFd_ >= 0;
}

bool Tc002Display::strobe(char level) noexcept {
    if (latchFd_ < 0) {
        return false;
    }
    return writeAll(latchFd_, &level, 1);
}

bool Tc002Display::open(const char* devicePath) {
    if (fd_ >= 0) {
        return true;
    }

    fd_ = ::open(devicePath, O_RDWR);
    if (fd_ < 0) {
        return false;
    }

    // Order copied from the capture. zkgui also reads each value back; we do
    // not, because a driver that accepted the write and reports something else
    // is a kernel bug we could not act on anyway.
    if (::ioctl(fd_, kWriteMode, &kMode) < 0 ||
        ::ioctl(fd_, kWriteLsbFirst, &kLsbFirst) < 0 ||
        ::ioctl(fd_, kWriteBitsPerWord, &kBitsPerWord) < 0 ||
        ::ioctl(fd_, kWriteMaxSpeedHz, &kSpeedHz) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // A panel we can load but never latch is worse than one we cannot reach:
    // every write succeeds and nothing appears. Fail here instead.
    if (!openLatch()) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    return true;
}

void Tc002Display::close() noexcept {
    if (fd_ < 0) {
        return;
    }

    // Leave the panel dark rather than holding whatever was last drawn. A
    // process that exits should not leave its final frame burned on the clock.
    std::memset(buffer_, 0, sizeof(buffer_));
    writeFrame();

    ::close(fd_);
    fd_ = -1;

    if (latchFd_ >= 0) {
        ::close(latchFd_);
        latchFd_ = -1;
    }
}

void Tc002Display::encode(const Framebuffer& frame) noexcept {
    // The 12 padding columns are never touched after construction: they are
    // zero-initialised and the vendor app never lit them either.
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const Rgb pixel = frame.at(x, y);
            const std::size_t at =
                (static_cast<std::size_t>(y) * kAddressedWidth +
                 static_cast<std::size_t>(x)) * 3u;

            const std::uint8_t r = scale(pixel.r, brightness_);
            const std::uint8_t g = scale(pixel.g, brightness_);
            const std::uint8_t b = scale(pixel.b, brightness_);

            switch (order_) {
                case ChannelOrder::Rgb:
                    buffer_[at] = r;
                    buffer_[at + 1] = g;
                    buffer_[at + 2] = b;
                    break;
                case ChannelOrder::Grb:
                    buffer_[at] = g;
                    buffer_[at + 1] = r;
                    buffer_[at + 2] = b;
                    break;
                case ChannelOrder::Bgr:
                    buffer_[at] = b;
                    buffer_[at + 1] = g;
                    buffer_[at + 2] = r;
                    break;
            }
        }
    }
}

bool Tc002Display::writeFrame() noexcept {
    if (fd_ < 0) {
        return false;
    }

    // Low, load, high. The SPI write only fills the driver chips' shift
    // registers; the rising edge is what puts them on the panel.
    // All three are attempted even if one fails. Returning early after a
    // failed low strobe would leave the latch held low, which is a worse
    // state to walk away from than a dropped frame.
    const bool low = strobe('0');
    const bool sent = writeAll(fd_, buffer_, sizeof(buffer_));
    const bool high = strobe('1');

    return low && sent && high;
}

void Tc002Display::present(const Framebuffer& frame) {
    encode(frame);
    writeFrame();
}

void Tc002Display::refresh() { writeFrame(); }

void Tc002Display::setBrightness(std::uint8_t brightness) {
    brightness_ = brightness;
}

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
