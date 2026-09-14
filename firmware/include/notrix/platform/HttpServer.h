// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "notrix/api/Http.h"

namespace notrix {
namespace platform {

/// Something that can answer an HTTP request. `ApiServer` is the implementation
/// core provides; the transport does not know or care.
class IHttpRequestHandler {
public:
    virtual ~IHttpRequestHandler() = default;
    virtual api::Response handle(const api::Request& request) = 0;
};

/// The HTTP transport: sockets, parsing, and framing.
///
/// Deliberately below the platform boundary. On the TC002 this wraps whatever
/// the FlyThings environment offers, which is one of the §46 unknowns; in a
/// browser there are no sockets at all and the emulator binds the API straight
/// into JavaScript instead. Keeping the transport here means neither answer
/// changes a single handler.
///
/// **No adapter implements this yet.** `IPlatformServices::httpServer()` returns
/// nullptr everywhere, which is the honest report (ADR 0013) rather than a stub
/// that accepts `start()` and never serves anything. The device implementation
/// arrives with Phase 7.
class IHttpServer {
public:
    virtual ~IHttpServer() = default;

    /// Begin accepting requests. Returns false if the port is unavailable.
    ///
    /// `handler` must outlive the server. Implementations must bind only to the
    /// interfaces they were asked to (§23: no default internet exposure).
    virtual bool start(int port, IHttpRequestHandler& handler) = 0;

    virtual void stop() = 0;
    virtual bool running() const = 0;

    /// The port actually bound, or 0 when not running.
    virtual int port() const = 0;
};

}  // namespace platform
}  // namespace notrix
