// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/tc002/Tc002Mcu.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cstring>

namespace notrix {
namespace platform {
namespace tc002 {
namespace {

constexpr std::uint8_t kHeader0 = 0xff;
constexpr std::uint8_t kHeader1 = 0x55;

/// header(2) + cmd(1) + len(1) + payload(len) + trailer(2)
constexpr int kOverhead = 6;

/// The version query the vendor application sends, byte for byte.
constexpr std::uint8_t kVersionQuery[] = {0xff, 0x55, 0x11, 0x00, 0x01, 0x65};

}  // namespace

Tc002Mcu::~Tc002Mcu() { close(); }

bool Tc002Mcu::open(const char* devicePath) {
    close();

    fd_ = ::open(devicePath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        return false;
    }

    struct termios tty;
    if (::tcgetattr(fd_, &tty) != 0) {
        close();
        return false;
    }

    ::cfmakeraw(&tty);
    // 1.5 Mbaud. Read from the port the vendor application had configured
    // rather than chosen, so it matches whatever the MCU actually expects.
    ::cfsetispeed(&tty, B1500000);
    ::cfsetospeed(&tty, B1500000);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= static_cast<unsigned>(~CRTSCTS);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;  // never block; poll() is called from the render loop

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        close();
        return false;
    }

    // Asking the version is how we know the link works at all. Nothing depends
    // on the answer, but a device that cannot answer it is one whose telemetry
    // should not be trusted either.
    ::write(fd_, kVersionQuery, sizeof(kVersionQuery));
    return true;
}

void Tc002Mcu::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    held_ = 0;
    batteryKnown_ = false;
}

void Tc002Mcu::consume(const std::uint8_t* frame, int length) noexcept {
    const std::uint8_t command = frame[2];
    const int payloadLength = frame[3];
    const std::uint8_t* payload = frame + 4;

    if (command == kTelemetry && payloadLength >= 1) {
        const std::uint8_t raw = payload[0];
        // Anything outside 0-100 is not a percentage, so it is discarded rather
        // than clamped: clamping 200 to 100 would invent a full battery.
        if (raw <= 100) {
            batteryPercent_ = static_cast<int>(raw);
            batteryKnown_ = true;
        }
        return;
    }

    if (command == kVersion && payloadLength > 0) {
        const int copy = payloadLength < static_cast<int>(sizeof(version_)) - 1
                             ? payloadLength
                             : static_cast<int>(sizeof(version_)) - 1;
        std::memcpy(version_, payload, static_cast<std::size_t>(copy));
        version_[copy] = '\0';
    }

    (void)length;
}

void Tc002Mcu::poll() {
    if (fd_ < 0) {
        return;
    }

    for (;;) {
        std::uint8_t chunk[128];
        const ssize_t got = ::read(fd_, chunk, sizeof(chunk));
        if (got <= 0) {
            break;  // EAGAIN is the normal case between telemetry frames
        }

        if (held_ + static_cast<int>(got) > static_cast<int>(sizeof(buffer_))) {
            // Resync rather than grow. A buffer full of bytes that never formed
            // a header is noise, and keeping it would only delay the next good
            // frame.
            held_ = 0;
            if (static_cast<int>(got) > static_cast<int>(sizeof(buffer_))) {
                continue;
            }
        }

        std::memcpy(buffer_ + held_, chunk, static_cast<std::size_t>(got));
        held_ += static_cast<int>(got);

        int at = 0;
        while (at + 4 <= held_) {
            if (buffer_[at] != kHeader0 || buffer_[at + 1] != kHeader1) {
                ++at;
                continue;
            }
            const int total = kOverhead + buffer_[at + 3];
            if (at + total > held_) {
                break;  // the rest is still on the wire
            }
            consume(buffer_ + at, total);
            at += total;
        }

        if (at > 0) {
            std::memmove(buffer_, buffer_ + at, static_cast<std::size_t>(held_ - at));
            held_ -= at;
        }
    }
}

BatteryStatus Tc002Mcu::battery() const {
    BatteryStatus status;
    status.known = batteryKnown_;
    status.percent = batteryPercent_;
    return status;
}

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
