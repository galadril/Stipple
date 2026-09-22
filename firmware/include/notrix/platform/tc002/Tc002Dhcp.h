// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "notrix/net/DhcpClient.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// The sockets and the ioctls: everything the DHCP client deliberately does
/// not know about.
///
/// The protocol, the retry timing and the framing live in notrix::net, above
/// the §53 boundary, and are tested on a host. What is left here is the part
/// that can only be tried on the device - binding sockets, and putting an
/// address on an interface.
///
/// **NOTRIX has to do this itself.** There is no DHCP client on the TC002:
/// not in /bin, not as a busybox applet, because that busybox has no applets.
/// The vendor application obtains the lease in-process, which is why every
/// NOTRIX session so far has had an address - each one began by stopping a
/// program that already had one. Nothing renews it, so a NOTRIX device left
/// running long enough simply loses its network.
///
/// Two sockets, because one cannot do both jobs:
///
/// A **packet socket** carries the broadcasts. A client asking for its first
/// address has no address, so there is nothing to put in a UDP socket's
/// source field and no route to 255.255.255.255; the headers have to be built
/// by hand and handed to the link layer. A kernel without AF_PACKET falls
/// back to a UDP broadcast, which works whenever the interface already has an
/// address - the common case here, and never the first-boot one.
///
/// A **UDP socket** carries the renewals, which are unicast to a server we
/// can already reach by the time they happen.
class Tc002Dhcp {
public:
    ~Tc002Dhcp();

    Tc002Dhcp() = default;
    Tc002Dhcp(const Tc002Dhcp&) = delete;
    Tc002Dhcp& operator=(const Tc002Dhcp&) = delete;

    /// Open the sockets and start asking. Returns false only if the
    /// interface could not be read at all - a missing packet socket is not a
    /// failure, it is a fallback.
    ///
    /// The address the interface already holds becomes the one we ask for, so
    /// taking over a lease the vendor application obtained is invisible to
    /// everything else on the network.
    bool begin(const char* interfaceName, const std::string& hostname,
               std::uint64_t nowMillis);

    /// Stop, and leave the interface exactly as it is. The address does not
    /// become wrong because nobody is renewing it, and this is what the
    /// hotspot calls before taking the radio.
    void end() noexcept;

    /// Start again on the interface and hostname last used, reading the
    /// clock itself.
    ///
    /// This is what the hotspot calls on its way out, and it is the half of
    /// "give the radio back" that was missing the first time: restoring
    /// wpa_supplicant gets an association, and on this device nothing else
    /// turns an association into an address. Without it the revert leaves no
    /// access point and no reachable station, which is the one state that
    /// needs a power cycle.
    bool restart();

    /// Call once per frame. Never blocks.
    void tick(std::uint64_t nowMillis);

    bool running() const noexcept { return running_; }
    bool bound() const noexcept { return client_.bound(); }
    net::dhcp::DhcpClient::State state() const noexcept { return client_.state(); }
    const net::dhcp::Lease& lease() const noexcept { return client_.lease(); }
    std::uint32_t remainingSeconds(std::uint64_t nowMillis) const noexcept {
        return client_.remainingSeconds(nowMillis);
    }

    /// Run the whole conversation and change nothing.
    ///
    /// The first run of this code on a real device happens over the very
    /// network it is negotiating, so a lease that came back with a different
    /// address would cut the connection in the middle of the test. Observing
    /// costs one run and answers the only question that matters - whether the
    /// server hands back the address we asked to keep.
    void setObserveOnly(bool observe) noexcept { observeOnly_ = observe; }
    bool observeOnly() const noexcept { return observeOnly_; }

    /// True when AF_PACKET was unavailable and this is running on the UDP
    /// fallback. Worth showing rather than hiding: it is the difference
    /// between a client that can bootstrap from nothing and one that cannot.
    bool usingFallback() const noexcept { return packetFd_ < 0; }

    /// The last thing worth putting in the log, once. Empty when there is
    /// nothing new - the caller polls and logs rather than this reaching up
    /// into the core to do it.
    std::string takeEvent();

private:
    bool openPacketSocket();
    bool openUdpSocket();

    bool readHardwareAddress(std::uint8_t* mac) const;
    std::uint32_t readInterfaceAddress() const;

    void send(const net::dhcp::DhcpClient::Packet& packet);
    void drain(std::uint64_t nowMillis);
    void drainOne(int fd, bool framed, std::uint64_t nowMillis);

    bool applyLease(const net::dhcp::Lease& lease);
    bool setInterfaceAddress(std::uint32_t address, std::uint32_t mask);
    bool setDefaultRoute(std::uint32_t router);
    void writeResolvConf(std::uint32_t dns);

    void note(const std::string& text);

    net::dhcp::DhcpClient client_;
    std::string interface_;

    /// Kept so restart() can come back the same way it went out.
    std::string hostname_;
    std::string event_;

    bool running_ = false;
    bool observeOnly_ = false;

    /// AF_PACKET, for broadcasts that have to go out without an address.
    int packetFd_ = -1;
    int ifIndex_ = 0;

    /// AF_INET bound to port 68, for renewals and for the fallback.
    int udpFd_ = -1;

    /// Varies per packet so two retransmissions can be told apart in a
    /// capture. Nothing reassembles anything, so it means nothing else.
    std::uint16_t ident_ = 1;
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
