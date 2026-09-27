// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace stipple {
namespace script {

/// The network, as a script may use it.
///
/// The same shape as the broker, for the same reason: a script is awake for a
/// few milliseconds every few seconds and cannot wait for anything. It says
/// what it wants fetched and how often; something else fetches it; the script
/// reads whatever arrived last. There is deliberately no call that performs a
/// request and returns its answer, because such a call would have to block the
/// thread that draws the panel (blueprint §16).
///
/// That is not a workaround. A script polling a weather API every five minutes
/// and drawing whatever it last got is what these scripts actually do, and it
/// is more robust than the synchronous version: an API that has gone down
/// leaves the last reading on screen with a visible age rather than a panel
/// that hangs for thirty seconds.
class IScriptHttp {
public:
    /// URLs one script may have on its list.
    ///
    /// Two. A panel 52 pixels wide is not a dashboard, and every extra URL is
    /// a request the device makes for ever without anybody watching it.
    static constexpr int kMaxFeedsPerScript = 2;

    /// The floor on how often a URL is re-fetched, whatever a script asks.
    ///
    /// Thirty seconds. Somebody's free weather API does not want a request a
    /// frame from every one of these devices, and the script author is not the
    /// person who would find out.
    static constexpr std::uint32_t kMinIntervalMillis = 30000;

    /// The default, when a script does not say.
    static constexpr std::uint32_t kDefaultIntervalMillis = 300000;  // 5 min

    /// And the ceiling, so an interval typo cannot park a feed for a year.
    static constexpr std::uint32_t kMaxIntervalMillis = 6u * 3600u * 1000u;

    virtual ~IScriptHttp() = default;

    /// Whether this device can fetch anything at all.
    ///
    /// False on a build with no network adapter, and false when the network is
    /// down. Either way the script has no data and owes the person looking at
    /// the panel an explanation rather than a stale number drawn as if live.
    virtual bool available() const noexcept = 0;

    /// Put a URL on this script's list, or update how often it is fetched.
    ///
    /// Idempotent and meant to be called from `draw()` every frame, because
    /// `draw()` is the only place a script can call anything from. False when
    /// the list is full or the URL is one this device will not fetch.
    ///
    /// The first fetch is started promptly; later ones follow the interval.
    virtual bool follow(std::string_view scriptId, std::string_view url,
                        std::uint32_t intervalMillis) = 0;

    /// The body of the last successful fetch, or null if there has not been
    /// one.
    ///
    /// Null rather than an empty string, for the third time in this codebase
    /// and the same reason: an endpoint that returns nothing and an endpoint
    /// that has never answered are different states.
    virtual const std::string* body(std::string_view scriptId,
                                    std::string_view url) const = 0;

    /// The HTTP status of the last attempt: 200, 404, 500. Zero when nothing
    /// has come back yet.
    ///
    /// Offered separately from the body because a 500 with an error page in it
    /// is not data, and a script that drew the body without looking would put
    /// somebody's stack trace on the panel.
    virtual int status(std::string_view scriptId, std::string_view url) const = 0;

    /// Milliseconds since that body arrived, or negative when none has.
    virtual std::int64_t ageMillis(std::string_view scriptId,
                                   std::string_view url) const = 0;

    /// Why the last attempt failed, or empty when it did not.
    virtual std::string_view failure(std::string_view scriptId,
                                     std::string_view url) const = 0;

    /// Drop everything held for a script, and stop fetching for it.
    ///
    /// A deleted script whose URL was still being fetched every five minutes
    /// would be a device making requests on behalf of code that no longer
    /// exists, and nothing on the device would ever mention it.
    virtual void forget(std::string_view scriptId) = 0;
};

}  // namespace script
}  // namespace stipple
