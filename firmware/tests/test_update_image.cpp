// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/update/UpdateImage.h"

#include <string>
#include <vector>

#include "support/TestFramework.h"

using notrix::update::inspect;
using notrix::update::kDeviceCode;
using notrix::update::kHeaderBytes;
using notrix::update::kOffDeviceCode;
using notrix::update::kOffHeaderCrc;
using notrix::update::kOffPartition;
using notrix::update::kOffPayloadAt;
using notrix::update::kOffPayloadLen;
using notrix::update::kOffRelocated;
using notrix::update::kRelocatedBytes;
using notrix::update::kResPartition;
using notrix::update::Report;

namespace {

void writeU32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    out[at] = static_cast<std::uint8_t>(value & 0xFFu);
    out[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    out[at + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    out[at + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

/// Build a container the way the vendor's packer does, so the tests exercise
/// the real shape rather than an approximation of it.
///
/// Not a copy of a factory image: those are Ulanzi's firmware and do not
/// belong in a public repository. The layout is what was measured, and the
/// two checksums are computed the way the device computes them.
std::vector<std::uint8_t> makeImage(std::size_t payloadBytes = 4096) {
    std::vector<std::uint8_t> filesystem(payloadBytes, 0);
    filesystem[0] = 'h';
    filesystem[1] = 's';
    filesystem[2] = 'q';
    filesystem[3] = 's';
    for (std::size_t i = 4; i < filesystem.size(); ++i) {
        filesystem[i] = static_cast<std::uint8_t>((i * 37u) & 0xFFu);
    }

    std::vector<std::uint8_t> out(kHeaderBytes, 0);
    const std::string magic = "ZKSWEV1.0-260922";
    for (std::size_t i = 0; i < magic.size(); ++i) {
        out[i] = static_cast<std::uint8_t>(magic[i]);
    }
    out[0x10] = 0x30;
    out[0x11] = 1;
    out[kOffPartition] = static_cast<std::uint8_t>(kResPartition);
    writeU32(out, kOffPayloadAt, static_cast<std::uint32_t>(kHeaderBytes));
    writeU32(out, kOffPayloadLen, static_cast<std::uint32_t>(filesystem.size()));
    for (std::size_t i = 0; i < kRelocatedBytes; ++i) {
        out[kOffRelocated + i] = filesystem[i];
    }
    writeU32(out, kOffDeviceCode, kDeviceCode);

    // The MD5 covers the filesystem as it will be written, then takes the
    // place of its first sixteen bytes in the payload.
    notrix::Md5 md5;
    md5.update(filesystem.data(), filesystem.size());
    std::uint8_t digest[notrix::Md5::kDigestBytes];
    md5.finish(digest);

    std::vector<std::uint8_t> payload = filesystem;
    for (std::size_t i = 0; i < kRelocatedBytes; ++i) {
        payload[i] = digest[i];
    }

    // Last, because it covers everything above it.
    writeU32(out, kOffHeaderCrc, notrix::crc32(out.data(), kOffHeaderCrc));

    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

Report check(const std::vector<std::uint8_t>& image) {
    return inspect(image.data(), image.size());
}

}  // namespace

NOTRIX_TEST(UpdateImage, AcceptsAWellFormedImage) {
    const Report report = check(makeImage());
    NOTRIX_CHECK(report.ok);
    NOTRIX_CHECK_EQ(report.problem, std::string());
    NOTRIX_CHECK_EQ(report.partition, kResPartition);
    NOTRIX_CHECK_EQ(report.payloadBytes, std::size_t(4096));
    NOTRIX_CHECK(report.squashfs);
    NOTRIX_CHECK_EQ(report.payloadMd5.size(), std::size_t(32));
}

NOTRIX_TEST(UpdateImage, RefusesSomethingThatIsNotAnUpdateImage) {
    std::vector<std::uint8_t> nonsense(2000, 0x41);
    Report report = inspect(nonsense.data(), nonsense.size());
    NOTRIX_CHECK_FALSE(report.ok);
    NOTRIX_CHECK(report.problem.find("magic") != std::string::npos);

    // And the empty case, which is what an upload that failed looks like.
    report = inspect(nullptr, 0);
    NOTRIX_CHECK_FALSE(report.ok);
    NOTRIX_CHECK(report.problem.find("too short") != std::string::npos);
}

NOTRIX_TEST(UpdateImage, RefusesAnImageForAnotherBoard) {
    std::vector<std::uint8_t> image = makeImage();
    writeU32(image, kOffDeviceCode, 0x12345678u);
    writeU32(image, kOffHeaderCrc, notrix::crc32(image.data(), kOffHeaderCrc));

    const Report report = check(image);
    NOTRIX_CHECK_FALSE(report.ok);
    NOTRIX_CHECK(report.problem.find("different device") != std::string::npos);
}

NOTRIX_TEST(UpdateImage, RefusesAnyPartitionButRes) {
    // Only res is written directly by the loader, and only res can be broken
    // without stopping the device booting far enough to be talked to.
    for (int partition : {0, 1, 2, 4, 6}) {
        std::vector<std::uint8_t> image = makeImage();
        image[kOffPartition] = static_cast<std::uint8_t>(partition);
        writeU32(image, kOffHeaderCrc, notrix::crc32(image.data(), kOffHeaderCrc));

        const Report report = check(image);
        NOTRIX_CHECK_FALSE(report.ok);
        NOTRIX_CHECK(report.problem.find("will not write") != std::string::npos);
    }
}

NOTRIX_TEST(UpdateImage, CatchesADamagedHeader) {
    std::vector<std::uint8_t> image = makeImage();
    image[0x11] ^= 0xFFu;  // change a header byte, leave the CRC alone

    const Report report = check(image);
    NOTRIX_CHECK_FALSE(report.ok);
    NOTRIX_CHECK(report.problem.find("header checksum") != std::string::npos);
}

NOTRIX_TEST(UpdateImage, CatchesADamagedPayload) {
    // The one that matters most. A header can be intact while the megabytes
    // behind it arrived corrupted, and this is what stands between that and
    // a recovery image that fails when somebody needs it.
    std::vector<std::uint8_t> image = makeImage();
    image[kHeaderBytes + 2000] ^= 0xFFu;

    const Report report = check(image);
    NOTRIX_CHECK_FALSE(report.ok);
    NOTRIX_CHECK(report.problem.find("damaged") != std::string::npos);
}

NOTRIX_TEST(UpdateImage, CatchesATruncatedUpload) {
    // What a dropped connection produces, and it has to be caught: half an
    // image staged as a recovery is worse than none.
    std::vector<std::uint8_t> image = makeImage();
    image.resize(image.size() - 64);

    const Report report = check(image);
    NOTRIX_CHECK_FALSE(report.ok);
    NOTRIX_CHECK(report.problem.find("truncated") != std::string::npos);
}

NOTRIX_TEST(UpdateImage, RefusesMoreThanThePartitionHolds) {
    std::vector<std::uint8_t> image = makeImage();
    image.resize(kHeaderBytes + 0x800000 + 1);

    const Report report = check(image);
    NOTRIX_CHECK_FALSE(report.ok);
    NOTRIX_CHECK(report.problem.find("larger than the partition") != std::string::npos);
}

NOTRIX_TEST(UpdateImage, NoticesWhenThePayloadIsNotASquashfs) {
    // Not refused - a future res image might legitimately be something else -
    // but reported, because every one seen so far is squashfs and a surprise
    // is worth surfacing.
    std::vector<std::uint8_t> image = makeImage();
    NOTRIX_CHECK(check(image).squashfs);

    // Rebuild with a different magic, keeping both checksums honest.
    std::vector<std::uint8_t> filesystem(4096, 0);
    filesystem[0] = 'N';
    filesystem[1] = 'O';
    filesystem[2] = 'P';
    filesystem[3] = 'E';
    std::vector<std::uint8_t> rebuilt = makeImage();
    for (std::size_t i = 0; i < kRelocatedBytes; ++i) {
        rebuilt[kOffRelocated + i] = filesystem[i];
    }
    notrix::Md5 md5;
    md5.update(filesystem.data(), filesystem.size());
    std::uint8_t digest[notrix::Md5::kDigestBytes];
    md5.finish(digest);
    for (std::size_t i = 0; i < 4096; ++i) {
        rebuilt[kHeaderBytes + i] = filesystem[i];
    }
    for (std::size_t i = 0; i < kRelocatedBytes; ++i) {
        rebuilt[kHeaderBytes + i] = digest[i];
    }
    writeU32(rebuilt, kOffHeaderCrc, notrix::crc32(rebuilt.data(), kOffHeaderCrc));

    const Report report = check(rebuilt);
    NOTRIX_CHECK(report.ok);
    NOTRIX_CHECK_FALSE(report.squashfs);
}
