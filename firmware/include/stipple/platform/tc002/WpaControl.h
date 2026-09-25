// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace stipple {
namespace platform {
namespace tc002 {

/// A client for wpa_supplicant's control socket.
///
/// The device has no wpa_cli, but the supplicant runs with `-C/dev/socket/` and
/// its socket is there as /dev/socket/wlan0. That socket is the whole
/// interface: a UNIX datagram socket taking plain text commands.
///
/// This is deliberately the only thing that talks to it, and it is deliberately
/// dumb: it sends a string and returns a string. What the strings *mean* lives
/// in WpaReplies.h, which is pure and tested on the host - a text protocol
/// decoded from a running daemon is exactly the code that should not also own a
/// file descriptor.
///
/// **Every call has a timeout.** This sits on the render loop, and a daemon
/// that has stopped answering must not take the clock with it (blueprint §16).
class WpaControl {
public:
    ~WpaControl();

    WpaControl() = default;
    WpaControl(const WpaControl&) = delete;
    WpaControl& operator=(const WpaControl&) = delete;

    /// Connect. Returns false if the socket is not there, which on this device
    /// means the supplicant is not running - a real state, not an error.
    bool open(const char* serverPath = "/dev/socket/wlan0");
    bool isOpen() const noexcept { return fd_ >= 0; }
    void close() noexcept;

    /// Send a command and return the reply, or an empty string on failure.
    ///
    /// Empty rather than an error code because every caller treats "no answer"
    /// and "an answer that means nothing" the same way, and a second failure
    /// channel would only be ignored.
    std::string ask(std::string_view command);

private:
    int fd_ = -1;

    /// The client socket's own path, which has to be removed on close.
    ///
    /// wpa_supplicant replies to the address it was sent from, so a client must
    /// bind one. It lives in /tmp, which is tmpfs: a stale socket file after a
    /// crash disappears at the next boot rather than accumulating in /data.
    std::string clientPath_;
};

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
