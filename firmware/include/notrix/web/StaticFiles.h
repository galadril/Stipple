// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "notrix/api/Http.h"

namespace notrix {
namespace web {

/// Serves the built-in configuration UI.
///
/// Deliberately separate from ApiServer. That class is the `/api/v1/*` contract
/// and nothing else; mixing file serving into it would blur a boundary that the
/// 404-for-unknown-API-version logic depends on being sharp.
///
/// Stateless and allocation-light: a hit copies the asset's bytes into the
/// response once, and a conditional request that matches copies nothing at all.
class StaticFiles {
public:
    /// Try to answer from the embedded assets.
    ///
    /// Returns false — leaving `out` untouched — when the path is not ours, so
    /// the caller can fall through to the API and get one consistent 404 rather
    /// than two competing ones.
    ///
    /// Returns true with a 405 for a path we do own but a method we do not: the
    /// distinction between "no such page" and "cannot POST to a page" is worth
    /// keeping.
    bool tryHandle(const api::Request& request, api::Response& out) const;

    /// Path a bare "/" resolves to.
    static constexpr std::string_view kIndexPath = "/index.html";

private:
    /// "/" becomes the index; anything else is used as-is. No traversal handling
    /// is needed or attempted, because lookup is an exact match against a fixed
    /// table rather than a filesystem walk — "/../../etc/passwd" simply is not
    /// in the table.
    static std::string_view resolve(std::string_view path) noexcept;
};

}  // namespace web
}  // namespace notrix
