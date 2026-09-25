// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/web/StaticFiles.h"

#include "stipple/web/WebAssets.h"

namespace stipple {
namespace web {

using api::Method;
using api::Request;
using api::Response;

std::string_view StaticFiles::resolve(std::string_view path) noexcept {
    if (path.empty() || path == "/") {
        return kIndexPath;
    }
    return path;
}

bool StaticFiles::tryHandle(const Request& request, Response& out) const {
    const Asset* asset = findAsset(resolve(request.path));
    if (asset == nullptr) {
        return false;
    }

    // HEAD is answered like GET without a body, because a client checking
    // whether the UI exists should not have to download it to find out.
    if (request.method != Method::Get && request.method != Method::Head) {
        out = api::methodNotAllowed("static content is read-only");
        return true;
    }

    // The ETag is a content hash, so an unchanged build serves 304 and the
    // browser reuses what it has. Assets are only invalidated by a rebuild,
    // which is exactly when the hash changes.
    if (!request.ifNoneMatch.empty() && request.ifNoneMatch == asset->etag) {
        out = Response{};
        out.status = 304;
        out.contentType.clear();
        out.etag = std::string(asset->etag);
        return true;
    }

    out = Response{};
    out.status = 200;
    out.contentType = std::string(asset->contentType);
    out.etag = std::string(asset->etag);

    // Revalidate every time rather than caching by age. The UI is served by the
    // device it configures, so a stale page after a firmware update would show
    // controls that no longer match the API — and "must-revalidate" costs one
    // conditional request, which the ETag then answers with a 304.
    out.cacheControl = "no-cache, must-revalidate";

    if (request.method == Method::Get) {
        out.body.assign(asset->body.data(), asset->body.size());
    }
    return true;
}

}  // namespace web
}  // namespace stipple
