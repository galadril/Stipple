// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace notrix {
namespace api {

/// HTTP as a pure data transformation.
///
/// A request goes in, a response comes out. No sockets, no threads, no
/// platform. That keeps every endpoint testable without a network stack, lets
/// the device and the simulator share one implementation, and means the
/// transport can be swapped without touching a single handler — which matters
/// because the TC002's transport is still unknown (§46).

enum class Method : std::uint8_t {
    Unknown,
    Get,
    Post,
    Put,
    Patch,
    Delete,
    Options,
    Head,
};

Method methodFromName(std::string_view name) noexcept;
const char* methodName(Method method) noexcept;

struct Request {
    Method method = Method::Unknown;

    /// Path only, without the query string. Expected to be already
    /// percent-decoded by the transport.
    std::string path;

    /// Raw query string, without the leading '?'.
    std::string query;

    std::string body;

    /// Bearer token or X-API-Key value, extracted by the transport. Kept
    /// separate from the body so no handler has to know how it arrived.
    std::string authToken;

    /// If-None-Match, for conditional requests. Named rather than reached for
    /// through a header map: the set of headers this device understands is small
    /// and fixed, and spelling it out keeps it auditable — the same reasoning
    /// that keeps matchRoute() a list of paths instead of a pattern engine.
    std::string ifNoneMatch;

    /// Value of a query parameter, or `fallback` when absent.
    std::string queryValue(std::string_view name, std::string_view fallback = {}) const;
    bool hasQuery(std::string_view name) const;
};

struct Response {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;

    /// Emitted as ETag and Cache-Control when non-empty. Only the static file
    /// handler sets these; API responses describe live state and are deliberately
    /// not cacheable.
    std::string etag;
    std::string cacheControl;
};

// --- response helpers --------------------------------------------------------

Response ok(std::string body);
Response created(std::string body);
Response noContent();

/// Errors share one shape: {"error":{"code":"...","message":"..."}}.
///
/// A machine-readable code with a human-readable message, so an integration can
/// branch on the code while a person debugging sees something useful. Blueprint
/// §40 forbids showing raw errors on the panel; this is the API's side of that.
Response error(int status, std::string_view code, std::string_view message);

Response badRequest(std::string_view message);
Response unauthorized(std::string_view message = "missing or invalid API token");
Response notFound(std::string_view message = "not found");
Response methodNotAllowed(std::string_view message = "method not allowed for this resource");
Response conflict(std::string_view message);
Response payloadTooLarge(std::string_view message = "request body too large");
Response unprocessable(std::string_view message);
Response serverError(std::string_view message);

// --- routing -----------------------------------------------------------------

/// Every addressable thing in the native API (blueprint §19.1).
enum class Resource : std::uint8_t {
    Unknown,
    Device,
    Health,
    Version,
    Diagnostics,
    Logs,
    AppCollection,
    AppItem,
    AppActivate,
    NotificationCollection,
    NotificationItem,
    AssetCollection,
    AssetItem,
    Settings,
    SystemReboot,
};

struct RouteMatch {
    Resource resource = Resource::Unknown;
    /// The `{id}` segment, for item routes.
    std::string id;
};

/// Match a path to a resource.
///
/// Deliberately not a generic pattern engine: the route set is small, fixed and
/// public API surface. Spelling it out keeps every reachable path visible in one
/// place, which matters when the thing being routed is untrusted network input.
RouteMatch matchRoute(std::string_view path);

/// Longest API version prefix this build serves.
inline constexpr std::string_view kApiV1Prefix = "/api/v1";

}  // namespace api
}  // namespace notrix
