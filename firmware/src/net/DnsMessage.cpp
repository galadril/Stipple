// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/net/DnsMessage.h"

namespace stipple {
namespace net {
namespace dns {
namespace {

constexpr std::size_t kHeaderBytes = 12;

constexpr std::uint16_t kTypeA = 1;
constexpr std::uint16_t kTypeCname = 5;
constexpr std::uint16_t kClassIn = 1;

/// Two high bits set marks a compression pointer; the other fourteen are the
/// offset it points at.
constexpr std::uint8_t kPointerMask = 0xC0;

/// A name is at most 255 bytes, so it cannot legitimately need more jumps
/// than that. Anything beyond is a loop.
constexpr int kMaxJumps = 64;

std::uint16_t readU16(const std::uint8_t* at) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(at[0]) << 8) | at[1]);
}

void writeU16(std::uint8_t* at, std::uint16_t value) noexcept {
    at[0] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    at[1] = static_cast<std::uint8_t>(value & 0xFFu);
}

/// Walk a name and return the offset just past it.
///
/// Only the *length* matters to this parser - the records are matched by
/// position, not by name - so the labels themselves are skipped rather than
/// reassembled. That avoids needing anywhere to put them.
///
/// Returns 0 on anything malformed, which every caller treats as fatal.
std::size_t skipName(const std::uint8_t* data, std::size_t length, std::size_t at) noexcept {
    int jumps = 0;
    std::size_t cursor = at;
    std::size_t afterFirstPointer = 0;

    while (cursor < length) {
        const std::uint8_t byte = data[cursor];

        if (byte == 0) {
            // Root label ends the name. If a pointer was followed, the name
            // in the record ended at the pointer, not here.
            return afterFirstPointer != 0 ? afterFirstPointer : cursor + 1;
        }

        if ((byte & kPointerMask) == kPointerMask) {
            if (cursor + 1 >= length) {
                return 0;
            }
            if (++jumps > kMaxJumps) {
                // A name pointing at itself, directly or round a ring. The
                // cap is what stops this being an endless loop in the render
                // thread.
                return 0;
            }
            const std::size_t target =
                static_cast<std::size_t>(readU16(data + cursor) & 0x3FFFu);
            if (afterFirstPointer == 0) {
                afterFirstPointer = cursor + 2;
            }
            // A pointer must point backwards. Forward pointers are how a
            // hostile message builds a loop that still terminates the jump
            // count on a naive reader.
            if (target >= cursor) {
                return 0;
            }
            cursor = target;
            continue;
        }

        if ((byte & kPointerMask) != 0) {
            // 0b01 and 0b10 are reserved label types.
            return 0;
        }

        const std::size_t label = byte;
        if (label > kMaxLabelBytes || cursor + 1 + label > length) {
            return 0;
        }
        cursor += 1 + label;
    }

    return 0;
}

}  // namespace

const char* describe(Result result) noexcept {
    switch (result) {
        case Result::Ok: return "resolved";
        case Result::Malformed: return "the reply was not a DNS message";
        case Result::WrongId: return "the reply answered a different question";
        case Result::NoSuchName: return "no such host";
        case Result::NoAddress: return "the host has no address of that kind";
        case Result::Truncated: return "the answer was too large for UDP";
        case Result::ServerFailure: return "the nameserver reported a failure";
    }
    return "unrecognised";
}

std::size_t build(std::string_view hostname, std::uint16_t id, std::uint8_t* out,
                  std::size_t outSize) noexcept {
    if (out == nullptr || hostname.empty() || hostname.size() > kMaxNameBytes) {
        return 0;
    }
    // Header, the name as length-prefixed labels plus a root byte, then type
    // and class.
    const std::size_t needed = kHeaderBytes + hostname.size() + 2 + 4;
    if (needed > outSize || needed > kMaxMessageBytes) {
        return 0;
    }

    for (std::size_t i = 0; i < kHeaderBytes; ++i) {
        out[i] = 0;
    }
    writeU16(out, id);
    // Recursion desired. This device asks a resolver to do the walking; it is
    // not one itself.
    out[2] = 0x01;
    writeU16(out + 4, 1);  // one question

    std::size_t cursor = kHeaderBytes;
    std::size_t labelStart = 0;

    for (std::size_t i = 0; i <= hostname.size(); ++i) {
        const bool end = i == hostname.size();
        if (!end && hostname[i] != '.') {
            continue;
        }
        const std::size_t label = i - labelStart;
        // An empty label means "..", or a leading or trailing dot. A trailing
        // dot is legal in a fully qualified name, so it is only rejected when
        // something follows it.
        if (label == 0) {
            if (end && i > 0) {
                break;
            }
            return 0;
        }
        if (label > kMaxLabelBytes) {
            return 0;
        }
        out[cursor++] = static_cast<std::uint8_t>(label);
        for (std::size_t j = 0; j < label; ++j) {
            out[cursor++] = static_cast<std::uint8_t>(hostname[labelStart + j]);
        }
        labelStart = i + 1;
    }

    out[cursor++] = 0;  // root
    writeU16(out + cursor, kTypeA);
    cursor += 2;
    writeU16(out + cursor, kClassIn);
    cursor += 2;
    return cursor;
}

Result parse(const std::uint8_t* data, std::size_t length, std::uint16_t id,
             std::uint32_t& address) noexcept {
    address = 0;

    if (data == nullptr || length < kHeaderBytes || length > kMaxMessageBytes) {
        return Result::Malformed;
    }
    if (readU16(data) != id) {
        // Checked before anything else is believed. Without it, any packet
        // arriving on the socket could answer a question it was not asked.
        return Result::WrongId;
    }

    const std::uint8_t flagsHigh = data[2];
    const std::uint8_t flagsLow = data[3];

    if ((flagsHigh & 0x80u) == 0) {
        return Result::Malformed;  // not a response
    }
    if ((flagsHigh & 0x02u) != 0) {
        return Result::Truncated;
    }

    switch (flagsLow & 0x0Fu) {
        case 0: break;
        case 2: return Result::ServerFailure;
        case 3: return Result::NoSuchName;
        default: return Result::Malformed;
    }

    const std::uint16_t questions = readU16(data + 4);
    const std::uint16_t answers = readU16(data + 6);
    if (answers == 0) {
        return Result::NoAddress;
    }

    std::size_t cursor = kHeaderBytes;

    // Step over the questions. Their names may themselves be compressed.
    for (std::uint16_t i = 0; i < questions; ++i) {
        cursor = skipName(data, length, cursor);
        if (cursor == 0 || cursor + 4 > length) {
            return Result::Malformed;
        }
        cursor += 4;  // type and class
    }

    for (std::uint16_t i = 0; i < answers; ++i) {
        cursor = skipName(data, length, cursor);
        if (cursor == 0 || cursor + 10 > length) {
            return Result::Malformed;
        }
        const std::uint16_t type = readU16(data + cursor);
        const std::uint16_t klass = readU16(data + cursor + 2);
        const std::uint16_t rdLength = readU16(data + cursor + 8);
        cursor += 10;

        if (cursor + rdLength > length) {
            return Result::Malformed;
        }

        if (type == kTypeA && klass == kClassIn && rdLength == 4) {
            // Network byte order, which is what a socket wants.
            address = static_cast<std::uint32_t>(data[cursor]) |
                      (static_cast<std::uint32_t>(data[cursor + 1]) << 8) |
                      (static_cast<std::uint32_t>(data[cursor + 2]) << 16) |
                      (static_cast<std::uint32_t>(data[cursor + 3]) << 24);
            return Result::Ok;
        }

        // A CNAME is skipped rather than followed by hand: a recursive
        // resolver puts the A record for the target in the same reply, and
        // chasing the chain ourselves would mean matching names, which is the
        // part with the compression pointers in it.
        (void)kTypeCname;
        cursor += rdLength;
    }

    return Result::NoAddress;
}

}  // namespace dns
}  // namespace net
}  // namespace stipple
