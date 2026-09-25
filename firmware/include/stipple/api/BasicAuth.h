// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace stipple {
namespace api {

/// HTTP Basic, checked in the core so every transport gets the same answer.
///
/// One mechanism for the API and the browser, because they are the same
/// server and two schemes would mean two things to get wrong (ADR 0018). A
/// browser prompts for it natively, and `curl -u` and every automation
/// library already speak it.
///
/// Pure, and therefore testable: this is the code that decides whether a
/// stranger on the LAN can rewrite the panel, and "it looked right" is not a
/// standard to hold it to.
///
/// **Basic sends the password in the clear**, base64 being an encoding and
/// not a cipher. Over a LAN, to a device with no certificate and no clock to
/// validate one against, that is the honest trade: the alternative is not
/// TLS, it is a self-signed certificate that teaches people to click through
/// warnings. What this defends against is the rest of the network having
/// casual write access to a clock, not somebody with a packet capture.

/// Compare without leaking where two strings first differ.
///
/// Timing on a device that renders at 30 FPS over Wi-Fi is a poor oracle, so
/// this is not the attack anybody would actually mount. It is four lines,
/// and writing the version that returns early would be choosing to be
/// measurably wrong for no gain.
inline bool constantTimeEquals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        difference = static_cast<unsigned char>(
            difference | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i])));
    }
    return difference == 0;
}

/// Decode an `Authorization` header value into user and password.
///
/// Returns false for anything that is not a well-formed Basic credential,
/// including the Bearer tokens this device also accepts elsewhere - a caller
/// asking about Basic should not be handed a half-parsed something else.
bool decodeBasic(std::string_view header, std::string& user, std::string& password);

/// Whether `header` presents the expected credentials.
///
/// An empty expected user means authentication is off and everything passes.
/// A configured user with an empty password is still enforced: half-configured
/// is refused at the API rather than silently meaning "open".
bool basicAuthorised(std::string_view header, std::string_view expectedUser,
                     std::string_view expectedPassword);

}  // namespace api
}  // namespace stipple
