// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/Tc002Sntp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <thread>
#include <utility>

namespace stipple {
namespace platform {
namespace tc002 {
namespace {

constexpr std::uint16_t kNtpPort = 123;

}  // namespace

Tc002Sntp::~Tc002Sntp() { closeSocket(); }

void Tc002Sntp::begin(std::string server) {
    server_ = std::move(server);
    stage_ = Stage::Idle;
    nextAttemptMillis_ = 0;
}

void Tc002Sntp::note(std::string text) {
    if (event_.empty()) {
        event_ = std::move(text);
    }
}

std::string Tc002Sntp::takeEvent() {
    std::string taken;
    taken.swap(event_);
    return taken;
}

void Tc002Sntp::closeSocket() {
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
}

bool Tc002Sntp::applyTime(std::uint32_t unixSeconds) {
    struct timeval now;
    now.tv_sec = static_cast<time_t>(unixSeconds);
    now.tv_usec = 0;

    struct timeval before;
    const bool knewBefore = ::gettimeofday(&before, nullptr) == 0;

    if (::settimeofday(&now, nullptr) != 0) {
        // Worth saying out loud. A device that asked for the time, got it,
        // and could not use it looks identical from the panel to one that
        // never got an answer.
        note("time: got an answer but could not set the clock");
        return false;
    }

    lastCorrection_ = knewBefore ? static_cast<std::int64_t>(unixSeconds) -
                                       static_cast<std::int64_t>(before.tv_sec)
                                 : 0;
    return true;
}

void Tc002Sntp::startQuery(std::uint64_t nowMillis) {
    closeSocket();

    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        nextAttemptMillis_ = nowMillis + kRetryMillis;
        stage_ = Stage::Resolved;
        return;
    }

    // Non-blocking throughout: this process is drawing a panel, and a stalled
    // recvfrom would be a frozen display.
    const int flags = ::fcntl(socket_, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_in to;
    std::memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(kNtpPort);
    to.sin_addr.s_addr = address_;

    std::uint8_t packet[net::sntp::kPacketBytes];
    net::sntp::build(packet);

    const ssize_t sent = ::sendto(socket_, packet, sizeof(packet), 0,
                                  reinterpret_cast<struct sockaddr*>(&to), sizeof(to));
    if (sent != static_cast<ssize_t>(sizeof(packet))) {
        closeSocket();
        nextAttemptMillis_ = nowMillis + kRetryMillis;
        stage_ = Stage::Resolved;
        return;
    }

    queryStartedMillis_ = nowMillis;
    stage_ = Stage::Waiting;
}

void Tc002Sntp::tick(std::uint64_t nowMillis, bool networkUp) {
    if (server_.empty() || !networkUp) {
        return;
    }

    switch (stage_) {
        case Stage::Idle: {
            if (nowMillis < nextAttemptMillis_) {
                return;
            }
            // A dotted quad needs no resolver, which is the common case once
            // somebody has configured a server explicitly.
            struct in_addr literal;
            if (::inet_pton(AF_INET, server_.c_str(), &literal) == 1) {
                address_ = literal.s_addr;
                stage_ = Stage::Resolved;
                return;
            }

            resolution_ = std::make_shared<Resolution>();
            auto shared = resolution_;
            const std::string host = server_;
            // Detached, and communicating only through atomics, so a lookup
            // that never returns cannot hold anything up or outlive its
            // storage.
            try {
                std::thread worker([shared, host]() {
                struct addrinfo hints;
                std::memset(&hints, 0, sizeof(hints));
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_DGRAM;

                struct addrinfo* result = nullptr;
                if (::getaddrinfo(host.c_str(), nullptr, &hints, &result) == 0 &&
                    result != nullptr) {
                    const auto* in = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
                    shared->address.store(in->sin_addr.s_addr);
                    shared->ok.store(true);
                    ::freeaddrinfo(result);
                }
                shared->done.store(true);
            });
                worker.detach();
            } catch (...) {
                // A worker that will not start must not take the panel with
                // it. Reported and retried rather than thrown: this is a
                // clock, and being an hour wrong beats being a crash.
                note("time: cannot start the resolver");
                resolution_.reset();
                nextAttemptMillis_ = nowMillis + kRetryMillis;
                stage_ = Stage::Idle;
                return;
            }

            note("time: looking up " + server_);
            stage_ = Stage::Resolving;
            return;
        }

        case Stage::Resolving: {
            if (resolution_ == nullptr || !resolution_->done.load()) {
                return;
            }
            if (!resolution_->ok.load()) {
                note("time: cannot resolve " + server_);
                resolution_.reset();
                nextAttemptMillis_ = nowMillis + kRetryMillis;
                stage_ = Stage::Idle;
                return;
            }
            address_ = resolution_->address.load();
            resolution_.reset();
            stage_ = Stage::Resolved;
            return;
        }

        case Stage::Resolved: {
            if (nowMillis < nextAttemptMillis_) {
                return;
            }
            startQuery(nowMillis);
            return;
        }

        case Stage::Waiting: {
            std::uint8_t packet[net::sntp::kPacketBytes];
            const ssize_t got = ::recvfrom(socket_, packet, sizeof(packet), 0, nullptr, nullptr);
            if (got > 0) {
                net::sntp::Reply reply;
                if (net::sntp::parse(packet, static_cast<std::size_t>(got), reply)) {
                    const bool first = !synchronised_;
                    if (applyTime(reply.unixSeconds)) {
                        synchronised_ = true;
                        // The first correction is always about fifty years,
                        // because it is 1970 to now. Saying which one this is
                        // stops that looking like a fault.
                        note(first ? "time: clock set from " + server_
                                   : "time: clock checked against " + server_);
                    }
                    closeSocket();
                    nextAttemptMillis_ = nowMillis + kResyncMillis;
                    stage_ = Stage::Resolved;
                    return;
                }
                // A reply that did not survive parsing is a reason to try a
                // different moment, not to trust it.
                note("time: " + server_ + " sent something unusable");
                closeSocket();
                nextAttemptMillis_ = nowMillis + kRetryMillis;
                stage_ = Stage::Resolved;
                return;
            }

            if (nowMillis - queryStartedMillis_ >= kQueryTimeoutMillis) {
                closeSocket();
                nextAttemptMillis_ = nowMillis + (synchronised_ ? kResyncMillis : kRetryMillis);
                stage_ = Stage::Resolved;
            }
            return;
        }
    }
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
