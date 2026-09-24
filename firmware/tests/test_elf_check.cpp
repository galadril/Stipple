// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/update/ElfCheck.h"

#include <string>

#include "support/TestFramework.h"

using notrix::update::elf::ElfVerdict;
using notrix::update::elf::inspect;

namespace {

/// A 20-byte ELF header with the fields this check reads.
std::string header(std::uint8_t cls, std::uint8_t data, std::uint16_t type,
                   std::uint16_t machine) {
    std::string bytes(20, '\0');
    bytes[0] = static_cast<char>(0x7F);
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = static_cast<char>(cls);
    bytes[5] = static_cast<char>(data);
    bytes[16] = static_cast<char>(type & 0xFF);
    bytes[17] = static_cast<char>((type >> 8) & 0xFF);
    bytes[18] = static_cast<char>(machine & 0xFF);
    bytes[19] = static_cast<char>((machine >> 8) & 0xFF);
    return bytes;
}

/// 32-bit, little-endian, ET_DYN, EM_ARM - what the device wants.
std::string armSharedObject() { return header(1, 1, 3, 40); }

}  // namespace

NOTRIX_TEST(ElfCheck, AcceptsAnArmSharedLibrary) {
    NOTRIX_CHECK(inspect(armSharedObject()) == ElfVerdict::Ok);
}

NOTRIX_TEST(ElfCheck, RefusesSomethingThatIsNotAnElf) {
    NOTRIX_CHECK(inspect(std::string(64, 'x')) == ElfVerdict::NotAnElf);
    // A PNG, which is a plausible thing to pick by accident in a file dialog.
    std::string png(64, '\0');
    png[0] = static_cast<char>(0x89);
    png[1] = 'P';
    png[2] = 'N';
    png[3] = 'G';
    NOTRIX_CHECK(inspect(png) == ElfVerdict::NotAnElf);
}

NOTRIX_TEST(ElfCheck, RefusesAHostBuild) {
    // EI_CLASS 2 is 64-bit: an x86-64 build of the same source, which is the
    // easiest wrong file to upload because it has the right name.
    NOTRIX_CHECK(inspect(header(2, 1, 3, 62)) == ElfVerdict::WrongClass);
}

NOTRIX_TEST(ElfCheck, RefusesABigEndianBinary) {
    NOTRIX_CHECK(inspect(header(1, 2, 3, 40)) == ElfVerdict::WrongClass);
}

NOTRIX_TEST(ElfCheck, RefusesAnExecutableRatherThanALibrary) {
    // ET_EXEC. notrix_device is exactly this, sits next to libnotrix.so in
    // the build directory, and would otherwise be accepted.
    NOTRIX_CHECK(inspect(header(1, 1, 2, 40)) == ElfVerdict::NotASharedObject);
}

NOTRIX_TEST(ElfCheck, RefusesTheWrongArchitecture) {
    // A 32-bit little-endian ET_DYN for x86 - right shape, wrong machine.
    NOTRIX_CHECK(inspect(header(1, 1, 3, 3)) == ElfVerdict::WrongArchitecture);
}

NOTRIX_TEST(ElfCheck, RefusesSomethingTooShortToJudge) {
    NOTRIX_CHECK(inspect("") == ElfVerdict::TooShort);
    NOTRIX_CHECK(inspect(armSharedObject().substr(0, 19)) == ElfVerdict::TooShort);
    // Exactly enough is enough.
    NOTRIX_CHECK(inspect(armSharedObject()) == ElfVerdict::Ok);
}

NOTRIX_TEST(ElfCheck, EveryVerdictHasWordsForIt) {
    // The message reaches a web page, so a verdict with no sentence would
    // show somebody "unrecognised" and tell them nothing.
    for (const ElfVerdict verdict : {ElfVerdict::Ok, ElfVerdict::TooShort,
                                     ElfVerdict::NotAnElf, ElfVerdict::WrongClass,
                                     ElfVerdict::NotASharedObject,
                                     ElfVerdict::WrongArchitecture}) {
        const std::string words = notrix::update::elf::describe(verdict);
        NOTRIX_CHECK(!words.empty());
        NOTRIX_CHECK(words != "unrecognised");
    }
}
