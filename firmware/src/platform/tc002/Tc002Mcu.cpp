// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/tc002/Tc002Mcu.h"

#include "notrix/platform/tc002/WriteAll.h"

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
    // Checked now, because this is the first thing written to the link. A
    // write that fails here means the port opened but is not usable, and
    // reporting that as a working MCU would make every reading after it a
    // confident guess.
    if (!writeAll(fd_, mcu::kVersionQuery, sizeof(mcu::kVersionQuery))) {
        return false;
    }

    // The microphone is switched on in poll(), not here. See requestMicrophone.
    return true;
}

void Tc002Mcu::requestMicrophone() {
    // The MCU sends no audio until asked, and the ask is sticky - it keeps
    // streaming until told to stop or until power is lost. That is the whole
    // history of this feature: the vendor application enabled it, NOTRIX
    // inherited a microphone it had never requested, the visualiser worked, and
    // a reboot silently took it away again.
    //
    // **Sent only once the MCU has answered something.** Writing it straight
    // after the version query did not work, and the wire says why: every frame
    // we send is acknowledged with a 0xfe, and a second frame written before
    // the first was acknowledged is dropped on the floor. It returns 7 from
    // write() either way, which is what made this look like a hardware
    // limitation rather than a handshake.
    //
    // Waiting on the version reply is the natural gate. It is the one thing the
    // MCU always says, it proves the link is alive, and it arrives within a few
    // milliseconds - so the microphone is on well before anything could want it.
    if (micRequested_ || state_.version[0] == '\0') {
        return;
    }
    // The flag is only set if the request actually went out, so a failed
    // write is retried on the next poll rather than leaving the microphone
    // off and this object convinced it asked.
    if (writeAll(fd_, mcu::kMicOn, sizeof(mcu::kMicOn))) {
        micRequested_ = true;
    }
}

void Tc002Mcu::close() noexcept {
    if (fd_ >= 0) {
        // Hand the microphone back. The enable outlives this process, so
        // leaving it on would mean a device that had once run NOTRIX kept
        // streaming audio to whatever ran next, which is both impolite and the
        // kind of state that makes the next person's capture lie to them - as
        // it did to ours.
        // Best effort, and deliberately so: the port is closing either way
        // and there is nothing left to retry with.
        const bool handedBack = writeAll(fd_, mcu::kMicOff, sizeof(mcu::kMicOff));
        static_cast<void>(handedBack);
        ::close(fd_);
        fd_ = -1;
    }
    held_ = 0;
    micRequested_ = false;
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

    requestMicrophone();
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
