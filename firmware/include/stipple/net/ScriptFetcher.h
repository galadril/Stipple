// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stipple/platform/HttpClient.h"
#include "stipple/script/IScriptHttp.h"

namespace stipple {
namespace net {

/// Fetches what scripts asked for, one request at a time, and remembers the
/// answers.
///
/// Portable core. It owns the schedule and the cache; the platform owns the
/// socket. That split is what lets the whole policy — round-robin, intervals,
/// backoff, what counts as stale — be tested without a network.
///
/// The scheduling is deliberately dull. Every feed has a due time; each tick
/// picks the feed that has been due longest and is not already running, and
/// starts it if nothing else is. Round-robin by due time rather than by list
/// order, because list order would let the first script starve the others
/// simply by asking for a short interval.
class ScriptFetcher final : public script::IScriptHttp {
public:
    /// Feeds across all scripts.
    static constexpr int kMaxFeedsTotal = 16 * script::IScriptHttp::kMaxFeedsPerScript;

    /// How long a fetch may take before it is abandoned.
    ///
    /// The adapter has its own timeouts; this is the backstop for an adapter
    /// that loses track. Without it one wedged request would stop every other
    /// feed on the device for ever, and the symptom would be "the weather app
    /// stopped working" on a device where nothing looked wrong.
    static constexpr std::uint32_t kFetchTimeoutMillis = 20000;

    /// Wait after a failure, before the same feed is tried again.
    ///
    /// Longer than the interval on purpose: an endpoint that is refusing
    /// connections is not one to ask every thirty seconds, and a device doing
    /// that to somebody's server is a device that gets blocked.
    static constexpr std::uint32_t kFailureBackoffMillis = 120000;

    void setClient(platform::IHttpClient* client) noexcept { client_ = client; }

    /// Whether the network is up. Pushed in rather than read, so the fetcher
    /// does not need to know what a network manager is.
    void setNetworkUp(bool up) noexcept { networkUp_ = up; }

    /// Drive the schedule. Called once per frame from the host.
    void tick(std::uint64_t nowMillis);

    /// Feeds held, for diagnostics.
    int feedCount() const noexcept { return static_cast<int>(feeds_.size()); }

    /// Requests started since boot, for diagnostics. The first number anybody
    /// wants when asked "is this device hammering my API".
    std::uint32_t started() const noexcept { return started_; }

    // script::IScriptHttp
    bool available() const noexcept override;
    bool follow(std::string_view scriptId, std::string_view url,
                std::uint32_t intervalMillis) override;
    const std::string* body(std::string_view scriptId,
                            std::string_view url) const override;
    int status(std::string_view scriptId, std::string_view url) const override;
    std::int64_t ageMillis(std::string_view scriptId,
                           std::string_view url) const override;
    std::string_view failure(std::string_view scriptId,
                             std::string_view url) const override;
    void forget(std::string_view scriptId) override;

private:
    struct Feed {
        std::string scriptId;
        std::string url;
        std::uint32_t intervalMillis = script::IScriptHttp::kDefaultIntervalMillis;

        /// When it should next be fetched.
        std::uint64_t dueMillis = 0;

        std::string body;
        int status = 0;
        std::string failure;
        std::uint64_t arrivedMillis = 0;
        bool seen = false;
    };

    const Feed* find(std::string_view scriptId, std::string_view url) const noexcept;
    Feed* find(std::string_view scriptId, std::string_view url) noexcept;
    void collect(std::uint64_t nowMillis);
    void start(std::uint64_t nowMillis);

    platform::IHttpClient* client_ = nullptr;
    bool networkUp_ = false;
    std::vector<Feed> feeds_;

    /// Which feed is in flight, and since when. An index rather than a
    /// pointer: follow() can grow the vector between ticks.
    int running_ = -1;
    std::uint64_t startedMillis_ = 0;
    std::uint32_t started_ = 0;

    /// The clock, as of the last tick. Held so ageMillis() can answer without
    /// a clock of its own - two clocks is how an age comes out negative.
    std::uint64_t nowMillis_ = 0;
};

}  // namespace net
}  // namespace stipple
