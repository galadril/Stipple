// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "stipple/net/DhcpMessage.h"

namespace stipple {
namespace net {
namespace dhcp {

/// The DHCP state machine, with no sockets in it.
///
/// It is told the time and handed bytes that arrived; it says when there are
/// bytes to send and where to send them. Everything that touches a socket,
/// an interface or a routing table lives in the platform adapter, which
/// leaves the part with all the timing in it testable on a host - and the
/// timing is the part that matters, because the failures here are measured in
/// hours. A renewal that is off by a factor of two looks perfect for a day.
///
/// **Nothing here ever gives up.** A client that stops asking is a device
/// that never comes back, and nobody is standing next to a clock waiting to
/// restart it. Retries back off to a minute apart and stay there.
class DhcpClient {
public:
    enum class State {
        /// Not running.
        kIdle,
        /// Broadcast a DISCOVER, waiting for somebody to offer.
        kSelecting,
        /// Accepted an offer, waiting for the server to confirm it.
        kRequesting,
        /// Holding a lease, nothing to do until T1.
        kBound,
        /// Past T1: asking the server that granted it for more time.
        kRenewing,
        /// Past T2: that server has gone quiet, asking anyone.
        kRebinding,
    };

    /// A packet to put on the wire.
    struct Packet {
        /// Host order. kBroadcastAddress means exactly that.
        std::uint32_t destination = kBroadcastAddress;
        std::size_t size = 0;
        std::uint8_t data[kMaxMessageBytes] = {};
    };

    /// First retransmission gap. The RFC suggests four seconds and then
    /// doubling, which is what this does.
    static constexpr std::uint64_t kFirstRetryMillis = 4000;

    /// Retries stop getting further apart here. A minute is often enough to
    /// catch a router that rebooted, and rare enough to be invisible.
    static constexpr std::uint64_t kMaxRetryMillis = 60000;

    /// REQUESTs that go unanswered before falling back to DISCOVER. A server
    /// that offered and then went quiet is usually a server that gave the
    /// address to somebody else.
    static constexpr int kMaxRequestAttempts = 4;

    /// Never renew more often than this, however short the lease. A server
    /// handing out sixty-second leases should not turn into a packet storm.
    static constexpr std::uint64_t kMinRenewIntervalMillis = 30000;

    /// Begin. `seed` only has to differ between boots - it seeds the
    /// transaction ids, which must not repeat across a restart or a server
    /// will answer the new client with the old client's conversation.
    void start(const std::uint8_t mac[kHardwareBytes], std::uint32_t seed,
               std::uint64_t nowMillis);

    /// Stop, keeping nothing. Does not send a RELEASE: the address stays
    /// configured and the lease runs out on its own, which is the right
    /// behaviour when the reason for stopping is that the radio is about to
    /// become an access point.
    void stop();

    /// The hostname to claim, for the router's client list. Bounded when it
    /// is put in the packet.
    void setHostname(const std::string& hostname) { hostname_ = hostname; }

    /// The address the interface already has, if any. Used as a hint so the
    /// server hands back the same one and nothing on the network notices
    /// STIPPLE taking over the lease.
    void setPreferredAddress(std::uint32_t address) noexcept { preferred_ = address; }

    /// Call every tick. Returns true when `out` should be sent.
    bool tick(std::uint64_t nowMillis, Packet& out);

    /// Feed in a packet that arrived on port 68.
    void receive(const std::uint8_t* data, std::size_t size, std::uint64_t nowMillis);

    State state() const noexcept { return state_; }
    bool bound() const noexcept {
        return state_ == State::kBound || state_ == State::kRenewing ||
               state_ == State::kRebinding;
    }
    const Lease& lease() const noexcept { return lease_; }

    /// True once, on the tick after a lease was granted - the caller
    /// configures the interface. Reading it clears it.
    bool takeAcquired() noexcept {
        const bool was = acquired_;
        acquired_ = false;
        return was;
    }

    /// True once, when a lease expired or was refused - the caller takes the
    /// address off the interface. Reading it clears it.
    ///
    /// Separate from `takeAcquired` because the two are not opposites: a
    /// renewal that succeeds raises neither, and a renewal onto a *different*
    /// address raises both.
    bool takeLost() noexcept {
        const bool was = lost_;
        lost_ = false;
        return was;
    }

    /// Seconds left on the lease, for the diagnostics page. Zero when not
    /// bound; kInfiniteLease when the server said so.
    std::uint32_t remainingSeconds(std::uint64_t nowMillis) const noexcept;

    /// For the log and the UI.
    static const char* stateName(State state) noexcept;

private:
    /// Next transaction id. A tiny LCG rather than rand(), so a test that
    /// starts the client twice with the same seed sees the same conversation
    /// and can assert on it.
    std::uint32_t nextXid() noexcept;

    void beginTransaction(State state, std::uint64_t nowMillis);
    void scheduleRetry(std::uint64_t nowMillis);
    void enterSelecting(std::uint64_t nowMillis);
    void adopt(const Lease& lease, std::uint64_t nowMillis);
    void dropLease();

    std::size_t compose(MessageType type, std::uint64_t nowMillis, Packet& out) const;

    State state_ = State::kIdle;
    std::uint8_t mac_[kHardwareBytes] = {0, 0, 0, 0, 0, 0};
    std::string hostname_;

    Lease lease_;
    Lease offered_;

    std::uint32_t xid_ = 0;
    std::uint32_t rng_ = 1;
    std::uint32_t preferred_ = 0;

    /// When the current conversation started, for the `secs` field.
    std::uint64_t transactionStartMillis_ = 0;
    std::uint64_t nextSendMillis_ = 0;
    std::uint64_t retryMillis_ = kFirstRetryMillis;
    int attempts_ = 0;

    /// Absolute deadlines, computed once when a lease is adopted.
    std::uint64_t renewAtMillis_ = 0;
    std::uint64_t rebindAtMillis_ = 0;
    std::uint64_t expireAtMillis_ = 0;
    bool infinite_ = false;

    /// Guards against a clock that steps backwards, which on this device is
    /// not hypothetical: the system clock is set from the network some way
    /// into the boot, long after this has started running.
    std::uint64_t lastTickMillis_ = 0;

    bool acquired_ = false;
    bool lost_ = false;
};

}  // namespace dhcp
}  // namespace net
}  // namespace stipple
