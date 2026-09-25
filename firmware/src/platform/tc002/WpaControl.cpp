// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/WpaControl.h"

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace stipple {
namespace platform {
namespace tc002 {
namespace {

/// Long enough for a daemon that is busy, short enough that the panel does not
/// visibly stall if it never answers. SCAN_RESULTS on a crowded band is the
/// slowest of these and still answers in milliseconds.
constexpr int kTimeoutMillis = 300;

/// Bound, because this arrives from a daemon reporting what strangers
/// broadcast (§38). A crowded scan is a few kilobytes.
constexpr std::size_t kMaxReply = 8192;

}  // namespace

WpaControl::~WpaControl() { close(); }

bool WpaControl::open(const char* serverPath) {
    close();

    fd_ = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        return false;
    }

    struct sockaddr_un local;
    std::memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;

    char path[sizeof(local.sun_path)];
    std::snprintf(path, sizeof(path), "/tmp/stipple-wpa-%d", static_cast<int>(::getpid()));
    std::snprintf(local.sun_path, sizeof(local.sun_path), "%s", path);

    // A stale socket from a previous run would make bind fail, and the process
    // that left it is gone by definition.
    ::unlink(path);

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&local), sizeof(local)) < 0) {
        close();
        return false;
    }
    clientPath_ = path;

    struct sockaddr_un remote;
    std::memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    std::snprintf(remote.sun_path, sizeof(remote.sun_path), "%s", serverPath);

    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&remote), sizeof(remote)) < 0) {
        close();
        return false;
    }

    // Both directions. A send that blocks is as bad as a receive that does.
    struct timeval timeout;
    timeout.tv_sec = kTimeoutMillis / 1000;
    timeout.tv_usec = (kTimeoutMillis % 1000) * 1000;
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    return true;
}

void WpaControl::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (!clientPath_.empty()) {
        ::unlink(clientPath_.c_str());
        clientPath_.clear();
    }
}

std::string WpaControl::ask(std::string_view command) {
    if (fd_ < 0 || command.empty()) {
        return std::string();
    }

    if (::send(fd_, command.data(), command.size(), 0) < 0) {
        return std::string();
    }

    std::string reply;
    reply.resize(kMaxReply);
    const ssize_t got = ::recv(fd_, &reply[0], reply.size(), 0);
    if (got <= 0) {
        // A timeout lands here too, which is the common case when the daemon
        // is mid-scan. Empty means "no answer", and every caller treats that
        // the same as an answer that means nothing.
        return std::string();
    }
    reply.resize(static_cast<std::size_t>(got));
    return reply;
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
