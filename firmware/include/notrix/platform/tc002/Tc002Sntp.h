// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "notrix/net/SntpMessage.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// Asks the network what time it is, and sets the system clock.
///
/// **The device has no RTC battery**, so every cold boot starts at 1970. The
/// stock application sets the time; NOTRIX replaces that application, so
/// without this the panel shows `__:__` forever - which is ClockApp
/// behaving correctly (ADR 0013: never show a plausible wrong time) and
/// looking exactly like a broken device.
///
/// Sockets and syscalls live here; the packet format is in
/// notrix/net/SntpMessage.h and is tested on a host.
class Tc002Sntp {
public:
    /// Tried again this often once a time has been obtained.
    ///
    /// The panel shows minutes, the part in hardware drifts slowly, and each
    /// query is a packet to somebody else's server - so this is deliberately
    /// not frequent.
    static constexpr std::uint64_t kResyncMillis = 3600u * 1000u;

    /// And this often while there is still no time at all, which is the case
    /// that actually matters to somebody watching a blank clock.
    static constexpr std::uint64_t kRetryMillis = 30u * 1000u;

    /// How long a query may go unanswered before it is abandoned. Not a
    /// blocking wait - the socket is non-blocking and this is counted across
    /// ticks.
    static constexpr std::uint64_t kQueryTimeoutMillis = 5000;

    ~Tc002Sntp();

    Tc002Sntp() = default;
    Tc002Sntp(const Tc002Sntp&) = delete;
    Tc002Sntp& operator=(const Tc002Sntp&) = delete;

    /// `server` may be a hostname or a dotted-quad address.
    void begin(std::string server);

    /// Call once per tick. `networkUp` gates everything: there is no point
    /// asking for the time down an interface with no address.
    void tick(std::uint64_t nowMillis, bool networkUp);

    /// Whether the system clock has been set from a server since boot.
    bool synchronised() const noexcept { return synchronised_; }

    /// Seconds the clock moved when it was last set, for the log. The first
    /// correction is always enormous - it is 1970 to now - and saying so is
    /// more useful than reporting a suspicious-looking jump with no context.
    std::int64_t lastCorrectionSeconds() const noexcept { return lastCorrection_; }

    /// One line, or empty. Taken rather than read, so a caller cannot log the
    /// same event twice.
    std::string takeEvent();

private:
    enum class Stage {
        Idle,
        /// A detached thread is in getaddrinfo. Threaded because resolution
        /// blocks for seconds on exactly the broken networks where this
        /// matters, and this process is also drawing the panel.
        Resolving,
        Resolved,
        Waiting,
    };

    void note(std::string text);
    void startQuery(std::uint64_t nowMillis);
    void closeSocket();
    bool applyTime(std::uint32_t unixSeconds);

    std::string server_;
    Stage stage_ = Stage::Idle;

    /// Written by the resolver thread, read here. The thread is detached and
    /// may outlive a failed lookup, so these are shared deliberately rather
    /// than owned.
    struct Resolution {
        std::atomic<bool> done{false};
        std::atomic<bool> ok{false};
        std::atomic<std::uint32_t> address{0};  // network byte order
    };
    std::shared_ptr<Resolution> resolution_;

    int socket_ = -1;
    std::uint32_t address_ = 0;
    std::uint64_t queryStartedMillis_ = 0;
    std::uint64_t nextAttemptMillis_ = 0;

    bool synchronised_ = false;
    std::int64_t lastCorrection_ = 0;
    std::string event_;
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
