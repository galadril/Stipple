// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/api/BasicAuth.h"

#include <string>

#include "stipple/core/Base64.h"
#include "support/TestFramework.h"

using stipple::api::basicAuthorised;
using stipple::api::constantTimeEquals;
using stipple::api::decodeBasic;

namespace {

/// Build the header a client would send, so the tests exercise the real
/// shape rather than a hand-written approximation of it.
std::string header(const std::string& user, const std::string& password) {
    const std::string joined = user + ":" + password;
    return "Basic " + stipple::base64::encode(
                          reinterpret_cast<const std::uint8_t*>(joined.data()), joined.size());
}

}  // namespace

// --- base64, which now decodes untrusted input -------------------------------

STIPPLE_TEST(Base64, RoundTrips) {
    const char* cases[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar",
                           "user:password", "\x00\x01\xff"};
    for (const char* text : cases) {
        const std::string input(text);
        const std::string encoded = stipple::base64::encode(
            reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
        std::string decoded;
        STIPPLE_REQUIRE(stipple::base64::decode(encoded, decoded, 1024));
        STIPPLE_CHECK_EQ(decoded, input);
    }
}

STIPPLE_TEST(Base64, DecodesTheKnownVectors) {
    std::string out;
    STIPPLE_REQUIRE(stipple::base64::decode("Zm9vYmFy", out, 64));
    STIPPLE_CHECK_EQ(out, std::string("foobar"));
    STIPPLE_REQUIRE(stipple::base64::decode("Zg==", out, 64));
    STIPPLE_CHECK_EQ(out, std::string("f"));
    STIPPLE_REQUIRE(stipple::base64::decode("Zm8=", out, 64));
    STIPPLE_CHECK_EQ(out, std::string("fo"));
}

STIPPLE_TEST(Base64, IsStrictBecauseItParsesCredentials) {
    // A decoder that accepts near-misses is one that turns a malformed
    // header into a credential somebody did not send.
    std::string out;
    STIPPLE_CHECK_FALSE(stipple::base64::decode("Zm9vYmF", out, 64));     // not a multiple of 4
    STIPPLE_CHECK_FALSE(stipple::base64::decode("Zm9v YmFy", out, 64));   // embedded space
    STIPPLE_CHECK_FALSE(stipple::base64::decode("Zm9v\nYmFy", out, 64));  // embedded newline
    STIPPLE_CHECK_FALSE(stipple::base64::decode("Zm9vYmF*", out, 64));    // stray character
    STIPPLE_CHECK_FALSE(stipple::base64::decode("Z===", out, 64));        // padding in the wrong place
    STIPPLE_CHECK_FALSE(stipple::base64::decode("=m9v", out, 64));
}

STIPPLE_TEST(Base64, RefusesMoreThanItWasAskedToHold) {
    // Bounded, because this is fed by whoever is on the network (§38).
    std::string out;
    const std::string large(4000, 'A');
    STIPPLE_CHECK_FALSE(stipple::base64::decode(large, out, 256));
}

// --- the credential ----------------------------------------------------------

STIPPLE_TEST(BasicAuth, ReadsAHeaderAClientWouldSend) {
    std::string user;
    std::string password;
    STIPPLE_REQUIRE(decodeBasic(header("mark", "hunter22"), user, password));
    STIPPLE_CHECK_EQ(user, std::string("mark"));
    STIPPLE_CHECK_EQ(password, std::string("hunter22"));
}

STIPPLE_TEST(BasicAuth, SplitsOnTheFirstColonBecausePasswordsContainThem) {
    // Splitting on the last colon would quietly accept a different username
    // than the client sent.
    std::string user;
    std::string password;
    STIPPLE_REQUIRE(decodeBasic(header("mark", "a:b:c"), user, password));
    STIPPLE_CHECK_EQ(user, std::string("mark"));
    STIPPLE_CHECK_EQ(password, std::string("a:b:c"));
}

STIPPLE_TEST(BasicAuth, AcceptsAnEmptyPasswordAsAValueNotAsAbsence) {
    std::string user;
    std::string password;
    STIPPLE_REQUIRE(decodeBasic(header("mark", ""), user, password));
    STIPPLE_CHECK_EQ(user, std::string("mark"));
    STIPPLE_CHECK(password.empty());
}

STIPPLE_TEST(BasicAuth, RefusesAnythingThatIsNotBasic) {
    std::string user;
    std::string password;
    STIPPLE_CHECK_FALSE(decodeBasic("", user, password));
    STIPPLE_CHECK_FALSE(decodeBasic("Basic", user, password));
    STIPPLE_CHECK_FALSE(decodeBasic("Basic ", user, password));
    // The other scheme this device accepts elsewhere. A caller asking about
    // Basic must not be handed a half-parsed something else.
    STIPPLE_CHECK_FALSE(decodeBasic("Bearer abcdef", user, password));
    STIPPLE_CHECK_FALSE(decodeBasic("Basic !!!!", user, password));
    // Valid base64, but no colon, so it is not a credential.
    STIPPLE_CHECK_FALSE(decodeBasic("Basic Zm9vYmFy", user, password));
}

STIPPLE_TEST(BasicAuth, IsCaseInsensitiveAboutTheScheme) {
    // Clients vary, and the RFC says the scheme is case-insensitive.
    std::string user;
    std::string password;
    std::string h = header("mark", "hunter22");
    h[0] = 'b';
    STIPPLE_REQUIRE(decodeBasic(h, user, password));
    STIPPLE_CHECK_EQ(user, std::string("mark"));
}

// --- the decision ------------------------------------------------------------

STIPPLE_TEST(BasicAuth, LetsEverythingThroughWhenNoUserIsSet) {
    // Off by default. A device that demanded a password before showing a
    // clock would be a worse first five minutes than the risk it removes.
    STIPPLE_CHECK(basicAuthorised("", "", ""));
    STIPPLE_CHECK(basicAuthorised("Basic nonsense", "", ""));
}

STIPPLE_TEST(BasicAuth, AcceptsTheRightCredentials) {
    STIPPLE_CHECK(basicAuthorised(header("mark", "hunter22"), "mark", "hunter22"));
}

STIPPLE_TEST(BasicAuth, RefusesEverythingElse) {
    STIPPLE_CHECK_FALSE(basicAuthorised(header("mark", "wrong"), "mark", "hunter22"));
    STIPPLE_CHECK_FALSE(basicAuthorised(header("nobody", "hunter22"), "mark", "hunter22"));
    STIPPLE_CHECK_FALSE(basicAuthorised("", "mark", "hunter22"));
    STIPPLE_CHECK_FALSE(basicAuthorised("Bearer hunter22", "mark", "hunter22"));

    // A prefix of the password is not the password. Worth asserting because
    // a comparison that stopped at the shorter length would accept it.
    STIPPLE_CHECK_FALSE(basicAuthorised(header("mark", "hunter2"), "mark", "hunter22"));
    STIPPLE_CHECK_FALSE(basicAuthorised(header("mark", "hunter223"), "mark", "hunter22"));
}

STIPPLE_TEST(BasicAuth, EnforcesAUserWithAnEmptyPassword) {
    // Half-configured must not quietly mean open. Somebody who set a
    // username and left the password blank has still asked for a gate.
    STIPPLE_CHECK(basicAuthorised(header("mark", ""), "mark", ""));
    STIPPLE_CHECK_FALSE(basicAuthorised(header("mark", "anything"), "mark", ""));
    STIPPLE_CHECK_FALSE(basicAuthorised("", "mark", ""));
}

STIPPLE_TEST(BasicAuth, ComparesWithoutLeakingWhereStringsDiffer) {
    STIPPLE_CHECK(constantTimeEquals("", ""));
    STIPPLE_CHECK(constantTimeEquals("abc", "abc"));
    STIPPLE_CHECK_FALSE(constantTimeEquals("abc", "abd"));
    STIPPLE_CHECK_FALSE(constantTimeEquals("abc", "abcd"));
    STIPPLE_CHECK_FALSE(constantTimeEquals("", "a"));
}
