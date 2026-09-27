// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/Tc002HttpClient.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <thread>

#include "stipple/core/Version.h"
#include "stipple/net/HttpFetch.h"

namespace stipple {
namespace platform {
namespace tc002 {
namespace {

using net::http::ResponseParser;
using net::http::Url;

/// Closes on the way out of any path, including the ones that return early.
class Socket {
public:
    explicit Socket(int fd) noexcept : fd_(fd) {}
    ~Socket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

private:
    int fd_;
};

/// One address for a host.
///
/// `getaddrinfo` first, a dotted quad second. The order matters on this
/// device: a statically linked binary cannot dlopen glibc's NSS modules, so
/// getaddrinfo resolves nothing while appearing to work - and the fallback is
/// what keeps `http://192.168.1.10/...` working regardless. Tc002MqttClient
/// refuses hostnames outright for this reason; here a name is worth trying,
/// because the failure is one feed saying so rather than a broker that never
/// connects.
bool resolve(const std::string& host, int port, struct sockaddr_in& out) {
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<std::uint16_t>(port));

    if (::inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1) {
        return true;
    }

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        return false;
    }
    const auto* in = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
    out.sin_addr = in->sin_addr;
    ::freeaddrinfo(result);
    return true;
}

/// Connect, but give up after `seconds` rather than after the kernel's own
/// patience - which on an unreachable address is about two minutes, and would
/// hold this feed's slot for six times the fetcher's backstop.
bool connectWithin(int fd, const struct sockaddr_in& address, int seconds) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return false;
    }

    if (::connect(fd, reinterpret_cast<const struct sockaddr*>(&address),
                  sizeof(address)) != 0) {
        if (errno != EINPROGRESS) {
            return false;
        }
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        struct timeval patience;
        patience.tv_sec = seconds;
        patience.tv_usec = 0;
        if (::select(fd + 1, nullptr, &writable, nullptr, &patience) <= 0) {
            return false;
        }
        // select() says writable for a refused connection too, so the error
        // has to be read back explicitly. Without this, a closed port looks
        // like a successful connect and fails later with something
        // unhelpful.
        int error = 0;
        socklen_t length = sizeof(error);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0) {
            return false;
        }
    }

    // Back to blocking, with timeouts, for the send and the read. The rest of
    // the exchange is a straight line and does not need a state machine.
    return ::fcntl(fd, F_SETFL, flags) == 0;
}

bool sendAll(int fd, const std::string& bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t wrote =
            ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (wrote <= 0) {
            if (wrote < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += static_cast<std::size_t>(wrote);
    }
    return true;
}

}  // namespace

Tc002HttpClient::~Tc002HttpClient() {
    // The worker is detached and owns its half of the exchange through a
    // shared_ptr, so it can safely outlive this object. Nothing to wait for.
}

bool Tc002HttpClient::begin(std::string_view url) {
    if (stage_ == Stage::Running) {
        return false;
    }

    Url parsed;
    if (!net::http::parseUrl(url, parsed)) {
        stage_ = Stage::Failed;
        failure_ = "bad url";
        return false;
    }

    // Refused, not downgraded. Fetching over http what somebody asked to
    // fetch over https would put their API key on the wire of a network they
    // believed was protected - the same call the MQTT client makes about a
    // broker password. The device carries OpenSSL, so this is a gap and not
    // a wall, and saying so is what makes it a gap somebody can find.
    if (parsed.secure) {
        stage_ = Stage::Failed;
        failure_ = "https not supported yet";
        return false;
    }

    auto exchange = std::make_shared<Exchange>();
    exchange_ = exchange;

    // kVersion is a string_view, and C++17 has no operator+ for
    // string + string_view - appending is the whole conversion.
    std::string agent = "Stipple/";
    agent.append(kVersion);
    const std::string request = net::http::buildGet(parsed, agent);
    const std::string host = parsed.host;
    const int port = parsed.port;

    try {
        std::thread worker([exchange, host, port, request]() {
            struct sockaddr_in address;
            if (!resolve(host, port, address)) {
                exchange->failure = "cannot resolve " + host;
                exchange->done.store(true);
                return;
            }

            Socket socket(::socket(AF_INET, SOCK_STREAM, 0));
            if (!socket.valid()) {
                exchange->failure = "no socket";
                exchange->done.store(true);
                return;
            }

            struct timeval patience;
            patience.tv_sec = kTimeoutSeconds;
            patience.tv_usec = 0;
            ::setsockopt(socket.get(), SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof(patience));
            ::setsockopt(socket.get(), SOL_SOCKET, SO_SNDTIMEO, &patience, sizeof(patience));
            const int nodelay = 1;
            ::setsockopt(socket.get(), IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

            if (!connectWithin(socket.get(), address, kTimeoutSeconds)) {
                exchange->failure = "cannot reach " + host;
                exchange->done.store(true);
                return;
            }

            if (!sendAll(socket.get(), request)) {
                exchange->failure = "send failed";
                exchange->done.store(true);
                return;
            }

            ResponseParser parser;
            char chunk[2048];
            std::size_t total = 0;

            for (;;) {
                const ssize_t got = ::recv(socket.get(), chunk, sizeof(chunk), 0);
                if (got < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    exchange->failure = "read timed out";
                    break;
                }
                if (got == 0) {
                    // The server closed. With Connection: close that is the
                    // end of a body with no Content-Length, which is a
                    // perfectly ordinary way for a response to end.
                    break;
                }

                total += static_cast<std::size_t>(got);
                if (!parser.feed(std::string_view(chunk, static_cast<std::size_t>(got)))) {
                    exchange->failure.assign(parser.failure());
                    break;
                }
                if (parser.done()) {
                    break;
                }
                if (total > kMaxTransferBytes) {
                    // Everything worth keeping is already kept; the rest is a
                    // server talking to itself.
                    break;
                }
            }

            if (exchange->failure.empty()) {
                const net::http::Response& response = parser.response();
                if (response.status == 0) {
                    exchange->failure = "no response";
                } else {
                    exchange->status.store(response.status);
                    exchange->body = response.body;
                    exchange->ok.store(true);
                }
            }
            // Written last, and the only thing the other side polls. Every
            // field above is published by this store.
            exchange->done.store(true);
        });
        worker.detach();
    } catch (...) {
        // A thread that will not start must not take the panel with it.
        exchange_.reset();
        stage_ = Stage::Failed;
        failure_ = "cannot start fetch";
        return false;
    }

    stage_ = Stage::Running;
    failure_.clear();
    status_ = 0;
    body_.clear();
    return true;
}

void Tc002HttpClient::poll(std::uint64_t) {
    if (stage_ != Stage::Running || exchange_ == nullptr) {
        return;
    }
    if (!exchange_->done.load()) {
        return;
    }

    if (exchange_->ok.load()) {
        status_ = exchange_->status.load();
        body_ = exchange_->body;
        failure_.clear();
        stage_ = Stage::Done;
    } else {
        failure_ = exchange_->failure.empty() ? "fetch failed" : exchange_->failure;
        status_ = 0;
        body_.clear();
        stage_ = Stage::Failed;
    }
    exchange_.reset();
}

void Tc002HttpClient::reset() {
    // A worker still running is left to finish into its own shared block and
    // drop it. Detaching the result here rather than waiting is what keeps
    // reset() instant, which matters because the fetcher calls it from the
    // frame loop.
    exchange_.reset();
    stage_ = Stage::Idle;
    status_ = 0;
    body_.clear();
    failure_.clear();
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
