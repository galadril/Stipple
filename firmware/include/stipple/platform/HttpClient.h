// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace stipple {
namespace platform {

/// One outbound HTTP request at a time.
///
/// **One, not a pool, and that is the design rather than a shortcut.** This
/// device has 14 MB free and sixteen scripts. A pool would mean sixteen
/// sockets, sixteen response buffers and sixteen timeouts to get right, in
/// exchange for making a panel that redraws every 33 ms fetch two things at
/// once instead of one. The core schedules round-robin over this, which is
/// both simpler and bounded by construction.
///
/// Nothing here blocks. `begin` starts a fetch and returns; `poll` is called
/// from the application loop and is where all the work happens or is
/// collected. Blueprint §16 forbids anything on the render thread that can
/// wait on a network.
class IHttpClient {
public:
    enum class Stage : std::uint8_t {
        /// Nothing in flight.
        Idle,
        /// A fetch is running. Keep polling.
        Running,
        /// Finished. `status()` and `body()` hold the answer.
        Done,
        /// Finished badly. `failure()` says why, in words meant for a person.
        Failed,
    };

    virtual ~IHttpClient() = default;

    /// Start a GET.
    ///
    /// False when one is already running, when the URL is unusable, or when
    /// this platform cannot fetch that URL at all — an https URL on a build
    /// with no TLS, for instance. A false leaves `failure()` describing it,
    /// so the reason reaches the panel rather than the log alone.
    virtual bool begin(std::string_view url) = 0;

    /// Drive it. Called once per frame; must return promptly every time.
    virtual void poll(std::uint64_t nowMillis) = 0;

    virtual Stage stage() const noexcept = 0;

    /// The HTTP status, once `stage()` is Done. Zero otherwise.
    virtual int status() const noexcept = 0;

    /// The body, once `stage()` is Done. Bounded by the adapter.
    virtual std::string_view body() const noexcept = 0;

    /// Why the last attempt failed, in words that can go on a 52-pixel panel
    /// or in a log a person will read. Empty when nothing has failed.
    virtual std::string_view failure() const noexcept = 0;

    /// Throw away the result and return to Idle, ready for the next fetch.
    virtual void reset() = 0;
};

}  // namespace platform
}  // namespace stipple
