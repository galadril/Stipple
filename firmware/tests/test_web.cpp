// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/web/StaticFiles.h"

#include <set>
#include <string>

#include "notrix/web/WebAssets.h"
#include "support/TestFramework.h"

using notrix::api::Method;
using notrix::api::Request;
using notrix::api::Response;
using notrix::web::Asset;
using notrix::web::assetCount;
using notrix::web::assets;
using notrix::web::findAsset;
using notrix::web::StaticFiles;

namespace {

Request get(const std::string& path, const std::string& ifNoneMatch = {}) {
    Request request;
    request.method = Method::Get;
    request.path = path;
    request.ifNoneMatch = ifNoneMatch;
    return request;
}

/// Serve a path, asserting that it was ours to serve.
Response serve(const Request& request) {
    StaticFiles files;
    Response response;
    NOTRIX_CHECK(files.tryHandle(request, response));
    return response;
}

}  // namespace

// --- the embedded table ------------------------------------------------------

NOTRIX_TEST(WebAssets, TheUiIsActuallyCompiledIn) {
    // Guards the build wiring rather than the code: if the CMake generator
    // stopped running, every page would 404 on a real device and nothing else
    // in the suite would notice.
    NOTRIX_CHECK(assetCount() >= 3);
    NOTRIX_CHECK(findAsset("/index.html") != nullptr);
    NOTRIX_CHECK(findAsset("/app.css") != nullptr);
    NOTRIX_CHECK(findAsset("/app.js") != nullptr);
}

NOTRIX_TEST(WebAssets, EveryAssetIsWellFormed) {
    for (int i = 0; i < assetCount(); ++i) {
        const Asset& asset = assets()[i];

        NOTRIX_CHECK(!asset.path.empty());
        NOTRIX_CHECK(asset.path.front() == '/');
        NOTRIX_CHECK(!asset.contentType.empty());
        NOTRIX_CHECK(!asset.body.empty());

        // A quoted content hash, as HTTP requires of a strong ETag.
        NOTRIX_CHECK(asset.etag.size() > 2);
        NOTRIX_CHECK(asset.etag.front() == '"');
        NOTRIX_CHECK(asset.etag.back() == '"');
    }
}

NOTRIX_TEST(WebAssets, PathsAreUnique) {
    // Two assets on one path would make lookup depend on table order, and the
    // loser would be unreachable with no error anywhere.
    std::set<std::string_view> seen;
    for (int i = 0; i < assetCount(); ++i) {
        NOTRIX_CHECK(seen.insert(assets()[i].path).second);
    }
}

NOTRIX_TEST(WebAssets, EtagsDifferBetweenDifferentFiles) {
    std::set<std::string_view> seen;
    for (int i = 0; i < assetCount(); ++i) {
        NOTRIX_CHECK(seen.insert(assets()[i].etag).second);
    }
}

NOTRIX_TEST(WebAssets, TheEmbeddedPageSurvivedGeneration) {
    // The generator writes raw string literals. A mangled escape or a delimiter
    // clash would corrupt the content while still compiling, so check for
    // landmarks from each end of the real files.
    const Asset* page = findAsset("/index.html");
    const std::string_view html = page->body;
    NOTRIX_CHECK(html.find("<!DOCTYPE html>") != std::string_view::npos);
    NOTRIX_CHECK(html.find("data-setting=\"clock.theme\"") != std::string_view::npos);
    NOTRIX_CHECK(html.find("</html>") != std::string_view::npos);

    const std::string_view script = findAsset("/app.js")->body;
    NOTRIX_CHECK(script.find("NOTRIX_BRIDGE") != std::string_view::npos);
    NOTRIX_CHECK(script.find("/api/v1/settings") != std::string_view::npos);
}

NOTRIX_TEST(WebAssets, TheUiOnlyTalksToTheVersionedApi) {
    // A page that reached for an unversioned path would 404 at runtime against
    // the very rule ADR 0015 established.
    const std::string script(findAsset("/app.js")->body);

    std::size_t at = 0;
    int checked = 0;
    while ((at = script.find("'/api", at)) != std::string::npos) {
        NOTRIX_CHECK(script.compare(at, 9, "'/api/v1/") == 0);
        ++checked;
        at += 5;
    }
    NOTRIX_CHECK(checked > 0);  // the scan found something to check
}

// --- serving -----------------------------------------------------------------

NOTRIX_TEST(StaticFiles, RootServesTheIndex) {
    const Response response = serve(get("/"));

    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK_EQ(response.contentType, std::string("text/html; charset=utf-8"));
    NOTRIX_CHECK(response.body.find("<!DOCTYPE html>") != std::string::npos);
}

NOTRIX_TEST(StaticFiles, ServesEachAssetWithItsOwnContentType) {
    NOTRIX_CHECK_EQ(serve(get("/app.css")).contentType,
                    std::string("text/css; charset=utf-8"));
    NOTRIX_CHECK_EQ(serve(get("/app.js")).contentType,
                    std::string("application/javascript; charset=utf-8"));
}

NOTRIX_TEST(StaticFiles, UnknownPathsAreNotOurs) {
    // Returning false rather than a 404 lets the caller produce one consistent
    // answer instead of two competing ones.
    StaticFiles files;
    Response response;
    NOTRIX_CHECK_FALSE(files.tryHandle(get("/nope.html"), response));
    NOTRIX_CHECK_FALSE(files.tryHandle(get("/api/v1/device"), response));
    NOTRIX_CHECK_FALSE(files.tryHandle(get("/app"), response));
}

NOTRIX_TEST(StaticFiles, AnEmptyPathIsTreatedAsRoot) {
    // Should not arise — a transport always supplies at least "/" — but an
    // empty path landing on the index beats it falling through to an API 404
    // that says nothing useful.
    NOTRIX_CHECK_EQ(serve(get("")).status, 200);
}

NOTRIX_TEST(StaticFiles, TraversalFindsNothingBecauseThereIsNoFilesystem) {
    // Not a defence that has to be maintained: lookup is an exact match against
    // a fixed table, so these paths simply are not in it.
    const char* attempts[] = {
        "/../etc/passwd",      "/./index.html",        "//index.html",
        "/index.html/",        "/index.html%00.txt",   "/..%2f..%2findex.html",
        "/INDEX.HTML",
    };

    StaticFiles files;
    for (const char* path : attempts) {
        Response response;
        NOTRIX_CHECK_FALSE(files.tryHandle(get(path), response));
    }
}

NOTRIX_TEST(StaticFiles, ConditionalRequestGetsA304) {
    const Response first = serve(get("/index.html"));
    NOTRIX_CHECK_EQ(first.status, 200);
    NOTRIX_CHECK(!first.etag.empty());

    const Response second = serve(get("/index.html", first.etag));
    NOTRIX_CHECK_EQ(second.status, 304);
    NOTRIX_CHECK(second.body.empty());
    NOTRIX_CHECK_EQ(second.etag, first.etag);
}

NOTRIX_TEST(StaticFiles, AStaleEtagStillGetsTheBody) {
    const Response response = serve(get("/index.html", "\"something-else\""));

    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK(!response.body.empty());
}

NOTRIX_TEST(StaticFiles, HeadAnswersWithoutABody) {
    Request request = get("/index.html");
    request.method = Method::Head;

    const Response response = serve(request);
    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK(response.body.empty());
    NOTRIX_CHECK(!response.etag.empty());
}

NOTRIX_TEST(StaticFiles, WritingToAPageIsRejectedNotIgnored) {
    const Method writes[] = {Method::Post, Method::Put, Method::Patch, Method::Delete};

    for (Method method : writes) {
        Request request = get("/index.html");
        request.method = method;

        const Response response = serve(request);
        NOTRIX_CHECK_EQ(response.status, 405);
    }
}

NOTRIX_TEST(StaticFiles, PagesRevalidateRatherThanCachingByAge) {
    // A page cached by age would survive a firmware update and show controls
    // that no longer match the API.
    const Response response = serve(get("/index.html"));
    NOTRIX_CHECK(response.cacheControl.find("no-cache") != std::string::npos);
}
