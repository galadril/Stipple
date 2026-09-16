// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "notrix/api/Http.h"

namespace notrix {

namespace app {
class AppRegistry;
class Carousel;
}  // namespace app

namespace notify {
class NotificationQueue;
}

namespace asset {
class IconStore;
}

namespace log {
class RingLog;
}

namespace config {
struct Config;
class ConfigStore;
}  // namespace config

namespace platform {
class IPlatformServices;
}

namespace api {

/// Everything the API is allowed to touch.
///
/// Passed in rather than reached for through a global, so a test can wire up
/// exactly the subset it needs and a null pointer means "this build does not
/// offer that", not a crash.
struct ApiContext {
    app::AppRegistry* apps = nullptr;
    app::Carousel* carousel = nullptr;
    notify::NotificationQueue* notifications = nullptr;
    asset::IconStore* icons = nullptr;
    config::Config* config = nullptr;
    config::ConfigStore* configStore = nullptr;
    platform::IPlatformServices* platform = nullptr;
    log::RingLog* logger = nullptr;
};

struct ApiOptions {
    /// When set, every request must present it. Empty disables authentication,
    /// which is the default for a LAN-only device (§23) but should be a
    /// deliberate choice, not an accident.
    std::string authToken;

    /// Requests larger than this are rejected before parsing. Bodies arrive
    /// from the network and must not be able to exhaust RAM (§38).
    std::size_t maxBodyBytes = 16u * 1024u;

    /// Token budget for parsing a request body.
    int maxJsonTokens = 512;
};

/// The native `/api/v1/*` surface (blueprint §19.1).
///
/// Pure: a request in, a response out. No sockets and no clock of its own —
/// `nowMillis` is supplied, so every endpoint is reproducible in a test.
class ApiServer {
public:
    ApiServer(const ApiContext& context, ApiOptions options = ApiOptions{})
        : context_(context), options_(std::move(options)) {}

    Response handle(const Request& request, std::uint64_t nowMillis);

    const ApiOptions& options() const noexcept { return options_; }

private:
    /// Checks the token when one is configured.
    ///
    /// Applies to every endpoint including health. An unauthenticated liveness
    /// probe is convenient, but it also leaks uptime, version and app names to
    /// anything on the LAN, and §23 asks for the security-conscious default.
    /// Monitoring must present the token.
    bool authorised(const Request& request) const;

    Response handleDevice(const Request& request);
    Response handleHealth(const Request& request, std::uint64_t nowMillis);
    Response handleVersion(const Request& request);
    Response handleDiagnostics(const Request& request, std::uint64_t nowMillis);
    Response handleLogs(const Request& request);

    Response handleAppCollection(const Request& request, std::uint64_t nowMillis);
    Response handleAppItem(const Request& request, const std::string& id,
                           std::uint64_t nowMillis);
    Response handleAppActivate(const Request& request, const std::string& id,
                               std::uint64_t nowMillis);

    Response handleNotificationCollection(const Request& request, std::uint64_t nowMillis);
    Response handleNotificationItem(const Request& request, const std::string& id,
                                    std::uint64_t nowMillis);

    Response handleAssetCollection(const Request& request);
    Response handleAssetItem(const Request& request, const std::string& id);

    Response handleSettings(const Request& request);
    Response handleReboot(const Request& request);

    ApiContext context_;
    ApiOptions options_;
};

}  // namespace api
}  // namespace notrix
