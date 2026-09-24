// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "notrix/core/Checksum.h"
#include "notrix/core/Md5.h"

namespace notrix {
namespace update {

/// Checking a TC002 ``update.img`` before it is allowed anywhere near flash.
///
/// The device's own loader validates a header CRC32 and a payload MD5 and
/// refuses anything that fails either, so this is not the last line of
/// defence. It is the *useful* one: an image that fails here is rejected
/// while somebody is watching a web page, rather than at the moment they are
/// holding the reset button on a clock that will not start.
///
/// That asymmetry is the whole argument for doing it twice. The cost of
/// checking early is a few hundred milliseconds. The cost of not checking is
/// a recovery image that turns out to be broken exactly when it is needed.
///
/// Layout confirmed byte for byte against a factory image, then proven by
/// rebuilding that image and getting the same bytes back. See
/// docs/research/tc002-platform-findings.md and
/// docs/adr/0020-persistence-through-the-vendor-update-path.md.

inline constexpr std::size_t kHeaderBytes = 0x23C;  // 572

inline constexpr std::size_t kOffMagic = 0x00;
inline constexpr std::size_t kOffPartition = 0x14;
inline constexpr std::size_t kOffPayloadAt = 0x18;
inline constexpr std::size_t kOffPayloadLen = 0x1C;
inline constexpr std::size_t kOffRelocated = 0x20;
inline constexpr std::size_t kOffDeviceCode = 0x35;
inline constexpr std::size_t kOffHeaderCrc = 0x238;

/// The first 16 bytes of the filesystem move into the header, and the MD5
/// takes their place at the start of the payload. Both are 16 bytes, which
/// is presumably why it was done this way.
inline constexpr std::size_t kRelocatedBytes = 16;

inline constexpr std::string_view kMagic = "ZKSWEV1.0";
inline constexpr std::uint32_t kDeviceCode = 0xAA550606u;
inline constexpr int kResPartition = 3;

/// The res partition is 8 MiB. Anything larger cannot be written and is
/// refused here rather than part-way through flashing a clock.
inline constexpr std::size_t kMaxPayloadBytes = 0x800000;

/// What the caller gets back. `ok` false means every other field is
/// meaningless and `problem` says why, in a sentence meant for a person.
struct Report {
    bool ok = false;
    std::string problem;

    int partition = 0;
    std::size_t payloadBytes = 0;
    std::uint32_t headerCrc = 0;
    std::string payloadMd5;
    /// True when the payload is a squashfs, which every `res` image so far
    /// has been.
    bool squashfs = false;
};

namespace detail {

inline std::uint32_t readU32(const std::uint8_t* at) noexcept {
    return static_cast<std::uint32_t>(at[0]) |
           (static_cast<std::uint32_t>(at[1]) << 8) |
           (static_cast<std::uint32_t>(at[2]) << 16) |
           (static_cast<std::uint32_t>(at[3]) << 24);
}

}  // namespace detail

/// Check an image. Reads it in place and copies nothing — on a device with
/// 36 MB of RAM, a second copy of a three-megabyte image is not free.
inline Report inspect(const std::uint8_t* data, std::size_t size) {
    Report report;

    if (data == nullptr || size < kHeaderBytes) {
        report.problem = "too short to be an update image";
        return report;
    }
    if (size > kHeaderBytes + kMaxPayloadBytes) {
        report.problem = "larger than the partition it would be written to";
        return report;
    }

    if (std::string_view(reinterpret_cast<const char*>(data), kMagic.size()) != kMagic) {
        report.problem = "not an update image (wrong magic)";
        return report;
    }

    const std::uint32_t device = detail::readU32(&data[kOffDeviceCode]);
    if (device != kDeviceCode) {
        // A different device code means a different board. Writing it would
        // be refused by the loader; saying so here means somebody finds out
        // before they rely on it.
        report.problem = "built for a different device";
        return report;
    }

    report.partition = data[kOffPartition];
    if (report.partition != kResPartition) {
        // Only `res` is written directly by the loader, and only `res` can
        // be broken without stopping the device from booting far enough to
        // be talked to. Nothing else is accepted here.
        report.problem = "targets a partition NOTRIX will not write";
        return report;
    }

    const std::uint32_t payloadAt = detail::readU32(&data[kOffPayloadAt]);
    const std::uint32_t payloadLen = detail::readU32(&data[kOffPayloadLen]);
    if (payloadAt != kHeaderBytes) {
        report.problem = "payload is not where the header says it should be";
        return report;
    }
    if (static_cast<std::size_t>(payloadAt) + payloadLen != size) {
        report.problem = "truncated, or longer than the header claims";
        return report;
    }
    if (payloadLen < kRelocatedBytes) {
        report.problem = "payload is too small to be a filesystem";
        return report;
    }

    const std::uint32_t storedCrc = detail::readU32(&data[kOffHeaderCrc]);
    const std::uint32_t actualCrc = crc32(data, kOffHeaderCrc);
    if (storedCrc != actualCrc) {
        report.problem = "header checksum does not match";
        return report;
    }
    report.headerCrc = storedCrc;

    // The MD5 covers the image as it will be written - with its real first
    // sixteen bytes back in place, not as they sit in the file. Fed in two
    // pieces so nothing is copied.
    const std::uint8_t* payload = data + payloadAt;
    Md5 md5;
    md5.update(&data[kOffRelocated], kRelocatedBytes);
    md5.update(payload + kRelocatedBytes, payloadLen - kRelocatedBytes);

    std::uint8_t computed[Md5::kDigestBytes];
    md5.finish(computed);
    for (std::size_t i = 0; i < Md5::kDigestBytes; ++i) {
        if (computed[i] != payload[i]) {
            report.problem = "payload checksum does not match - the file is damaged";
            return report;
        }
    }

    static const char kDigits[] = "0123456789abcdef";
    report.payloadMd5.reserve(Md5::kDigestBytes * 2);
    for (const std::uint8_t byte : computed) {
        report.payloadMd5.push_back(kDigits[(byte >> 4) & 0x0Fu]);
        report.payloadMd5.push_back(kDigits[byte & 0x0Fu]);
    }

    report.payloadBytes = payloadLen;
    report.squashfs = data[kOffRelocated] == 'h' && data[kOffRelocated + 1] == 's' &&
                      data[kOffRelocated + 2] == 'q' && data[kOffRelocated + 3] == 's';
    report.ok = true;
    return report;
}

inline Report inspect(std::string_view image) {
    return inspect(reinterpret_cast<const std::uint8_t*>(image.data()), image.size());
}

}  // namespace update
}  // namespace notrix
