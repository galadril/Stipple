// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/Tc002Input.h"

#include "stipple/platform/tc002/RotaryDecoder.h"

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

namespace stipple {
namespace platform {
namespace tc002 {
namespace {

// Declared rather than included from <linux/input.h>, for the same reason as
// the spidev ioctls in Tc002Display: the layout is stable kernel ABI, and a
// build-host header is an assumption about the device's kernel. On 32-bit ARM
// this is 8 + 2 + 2 + 4 = 16 bytes.
struct EvdevEvent {
    long seconds;
    long microseconds;
    std::uint16_t type;
    std::uint16_t code;
    std::int32_t value;
};

constexpr std::uint16_t kEvKey = 0x01;
constexpr std::uint16_t kEvAbs = 0x03;

constexpr std::int32_t kPressed = 1;
constexpr std::int32_t kReleased = 0;

// Measured on hardware 2026-09-20. The names are arrow keys because the device
// tree assigned four arrow codes to four GPIOs; they carry no meaning.
constexpr std::uint16_t kCodeMinus = 108;   // KEY_DOWN
constexpr std::uint16_t kCodeMiddle = 105;  // KEY_LEFT
constexpr std::uint16_t kCodePlus = 106;    // KEY_RIGHT
constexpr std::uint16_t kCodeKnobPress = 103;  // KEY_UP

/// CLOCK_MONOTONIC, which must stay the same source the device ISystemClock
/// uses: InputMapper derives press duration and rotary acceleration by
/// subtracting these from clock readings, and two different clocks would make
/// a long press look negative.
std::uint64_t nowMillis() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000u +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000000u;
}

}  // namespace

Tc002Input::~Tc002Input() { close(); }

bool Tc002Input::open(const char* keysPath, const char* knobPath) {
    close();

    // Non-blocking: poll() is called from the render loop, and a read that
    // waited for a button would stall the panel.
    keysFd_ = ::open(keysPath, O_RDONLY | O_NONBLOCK);
    knobFd_ = ::open(knobPath, O_RDONLY | O_NONBLOCK);

    if (keysFd_ < 0 || knobFd_ < 0) {
        close();
        return false;
    }
    return true;
}

void Tc002Input::close() noexcept {
    if (keysFd_ >= 0) {
        ::close(keysFd_);
        keysFd_ = -1;
    }
    if (knobFd_ >= 0) {
        ::close(knobFd_);
        knobFd_ = -1;
    }
}

void Tc002Input::push(RawInput source, ButtonPhase phase) noexcept {
    if (count_ == kQueueCapacity) {
        // Drop the oldest, per IInputDevice. Dropping the newest would let one
        // stuck control hide every later press.
        head_ = (head_ + 1) % kQueueCapacity;
        --count_;
        ++dropped_;
    }

    InputEvent& slot = queue_[(head_ + count_) % kQueueCapacity];
    slot.source = source;
    slot.phase = phase;
    slot.timestampMillis = nowMillis();
    ++count_;
}

void Tc002Input::drainKeys() noexcept {
    EvdevEvent raw;
    while (::read(keysFd_, &raw, sizeof(raw)) == static_cast<ssize_t>(sizeof(raw))) {
        if (raw.type != kEvKey) {
            continue;
        }

        // Autorepeat (value 2) is deliberately ignored. InputMapper derives
        // long-press from Down/Up timestamps, so a repeat stream would look
        // like a burst of presses and fire an action per repeat.
        if (raw.value != kPressed && raw.value != kReleased) {
            continue;
        }

        const ButtonPhase phase =
            (raw.value == kPressed) ? ButtonPhase::Down : ButtonPhase::Up;

        switch (raw.code) {
            case kCodeMinus: push(RawInput::KeyMinus, phase); break;
            case kCodeMiddle: push(RawInput::KeyMiddle, phase); break;
            case kCodePlus: push(RawInput::KeyPlus, phase); break;
            case kCodeKnobPress: push(RawInput::RotaryPress, phase); break;
            default: break;
        }
    }
}

void Tc002Input::drainKnob() noexcept {
    EvdevEvent raw;
    while (::read(knobFd_, &raw, sizeof(raw)) == static_cast<ssize_t>(sizeof(raw))) {
        if (raw.type != kEvAbs) {
            continue;
        }

        // One detent, one tick - and a detent is a *pair* of ABS_X values, not
        // one. See RotaryDecoder.h: firing on both halves advanced the carousel
        // two apps per click.
        switch (decodeRotary(raw.value)) {
            case Detent::Clockwise:
                push(RawInput::RotaryRight, ButtonPhase::Tick);
                break;
            case Detent::CounterClockwise:
                push(RawInput::RotaryLeft, ButtonPhase::Tick);
                break;
            case Detent::None:
                break;
        }
    }
}

void Tc002Input::drain() noexcept {
    if (keysFd_ >= 0) {
        drainKeys();
    }
    if (knobFd_ >= 0) {
        drainKnob();
    }
}

bool Tc002Input::poll(InputEvent& event) {
    drain();

    if (count_ == 0) {
        return false;
    }

    event = queue_[head_];
    head_ = (head_ + 1) % kQueueCapacity;
    --count_;
    return true;
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
