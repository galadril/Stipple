// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace notrix {
namespace update {
namespace elf {

/// Is this an ARM 32-bit little-endian shared object?
///
/// The whole check, and deliberately no more than that. It answers one
/// question - "could this file possibly be a NOTRIX build for this device" -
/// and it answers it from the first twenty bytes, before anything is written
/// anywhere.
///
/// **Why it exists.** Uploading firmware is the one operation where getting it
/// wrong is expensive: a bad file replaces a working application, and the
/// device that would tell you about it is the one that just stopped working.
/// A build for the wrong architecture is the easy mistake - it happened during
/// bring-up, where a library built in the wrong container flashed and verified
/// perfectly and then failed at `dlopen` with a message only visible over ADB.
///
/// What it cannot tell you is whether the code is *correct*, or built against
/// an ABI this device has. Nothing static can. That is what the shim's
/// three-tier fallback is for: a library that loads but misbehaves is still
/// one deleted file away from the version it replaced.
enum class ElfVerdict : std::uint8_t {
    Ok,
    TooShort,
    NotAnElf,
    /// 64-bit, or big-endian: a host build, almost always.
    WrongClass,
    /// Not ET_DYN - an executable or an object file rather than a library.
    NotASharedObject,
    /// Built for something that is not ARM.
    WrongArchitecture,
};

/// A sentence for a human, matching the verdict.
inline const char* describe(ElfVerdict verdict) noexcept {
    switch (verdict) {
        case ElfVerdict::Ok: return "an ARM shared library";
        case ElfVerdict::TooShort: return "too short to be a library";
        case ElfVerdict::NotAnElf: return "not an ELF file";
        case ElfVerdict::WrongClass: return "not a 32-bit little-endian binary";
        case ElfVerdict::NotASharedObject: return "not a shared library";
        case ElfVerdict::WrongArchitecture: return "not built for ARM";
    }
    return "unrecognised";
}

inline ElfVerdict inspect(std::string_view image) noexcept {
    // e_machine ends at byte 20, so anything shorter cannot be judged.
    constexpr std::size_t kHeaderBytes = 20;
    if (image.size() < kHeaderBytes) {
        return ElfVerdict::TooShort;
    }

    const auto byteAt = [&image](std::size_t index) -> std::uint8_t {
        return static_cast<std::uint8_t>(image[index]);
    };

    if (byteAt(0) != 0x7F || byteAt(1) != 'E' || byteAt(2) != 'L' || byteAt(3) != 'F') {
        return ElfVerdict::NotAnElf;
    }

    // EI_CLASS 1 is 32-bit, EI_DATA 1 is little-endian. A host build trips
    // this first and is the most common wrong file to upload.
    if (byteAt(4) != 1 || byteAt(5) != 1) {
        return ElfVerdict::WrongClass;
    }

    // e_type at 16, little-endian. 3 is ET_DYN.
    const std::uint16_t type =
        static_cast<std::uint16_t>(byteAt(16) | (static_cast<std::uint16_t>(byteAt(17)) << 8));
    if (type != 3) {
        return ElfVerdict::NotASharedObject;
    }

    // e_machine at 18. 40 is EM_ARM.
    const std::uint16_t machine =
        static_cast<std::uint16_t>(byteAt(18) | (static_cast<std::uint16_t>(byteAt(19)) << 8));
    if (machine != 40) {
        return ElfVerdict::WrongArchitecture;
    }

    return ElfVerdict::Ok;
}

}  // namespace elf
}  // namespace update
}  // namespace notrix
