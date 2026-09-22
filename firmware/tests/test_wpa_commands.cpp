// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/tc002/WpaCommands.h"

#include <string>

#include "support/TestFramework.h"

using notrix::platform::tc002::wpa::kJoinPriority;
using notrix::platform::tc002::wpa::parseNetworkId;
using notrix::platform::tc002::wpa::passphraseProblem;
using notrix::platform::tc002::wpa::setNetworkHex;
using notrix::platform::tc002::wpa::setPassphrase;
using notrix::platform::tc002::wpa::ssidProblem;
using notrix::platform::tc002::wpa::toHex;

NOTRIX_TEST(WpaCommands, HexEncodesAnSsid) {
    NOTRIX_CHECK_EQ(toHex("abc"), std::string("616263"));
    NOTRIX_CHECK_EQ(toHex(""), std::string());
    // High bytes, which an SSID is allowed to contain and which the quoted
    // form would mangle.
    NOTRIX_CHECK_EQ(toHex(std::string("\x00\xff", 2)), std::string("00ff"));
}

NOTRIX_TEST(WpaCommands, AnSsidCannotEscapeIntoACommand) {
    // The point of hex. This name is a deliberate attempt to end the command
    // and start another one, and it comes out as data either way.
    const std::string hostile = "evil\" \nLIST_NETWORKS\n";
    const std::string command = setNetworkHex(0, "ssid", hostile);

    NOTRIX_CHECK(command.find('"') == std::string::npos);
    NOTRIX_CHECK(command.find('\n') == std::string::npos);
    NOTRIX_CHECK(command.find("LIST_NETWORKS") == std::string::npos);
    NOTRIX_CHECK_EQ(command, "SET_NETWORK 0 ssid " + toHex(hostile));
}

NOTRIX_TEST(WpaCommands, BuildsTheCommandsWpaSupplicantExpects) {
    NOTRIX_CHECK_EQ(setNetworkHex(3, "ssid", "home"), std::string("SET_NETWORK 3 ssid 686f6d65"));
    NOTRIX_CHECK_EQ(setPassphrase(3, "hunter22"),
                    std::string("SET_NETWORK 3 psk \"hunter22\""));
}

NOTRIX_TEST(WpaCommands, RefusesAPassphraseItCannotSetHonestly) {
    // A passphrase cannot be hex-encoded - wpa_supplicant would read the hex
    // as the derived key rather than the words - so the quoted form is the
    // only one, and its parser stops at the first quote without honouring
    // escapes. Truncating there would store a different password than the one
    // typed and then report success.
    const std::string problem = passphraseProblem("has\"quote");
    NOTRIX_CHECK(!problem.empty());
    NOTRIX_CHECK(problem.find("double quote") != std::string::npos);
}

NOTRIX_TEST(WpaCommands, RefusesPassphrasesThatCannotWork) {
    NOTRIX_CHECK(!passphraseProblem("short").empty());   // under 8
    NOTRIX_CHECK(!passphraseProblem(std::string(64, 'a')).empty());  // over 63
    NOTRIX_CHECK(!passphraseProblem("line\nbreak").empty());
    NOTRIX_CHECK(!passphraseProblem(std::string("nul\0byte", 8)).empty());

    // And accepts the ones that can. Backslashes are fine: the quoted form
    // does not treat them as escapes, so they arrive as typed.
    NOTRIX_CHECK(passphraseProblem("hunter22").empty());
    NOTRIX_CHECK(passphraseProblem("back\\slash").empty());
    NOTRIX_CHECK(passphraseProblem(std::string(63, 'a')).empty());
    NOTRIX_CHECK(passphraseProblem("~!@#$%^&*()_+{}|:<>?").empty());
}

NOTRIX_TEST(WpaCommands, ChecksAnSsidIsOneAtAll) {
    NOTRIX_CHECK(!ssidProblem("").empty());
    NOTRIX_CHECK(!ssidProblem(std::string(33, 'a')).empty());
    NOTRIX_CHECK(ssidProblem("a").empty());
    NOTRIX_CHECK(ssidProblem(std::string(32, 'a')).empty());
    // Hostile names are allowed through - hex encoding is what makes them
    // safe, not refusing them.
    NOTRIX_CHECK(ssidProblem("evil\" \nSTUFF").empty());
}

NOTRIX_TEST(WpaCommands, ReadsAnAddNetworkReply) {
    NOTRIX_CHECK_EQ(parseNetworkId("0\n"), 0);
    NOTRIX_CHECK_EQ(parseNetworkId("12"), 12);
    NOTRIX_CHECK_EQ(parseNetworkId("3\r\n"), 3);
}

NOTRIX_TEST(WpaCommands, RefusesAnAddNetworkReplyThatIsNotAnId) {
    // FAIL is the common one, and treating it as network 0 would mean
    // reconfiguring whichever network happened to be first - most likely the
    // one currently working.
    NOTRIX_CHECK_EQ(parseNetworkId("FAIL\n"), -1);
    NOTRIX_CHECK_EQ(parseNetworkId(""), -1);
    NOTRIX_CHECK_EQ(parseNetworkId("UNKNOWN COMMAND"), -1);
    NOTRIX_CHECK_EQ(parseNetworkId("0 extra"), -1);
    NOTRIX_CHECK_EQ(parseNetworkId("99999999"), -1);
}

NOTRIX_TEST(WpaCommands, JoinsAtAHigherPriorityThanWhatIsStored) {
    // ADR 0018 turns on this: the device appends and never replaces, so the
    // network just chosen has to outrank the ones already there - and a wrong
    // password falls back to whatever was working instead of stranding a
    // device nobody can reach.
    NOTRIX_CHECK(kJoinPriority > 0);
}
