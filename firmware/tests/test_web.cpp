// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/web/StaticFiles.h"

#include <set>
#include <string>

#include "stipple/web/WebAssets.h"
#include "support/TestFramework.h"

using stipple::api::Method;
using stipple::api::Request;
using stipple::api::Response;
using stipple::web::Asset;
using stipple::web::assetCount;
using stipple::web::assets;
using stipple::web::findAsset;
using stipple::web::StaticFiles;

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
    STIPPLE_CHECK(files.tryHandle(request, response));
    return response;
}

}  // namespace

// --- the embedded table ------------------------------------------------------

STIPPLE_TEST(WebAssets, TheUiIsActuallyCompiledIn) {
    // Guards the build wiring rather than the code: if the CMake generator
    // stopped running, every page would 404 on a real device and nothing else
    // in the suite would notice.
    STIPPLE_CHECK(assetCount() >= 3);
    STIPPLE_CHECK(findAsset("/index.html") != nullptr);
    STIPPLE_CHECK(findAsset("/app.css") != nullptr);
    STIPPLE_CHECK(findAsset("/app.js") != nullptr);
}

STIPPLE_TEST(WebAssets, EveryAssetIsWellFormed) {
    for (int i = 0; i < assetCount(); ++i) {
        const Asset& asset = assets()[i];

        STIPPLE_CHECK(!asset.path.empty());
        STIPPLE_CHECK(asset.path.front() == '/');
        STIPPLE_CHECK(!asset.contentType.empty());
        STIPPLE_CHECK(!asset.body.empty());

        // A quoted content hash, as HTTP requires of a strong ETag.
        STIPPLE_CHECK(asset.etag.size() > 2);
        STIPPLE_CHECK(asset.etag.front() == '"');
        STIPPLE_CHECK(asset.etag.back() == '"');
    }
}

STIPPLE_TEST(WebAssets, PathsAreUnique) {
    // Two assets on one path would make lookup depend on table order, and the
    // loser would be unreachable with no error anywhere.
    std::set<std::string_view> seen;
    for (int i = 0; i < assetCount(); ++i) {
        STIPPLE_CHECK(seen.insert(assets()[i].path).second);
    }
}

STIPPLE_TEST(WebAssets, EtagsDifferBetweenDifferentFiles) {
    std::set<std::string_view> seen;
    for (int i = 0; i < assetCount(); ++i) {
        STIPPLE_CHECK(seen.insert(assets()[i].etag).second);
    }
}

STIPPLE_TEST(WebAssets, TheEmbeddedPageSurvivedGeneration) {
    // The generator writes raw string literals. A mangled escape or a delimiter
    // clash would corrupt the content while still compiling, so check for
    // landmarks from each end of the real files.
    const Asset* page = findAsset("/index.html");
    const std::string_view html = page->body;
    STIPPLE_CHECK(html.find("<!DOCTYPE html>") != std::string_view::npos);
    STIPPLE_CHECK(html.find("data-setting=\"clock.theme\"") != std::string_view::npos);
    STIPPLE_CHECK(html.find("</html>") != std::string_view::npos);

    const std::string_view script = findAsset("/app.js")->body;
    STIPPLE_CHECK(script.find("STIPPLE_BRIDGE") != std::string_view::npos);
    STIPPLE_CHECK(script.find("/api/v1/settings") != std::string_view::npos);
}

STIPPLE_TEST(WebAssets, TheUiOnlyTalksToTheVersionedApi) {
    // A page that reached for an unversioned path would 404 at runtime against
    // the very rule ADR 0015 established.
    const std::string script(findAsset("/app.js")->body);

    std::size_t at = 0;
    int checked = 0;
    while ((at = script.find("'/api", at)) != std::string::npos) {
        STIPPLE_CHECK(script.compare(at, 9, "'/api/v1/") == 0);
        ++checked;
        at += 5;
    }
    STIPPLE_CHECK(checked > 0);  // the scan found something to check
}

// --- serving -----------------------------------------------------------------

STIPPLE_TEST(StaticFiles, RootServesTheIndex) {
    const Response response = serve(get("/"));

    STIPPLE_CHECK_EQ(response.status, 200);
    STIPPLE_CHECK_EQ(response.contentType, std::string("text/html; charset=utf-8"));
    STIPPLE_CHECK(response.body.find("<!DOCTYPE html>") != std::string::npos);
}

STIPPLE_TEST(StaticFiles, ServesEachAssetWithItsOwnContentType) {
    STIPPLE_CHECK_EQ(serve(get("/app.css")).contentType,
                    std::string("text/css; charset=utf-8"));
    STIPPLE_CHECK_EQ(serve(get("/app.js")).contentType,
                    std::string("application/javascript; charset=utf-8"));
}

STIPPLE_TEST(StaticFiles, UnknownPathsAreNotOurs) {
    // Returning false rather than a 404 lets the caller produce one consistent
    // answer instead of two competing ones.
    StaticFiles files;
    Response response;
    STIPPLE_CHECK_FALSE(files.tryHandle(get("/nope.html"), response));
    STIPPLE_CHECK_FALSE(files.tryHandle(get("/api/v1/device"), response));
    STIPPLE_CHECK_FALSE(files.tryHandle(get("/app"), response));
}

STIPPLE_TEST(StaticFiles, AnEmptyPathIsTreatedAsRoot) {
    // Should not arise — a transport always supplies at least "/" — but an
    // empty path landing on the index beats it falling through to an API 404
    // that says nothing useful.
    STIPPLE_CHECK_EQ(serve(get("")).status, 200);
}

STIPPLE_TEST(StaticFiles, TraversalFindsNothingBecauseThereIsNoFilesystem) {
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
        STIPPLE_CHECK_FALSE(files.tryHandle(get(path), response));
    }
}

STIPPLE_TEST(StaticFiles, ConditionalRequestGetsA304) {
    const Response first = serve(get("/index.html"));
    STIPPLE_CHECK_EQ(first.status, 200);
    STIPPLE_CHECK(!first.etag.empty());

    const Response second = serve(get("/index.html", first.etag));
    STIPPLE_CHECK_EQ(second.status, 304);
    STIPPLE_CHECK(second.body.empty());
    STIPPLE_CHECK_EQ(second.etag, first.etag);
}

STIPPLE_TEST(StaticFiles, AStaleEtagStillGetsTheBody) {
    const Response response = serve(get("/index.html", "\"something-else\""));

    STIPPLE_CHECK_EQ(response.status, 200);
    STIPPLE_CHECK(!response.body.empty());
}

STIPPLE_TEST(StaticFiles, HeadAnswersWithoutABody) {
    Request request = get("/index.html");
    request.method = Method::Head;

    const Response response = serve(request);
    STIPPLE_CHECK_EQ(response.status, 200);
    STIPPLE_CHECK(response.body.empty());
    STIPPLE_CHECK(!response.etag.empty());
}

STIPPLE_TEST(StaticFiles, WritingToAPageIsRejectedNotIgnored) {
    const Method writes[] = {Method::Post, Method::Put, Method::Patch, Method::Delete};

    for (Method method : writes) {
        Request request = get("/index.html");
        request.method = method;

        const Response response = serve(request);
        STIPPLE_CHECK_EQ(response.status, 405);
    }
}

STIPPLE_TEST(StaticFiles, PagesRevalidateRatherThanCachingByAge) {
    // A page cached by age would survive a firmware update and show controls
    // that no longer match the API.
    const Response response = serve(get("/index.html"));
    STIPPLE_CHECK(response.cacheControl.find("no-cache") != std::string::npos);
}

STIPPLE_TEST(WebAssets, ThePageDeclaresAnIconSoBrowsersStopGuessing) {
    // A browser not told where the icon is asks for /favicon.ico, which this
    // device does not have - so every visit wrote a 404 into the device's own
    // log. Serving an icon is only half of it; the page has to say so, or the
    // browser never looks.
    const Asset* icon = findAsset("/favicon.svg");
    STIPPLE_REQUIRE(icon != nullptr);
    STIPPLE_CHECK_EQ(std::string(icon->contentType), std::string("image/svg+xml"));

    const Asset* page = findAsset("/index.html");
    STIPPLE_REQUIRE(page != nullptr);
    const std::string html(page->body);
    STIPPLE_CHECK(html.find("rel=\"icon\"") != std::string::npos);
    STIPPLE_CHECK(html.find("/favicon.svg") != std::string::npos);
}
