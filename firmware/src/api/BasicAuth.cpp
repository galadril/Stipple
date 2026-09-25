// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/api/BasicAuth.h"

#include "stipple/core/Base64.h"

namespace stipple {
namespace api {
namespace {

/// A credential longer than this is not one. A username and a password with
/// a colon between them, bounded generously (§38).
constexpr std::size_t kMaxCredentialBytes = 256;

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char left = a[i];
        char right = b[i];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool decodeBasic(std::string_view header, std::string& user, std::string& password) {
    user.clear();
    password.clear();

    constexpr std::string_view kScheme = "Basic ";
    if (header.size() <= kScheme.size() ||
        !equalsIgnoreCase(header.substr(0, kScheme.size()), kScheme)) {
        return false;
    }

    // One space, and the rest is the credential. Leading spaces are trimmed
    // because some clients send more than one; nothing else is, because a
    // credential with whitespace in it is a malformed header rather than
    // something to tidy up.
    std::string_view encoded = header.substr(kScheme.size());
    while (!encoded.empty() && encoded.front() == ' ') {
        encoded.remove_prefix(1);
    }
    while (!encoded.empty() &&
           (encoded.back() == ' ' || encoded.back() == '\r' || encoded.back() == '\n')) {
        encoded.remove_suffix(1);
    }
    if (encoded.empty()) {
        return false;
    }

    std::string decoded;
    if (!base64::decode(encoded, decoded, kMaxCredentialBytes)) {
        return false;
    }

    // The *first* colon separates them. A password containing colons is
    // legal and common, and splitting on the last one would quietly accept a
    // different username than the client sent.
    const std::size_t colon = decoded.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    user = decoded.substr(0, colon);
    password = decoded.substr(colon + 1);
    return true;
}

bool basicAuthorised(std::string_view header, std::string_view expectedUser,
                     std::string_view expectedPassword) {
    if (expectedUser.empty()) {
        return true;  // authentication is off
    }

    std::string user;
    std::string password;
    if (!decodeBasic(header, user, password)) {
        return false;
    }

    // Both compared, and both in constant time, and deliberately without a
    // short circuit between them: returning as soon as the username is wrong
    // would answer "is this a real account" faster than "is this the right
    // password", which is the one distinction worth not handing out.
    const bool userMatches = constantTimeEquals(user, expectedUser);
    const bool passwordMatches = constantTimeEquals(password, expectedPassword);
    return userMatches && passwordMatches;
}

}  // namespace api
}  // namespace stipple
