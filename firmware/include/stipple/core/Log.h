// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace stipple {
namespace log {

enum class Level : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

const char* levelName(Level level) noexcept;

/// Fixed-capacity in-memory log (blueprint §22).
///
/// A ring buffer, deliberately: the device has flash that wears out and no
/// business writing a log line to it every second. Everything here is
/// inline storage with a compile-time size, so logging allocates nothing and
/// cannot grow — which also means it cannot be the thing that exhausts RAM
/// during an incident, when logging is busiest.
///
/// Messages longer than the limit are truncated rather than dropped: a
/// truncated line still says what happened.
///
/// Cost is `kCapacity * kMaxMessageBytes` plus a little; see the memory-budget
/// test, which asserts it stays within the panel's own footprint.
class RingLog {
public:
    /// Sized against an unknown RAM budget: at 24 x 80 this costs roughly 2 KB,
    /// comparable to the framebuffer itself. Deliberately conservative — it is
    /// far easier to raise once Phase 7 measures real headroom than to discover
    /// on hardware that the log is what pushed the device over.
    static constexpr int kCapacity = 24;
    static constexpr std::size_t kMaxMessageBytes = 80;

    struct Entry {
        std::uint64_t timestampMillis = 0;
        Level level = Level::Info;
        /// Always NUL-terminated.
        char message[kMaxMessageBytes] = {};
    };

    void write(Level level, std::uint64_t timestampMillis, std::string_view message) noexcept;

    void trace(std::uint64_t at, std::string_view m) noexcept { write(Level::Trace, at, m); }
    void debug(std::uint64_t at, std::string_view m) noexcept { write(Level::Debug, at, m); }
    void info(std::uint64_t at, std::string_view m) noexcept { write(Level::Info, at, m); }
    void warn(std::uint64_t at, std::string_view m) noexcept { write(Level::Warn, at, m); }
    void error(std::uint64_t at, std::string_view m) noexcept { write(Level::Error, at, m); }
    void fatal(std::uint64_t at, std::string_view m) noexcept { write(Level::Fatal, at, m); }

    /// Entries currently held, oldest first. Never more than kCapacity.
    int count() const noexcept { return count_; }

    /// `index` 0 is the oldest retained entry.
    const Entry& at(int index) const noexcept;

    /// Total ever written, including entries since overwritten. The difference
    /// against `count()` is how much history was lost.
    std::uint32_t totalWritten() const noexcept { return totalWritten_; }

    /// Below this level, `write` returns immediately. Defaults to Info so a
    /// shipped device is not spending its ring on trace chatter.
    void setMinimumLevel(Level level) noexcept { minimumLevel_ = level; }
    Level minimumLevel() const noexcept { return minimumLevel_; }

    void clear() noexcept;

private:
    Entry entries_[kCapacity];
    int head_ = 0;  ///< index of the oldest entry
    int count_ = 0;
    std::uint32_t totalWritten_ = 0;
    Level minimumLevel_ = Level::Info;
};

}  // namespace log
}  // namespace stipple
