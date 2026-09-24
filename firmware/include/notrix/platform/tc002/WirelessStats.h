// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

namespace notrix {
namespace platform {
namespace tc002 {
namespace wireless {

/// Signal strength, read out of /proc/net/wireless.
///
/// Pure parsing, separated from the file for the same reason the MCU protocol
/// and the rotary decoder were: a text format nobody chose is exactly the code
/// most likely to be wrong, and it cannot be tested at all while it lives in a
/// file only the device can compile.
///
/// The format is the kernel's, and it is awkward on purpose - it was designed
/// to be read by a person:
///
///     Inter-| sta-|   Quality        |   Discarded packets ...
///      face | tus | link level noise |  nwid  crypt ...
///      wlan0: 0000   66.  -44.  -256        0      0 ...
///
/// Two header lines, then one row per interface. The numbers carry a trailing
/// full stop - it marks an updated value, not a decimal point - and dropping
/// that on the floor is how a reader ends up with 44 where it wanted -44.
struct Stats {
    /// False means the interface was not in the file. On this device that
    /// means the radio is down, not that the signal is zero - and a UI showing
    /// "0 dBm" for a missing radio is the same confident lie as a battery at
    /// 0% because nothing answered.
    bool known = false;

    /// Link quality, 0-70 on this hardware. Kept because it is what the driver
    /// is actually confident about; the level below is derived from it.
    int linkQuality = 0;

    /// Signal level in dBm. Negative, and closer to zero is better.
    int levelDbm = 0;
};

/// Find one interface's row in the contents of /proc/net/wireless.
///
/// Header-only, like RotaryDecoder and McuProtocol and for the same reason:
/// the device half of the tree is not compiled for the host, so anything left
/// in a .cpp beside it cannot be tested at all.
///
/// A named detail namespace rather than an anonymous one: anonymous in a
/// header gives every translation unit its own private copy of each helper,
/// which is a quiet way to bloat a binary that has 8 MiB to live in.
namespace detail {

inline bool isSpace(char c) noexcept { return c == ' ' || c == '\t'; }

inline void skipSpace(std::string_view text, std::size_t& at) noexcept {
    while (at < text.size() && isSpace(text[at])) {
        ++at;
    }
}

/// Read a signed integer, tolerating the trailing full stop the kernel writes.
///
/// That stop is a flag meaning "this value was updated", not a decimal point.
/// Treating it as one, or stopping at it and keeping what follows, both give
/// numbers that look plausible and are wrong.
inline bool readInt(std::string_view text, std::size_t& at, int& out) noexcept {
    skipSpace(text, at);
    if (at >= text.size()) {
        return false;
    }

    bool negative = false;
    if (text[at] == '-') {
        negative = true;
        ++at;
    }

    const std::size_t begin = at;
    int value = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        // Bounded. This reads a kernel file, but a bounded parser costs one
        // comparison and removes the question entirely.
        if (value > 1000000) {
            return false;
        }
        value = value * 10 + (text[at] - '0');
        ++at;
    }
    if (at == begin) {
        return false;
    }

    // The update marker, if present.
    if (at < text.size() && text[at] == '.') {
        ++at;
    }

    out = negative ? -value : value;
    return true;
}

}  // namespace detail

inline Stats parse(std::string_view contents, std::string_view interface) noexcept {
    Stats stats;
    if (interface.empty()) {
        return stats;
    }

    std::size_t at = 0;
    while (at < contents.size()) {
        std::size_t lineEnd = contents.find('\n', at);
        if (lineEnd == std::string_view::npos) {
            lineEnd = contents.size();
        }
        const std::string_view line = contents.substr(at, lineEnd - at);
        at = lineEnd + 1;

        // Interface names are indented and followed by a colon. The two header
        // lines contain neither in that arrangement, so matching on it is what
        // lets this skip them without counting lines - a header that gains a
        // line one day should not silently shift which row is read.
        std::size_t nameAt = 0;
        detail::skipSpace(line, nameAt);
        const std::size_t colon = line.find(':', nameAt);
        if (colon == std::string_view::npos) {
            continue;
        }
        if (line.substr(nameAt, colon - nameAt) != interface) {
            continue;
        }

        std::size_t field = colon + 1;
        int status = 0;
        int quality = 0;
        int level = 0;
        if (!detail::readInt(line, field, status) ||
            !detail::readInt(line, field, quality) ||
            !detail::readInt(line, field, level)) {
            return stats;  // the row was there and unreadable, which is not "fine"
        }

        stats.known = true;
        stats.linkQuality = quality;
        stats.levelDbm = level;
        return stats;
    }

    return stats;
}

}  // namespace wireless
}  // namespace tc002
}  // namespace platform
}  // namespace notrix
