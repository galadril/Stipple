// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/tc002/Tc002Mcu.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cstring>

namespace notrix {
namespace platform {
namespace tc002 {

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
    ::write(fd_, mcu::kVersionQuery, sizeof(mcu::kVersionQuery));
    return true;
}

void Tc002Mcu::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    held_ = 0;
    // Everything decoded is forgotten along with the port. A percentage from
    // before a link went away is not a reading, it is a memory.
    state_ = mcu::State{};
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

        const int consumed =
            mcu::walk(state_, buffer_, held_, static_cast<int>(sizeof(buffer_)));
        if (consumed > 0) {
            std::memmove(buffer_, buffer_ + consumed,
                         static_cast<std::size_t>(held_ - consumed));
            held_ -= consumed;
        }
    }
}

SoundLevel Tc002Mcu::level() const {
    SoundLevel sound;
    sound.known = state_.micKnown;
    sound.amplitude = state_.micAmplitude;
    return sound;
}

BatteryStatus Tc002Mcu::battery() const {
    BatteryStatus status;
    status.known = state_.batteryKnown;
    status.percent = state_.percent;
    status.millivolts = state_.millivolts;
    status.chargingKnown = state_.chargingKnown;
    status.charging = state_.charging;
    return status;
}

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
