// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/core/Log.h"

namespace stipple {
namespace log {

const char* levelName(Level level) noexcept {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

void RingLog::write(Level level, std::uint64_t timestampMillis,
                    std::string_view message) noexcept {
    if (level < minimumLevel_) {
        return;
    }

    // Where the new entry goes: append while there is room, otherwise overwrite
    // the oldest and advance the head.
    int slot = 0;
    if (count_ < kCapacity) {
        slot = (head_ + count_) % kCapacity;
        ++count_;
    } else {
        slot = head_;
        head_ = (head_ + 1) % kCapacity;
    }

    Entry& entry = entries_[slot];
    entry.timestampMillis = timestampMillis;
    entry.level = level;

    // Truncate rather than drop: a cut-off line still says what happened.
    const std::size_t copied =
        message.size() < kMaxMessageBytes - 1 ? message.size() : kMaxMessageBytes - 1;
    for (std::size_t i = 0; i < copied; ++i) {
        entry.message[i] = message[i];
    }
    entry.message[copied] = '\0';

    ++totalWritten_;
}

const RingLog::Entry& RingLog::at(int index) const noexcept {
    static const Entry empty{};
    if (index < 0 || index >= count_) {
        return empty;
    }
    return entries_[(head_ + index) % kCapacity];
}

void RingLog::clear() noexcept {
    head_ = 0;
    count_ = 0;
}

}  // namespace log
}  // namespace stipple
