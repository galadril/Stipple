// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/net/DhcpClient.h"

namespace stipple {
namespace net {
namespace dhcp {
namespace {

std::uint64_t saturatingSubtract(std::uint64_t value, std::uint64_t amount) noexcept {
    return value > amount ? value - amount : 0;
}

std::uint64_t millisFromSeconds(std::uint32_t seconds) noexcept {
    return static_cast<std::uint64_t>(seconds) * 1000u;
}

}  // namespace

const char* DhcpClient::stateName(State state) noexcept {
    switch (state) {
        case State::kIdle:
            return "idle";
        case State::kSelecting:
            return "selecting";
        case State::kRequesting:
            return "requesting";
        case State::kBound:
            return "bound";
        case State::kRenewing:
            return "renewing";
        case State::kRebinding:
            return "rebinding";
    }
    return "idle";
}

std::uint32_t DhcpClient::nextXid() noexcept {
    rng_ = rng_ * 1664525u + 1013904223u;
    // Zero is legal but indistinguishable from an uninitialised field in a
    // capture, and this is a protocol somebody will end up reading in
    // Wireshark at two in the morning.
    return rng_ == 0 ? 1u : rng_;
}

void DhcpClient::start(const std::uint8_t mac[kHardwareBytes], std::uint32_t seed,
                       std::uint64_t nowMillis) {
    for (std::size_t i = 0; i < kHardwareBytes; ++i) {
        mac_[i] = mac[i];
    }
    rng_ = seed == 0 ? 1u : seed;
    lease_ = Lease();
    offered_ = Lease();
    infinite_ = false;
    acquired_ = false;
    lost_ = false;
    lastTickMillis_ = nowMillis;
    enterSelecting(nowMillis);
}

void DhcpClient::stop() {
    state_ = State::kIdle;
    offered_ = Lease();
    // The lease is deliberately kept. Stopping does not make the address on
    // the interface wrong, and the caller may well want to put it back.
}

void DhcpClient::enterSelecting(std::uint64_t nowMillis) {
    offered_ = Lease();
    beginTransaction(State::kSelecting, nowMillis);
}

void DhcpClient::beginTransaction(State state, std::uint64_t nowMillis) {
    state_ = state;
    xid_ = nextXid();
    transactionStartMillis_ = nowMillis;
    nextSendMillis_ = nowMillis;
    retryMillis_ = kFirstRetryMillis;
    attempts_ = 0;
}

void DhcpClient::scheduleRetry(std::uint64_t nowMillis) {
    ++attempts_;
    nextSendMillis_ = nowMillis + retryMillis_;
    retryMillis_ = retryMillis_ * 2u;
    if (retryMillis_ > kMaxRetryMillis) {
        retryMillis_ = kMaxRetryMillis;
    }
}

void DhcpClient::dropLease() {
    lease_ = Lease();
    infinite_ = false;
    renewAtMillis_ = 0;
    rebindAtMillis_ = 0;
    expireAtMillis_ = 0;
}

void DhcpClient::adopt(const Lease& granted, std::uint64_t nowMillis) {
    Lease next = granted;

    // A renewal often answers with the address and nothing else, because
    // nothing else changed. Carrying the old values forward is the
    // difference between renewing a lease and losing the default route every
    // half hour.
    if (next.mask == 0) {
        next.mask = lease_.mask;
    }
    if (next.router == 0) {
        next.router = lease_.router;
    }
    if (next.dns == 0) {
        next.dns = lease_.dns;
    }
    if (next.server == 0) {
        next.server = lease_.server != 0 ? lease_.server : offered_.server;
    }

    const bool changed = next.address != lease_.address || next.mask != lease_.mask ||
                         next.router != lease_.router || next.dns != lease_.dns;

    lease_ = next;
    preferred_ = next.address;
    offered_ = Lease();
    state_ = State::kBound;
    attempts_ = 0;
    retryMillis_ = kFirstRetryMillis;
    infinite_ = next.leaseSeconds == kInfiniteLease;

    if (infinite_) {
        renewAtMillis_ = 0;
        rebindAtMillis_ = 0;
        expireAtMillis_ = 0;
    } else {
        const std::uint64_t leaseMillis = millisFromSeconds(next.leaseSeconds);

        std::uint64_t renewMillis = millisFromSeconds(next.renewSeconds);
        if (renewMillis < kMinRenewIntervalMillis) {
            // A very short lease should not turn into a packet storm, but
            // the floor must never land after the lease has already gone.
            const std::uint64_t half = leaseMillis / 2u;
            renewMillis = kMinRenewIntervalMillis < half ? kMinRenewIntervalMillis : half;
        }

        std::uint64_t rebindMillis = millisFromSeconds(next.rebindSeconds);
        if (rebindMillis <= renewMillis || rebindMillis >= leaseMillis) {
            rebindMillis = renewMillis + (leaseMillis - renewMillis) / 2u;
        }

        renewAtMillis_ = nowMillis + renewMillis;
        rebindAtMillis_ = nowMillis + rebindMillis;
        expireAtMillis_ = nowMillis + leaseMillis;
    }

    if (changed) {
        acquired_ = true;
    }
}

std::size_t DhcpClient::compose(MessageType type, std::uint64_t nowMillis, Packet& out) const {
    Request request;
    request.xid = xid_;
    for (std::size_t i = 0; i < kHardwareBytes; ++i) {
        request.mac[i] = mac_[i];
    }
    request.hostname = hostname_;

    const std::uint64_t elapsed = saturatingSubtract(nowMillis, transactionStartMillis_) / 1000u;
    request.secs = elapsed > 65535u ? static_cast<std::uint16_t>(65535)
                                    : static_cast<std::uint16_t>(elapsed);

    switch (state_) {
        case State::kSelecting:
            // Ask for the address we already have. On a device that inherited
            // one from the vendor application this is what keeps it: the
            // server hands the same address back and nothing on the network
            // notices the handover.
            request.requestedAddress = preferred_;
            request.broadcast = true;
            out.destination = kBroadcastAddress;
            break;

        case State::kRequesting:
            request.requestedAddress = offered_.address;
            request.serverAddress = offered_.server;
            request.broadcast = true;
            out.destination = kBroadcastAddress;
            break;

        case State::kRenewing:
            // Unicast, and the address goes in ciaddr rather than option 50 -
            // a renewal says "I have this", not "I would like this".
            request.clientAddress = lease_.address;
            request.broadcast = false;
            out.destination = lease_.server;
            break;

        case State::kRebinding:
            request.clientAddress = lease_.address;
            request.broadcast = true;
            out.destination = kBroadcastAddress;
            break;

        case State::kIdle:
        case State::kBound:
            return 0;
    }

    return build(type, request, out.data, sizeof(out.data));
}

bool DhcpClient::tick(std::uint64_t nowMillis, Packet& out) {
    if (state_ == State::kIdle) {
        return false;
    }

    // A clock that steps backwards must not park every deadline hours into
    // the future. On this device that is not hypothetical - the system clock
    // is set from the network well after this starts running.
    if (nowMillis < lastTickMillis_) {
        const std::uint64_t stepped = lastTickMillis_ - nowMillis;
        transactionStartMillis_ = saturatingSubtract(transactionStartMillis_, stepped);
        nextSendMillis_ = saturatingSubtract(nextSendMillis_, stepped);
        renewAtMillis_ = saturatingSubtract(renewAtMillis_, stepped);
        rebindAtMillis_ = saturatingSubtract(rebindAtMillis_, stepped);
        expireAtMillis_ = saturatingSubtract(expireAtMillis_, stepped);
    }
    lastTickMillis_ = nowMillis;

    if (state_ == State::kBound) {
        if (infinite_ || nowMillis < renewAtMillis_) {
            return false;
        }
        beginTransaction(State::kRenewing, nowMillis);
    } else if (state_ == State::kRenewing && nowMillis >= rebindAtMillis_) {
        beginTransaction(State::kRebinding, nowMillis);
    }

    if ((state_ == State::kRenewing || state_ == State::kRebinding) &&
        nowMillis >= expireAtMillis_) {
        // Out of time. The address has to come off the interface: keeping an
        // expired lease is how two devices end up with the same address.
        dropLease();
        lost_ = true;
        enterSelecting(nowMillis);
    }

    if (state_ == State::kRequesting && attempts_ >= kMaxRequestAttempts) {
        // Offered and then ignored. Usually means the address went to
        // somebody else while we were answering; start over rather than
        // keep asking for one we cannot have.
        enterSelecting(nowMillis);
    }

    if (nowMillis < nextSendMillis_) {
        return false;
    }

    // A renewal with no server to unicast to is a rebind that has not
    // noticed yet.
    if (state_ == State::kRenewing && lease_.server == 0) {
        beginTransaction(State::kRebinding, nowMillis);
    }

    const MessageType type =
        state_ == State::kSelecting ? MessageType::kDiscover : MessageType::kRequest;

    const std::size_t size = compose(type, nowMillis, out);
    if (size == 0) {
        scheduleRetry(nowMillis);
        return false;
    }

    out.size = size;
    scheduleRetry(nowMillis);
    return true;
}

void DhcpClient::receive(const std::uint8_t* data, std::size_t size, std::uint64_t nowMillis) {
    if (state_ == State::kIdle) {
        return;
    }

    Reply reply;
    if (!parse(data, size, reply)) {
        return;
    }
    if (reply.xid != xid_) {
        // Somebody else's conversation, or our own from before a restart.
        return;
    }

    switch (state_) {
        case State::kSelecting:
            if (reply.type == MessageType::kOffer && reply.lease.address != 0) {
                offered_ = reply.lease;
                // Same transaction id: the RFC wants the REQUEST to continue
                // the conversation the DISCOVER started.
                state_ = State::kRequesting;
                attempts_ = 0;
                retryMillis_ = kFirstRetryMillis;
                nextSendMillis_ = nowMillis;
            }
            break;

        case State::kRequesting:
        case State::kRenewing:
        case State::kRebinding:
            if (reply.type == MessageType::kAck) {
                if (reply.lease.address == 0 || reply.lease.leaseSeconds == 0) {
                    break;  // an ACK that grants nothing is not an ACK
                }
                adopt(reply.lease, nowMillis);
            } else if (reply.type == MessageType::kNak) {
                // Refused. Whatever we thought we had, we do not.
                const bool hadOne = lease_.address != 0;
                dropLease();
                if (hadOne) {
                    lost_ = true;
                }
                enterSelecting(nowMillis);
            }
            break;

        case State::kIdle:
        case State::kBound:
            break;
    }
}

std::uint32_t DhcpClient::remainingSeconds(std::uint64_t nowMillis) const noexcept {
    if (!bound()) {
        return 0;
    }
    if (infinite_) {
        return kInfiniteLease;
    }
    if (nowMillis >= expireAtMillis_) {
        return 0;
    }
    return static_cast<std::uint32_t>((expireAtMillis_ - nowMillis) / 1000u);
}

}  // namespace dhcp
}  // namespace net
}  // namespace stipple
