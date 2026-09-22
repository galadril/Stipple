// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/tc002/WirelessStats.h"

#include <string>

#include "support/TestFramework.h"

using notrix::platform::tc002::wireless::parse;
using notrix::platform::tc002::wireless::Stats;

namespace {

/// Copied off the device, byte for byte, so this tests the format the kernel
/// actually writes rather than the one it was remembered as writing.
constexpr const char* kReal =
    "Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE\n"
    " face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22\n"
    " wlan0: 0000   66.  -44.  -256        0      0      0      0      0        0\n"
    "  p2p0: 0000    0     0     0        0      0      0      0      0        0\n";

}  // namespace

NOTRIX_TEST(WirelessStats, ReadsTheRealFileFromTheDevice) {
    const Stats stats = parse(kReal, "wlan0");
    NOTRIX_CHECK(stats.known);
    NOTRIX_CHECK_EQ(stats.linkQuality, 66);
    NOTRIX_CHECK_EQ(stats.levelDbm, -44);
}

NOTRIX_TEST(WirelessStats, TheTrailingStopIsAMarkerNotADecimalPoint) {
    // The kernel writes "-44." to mean "this value was updated". Treating it
    // as a decimal point, or stopping at it and keeping what follows, both
    // produce numbers that look plausible and are wrong - and -44 against 44
    // is the difference between a good signal and an impossible one.
    const Stats stats = parse(kReal, "wlan0");
    NOTRIX_CHECK(stats.levelDbm < 0);
    NOTRIX_CHECK_EQ(stats.levelDbm, -44);
}

NOTRIX_TEST(WirelessStats, PicksTheRightInterface) {
    const Stats other = parse(kReal, "p2p0");
    NOTRIX_CHECK(other.known);
    NOTRIX_CHECK_EQ(other.levelDbm, 0);
    NOTRIX_CHECK_EQ(other.linkQuality, 0);
}

NOTRIX_TEST(WirelessStats, AMissingInterfaceIsUnknownNotZero) {
    // The radio being down is not the same as a signal of zero, and a UI
    // showing "0 dBm" for a missing radio is the same confident lie as a
    // battery reading 0% because nothing answered.
    const Stats stats = parse(kReal, "wlan9");
    NOTRIX_CHECK_FALSE(stats.known);
    NOTRIX_CHECK_EQ(stats.levelDbm, 0);
}

NOTRIX_TEST(WirelessStats, HeaderLinesAreNeverMistakenForData) {
    // Matched on the shape of an interface row rather than by counting lines,
    // so a kernel that adds a header line does not silently shift which row
    // gets read.
    NOTRIX_CHECK_FALSE(parse(kReal, "face").known);
    NOTRIX_CHECK_FALSE(parse(kReal, "Inter-").known);
}

NOTRIX_TEST(WirelessStats, RubbishIsRefusedRatherThanHalfRead) {
    NOTRIX_CHECK_FALSE(parse("", "wlan0").known);
    NOTRIX_CHECK_FALSE(parse("wlan0:\n", "wlan0").known);
    NOTRIX_CHECK_FALSE(parse(" wlan0: 0000\n", "wlan0").known);
    NOTRIX_CHECK_FALSE(parse(" wlan0: 0000  66.\n", "wlan0").known);
    NOTRIX_CHECK_FALSE(parse(" wlan0: nonsense here\n", "wlan0").known);
    NOTRIX_CHECK_FALSE(parse(kReal, "").known);
}

NOTRIX_TEST(WirelessStats, AFileWithNoTrailingNewlineStillParses) {
    // Nothing guarantees one, and losing the last row because of it would be
    // a bug that only appears on some kernels.
    const Stats stats = parse(
        "Inter-| sta-|   Quality\n"
        " face | tus | link level noise\n"
        " wlan0: 0000   70.  -30.  -256", "wlan0");
    NOTRIX_CHECK(stats.known);
    NOTRIX_CHECK_EQ(stats.levelDbm, -30);
}

NOTRIX_TEST(WirelessStats, ValuesWithoutTheMarkerAreStillRead) {
    // The stop only appears on values the driver updated this cycle, so a row
    // can arrive without any.
    const Stats stats = parse(
        "Inter-| sta-|   Quality\n"
        " face | tus | link level noise\n"
        " wlan0: 0000   58  -62  -256\n", "wlan0");
    NOTRIX_CHECK(stats.known);
    NOTRIX_CHECK_EQ(stats.linkQuality, 58);
    NOTRIX_CHECK_EQ(stats.levelDbm, -62);
}
