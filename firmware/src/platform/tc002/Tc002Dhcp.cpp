// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/Tc002Dhcp.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "stipple/net/Ipv4Udp.h"

namespace stipple {
namespace platform {
namespace tc002 {
namespace {

using net::dhcp::kClientPort;
using net::dhcp::kServerPort;

/// Big enough for anything that will arrive on port 68 and small enough to
/// sit on the stack of a render loop.
constexpr std::size_t kReceiveBytes = 2048;

/// Bounded, because this runs on the frame loop (§16). A burst of traffic
/// must cost a fixed slice of one frame, not however long it takes to empty
/// the socket.
constexpr int kMaxPacketsPerTick = 8;

/// Accept UDP datagrams addressed to port 68, and nothing else.
///
/// Without it the packet socket receives every frame on the interface, and on
/// a busy network the reply we are waiting for is the one that gets dropped
/// when the buffer fills. Offsets are into the IP header, because a SOCK_DGRAM
/// packet socket has already taken the link layer off.
struct sock_filter kPort68[] = {
    {0x30, 0, 0, 9},        // A = protocol
    {0x15, 0, 6, 17},       // if A != UDP, drop
    {0x28, 0, 0, 6},        // A = flags and fragment offset
    {0x45, 4, 0, 0x1FFF},   // if fragmented, drop - a later fragment has no ports
    {0xB1, 0, 0, 0},        // X = IP header length
    {0x48, 0, 0, 2},        // A = destination port
    {0x15, 0, 1, 68},       // if A != 68, drop
    {0x06, 0, 0, 0x40000},  // accept
    {0x06, 0, 0, 0},        // drop
};

void fillSockaddr(struct sockaddr* out, std::uint32_t address) {
    struct sockaddr_in in;
    std::memset(&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_addr.s_addr = htonl(address);
    std::memcpy(out, &in, sizeof(in));
}

}  // namespace

Tc002Dhcp::~Tc002Dhcp() { end(); }

void Tc002Dhcp::note(const std::string& text) { event_ = text; }

std::string Tc002Dhcp::takeEvent() {
    std::string taken;
    taken.swap(event_);
    return taken;
}

bool Tc002Dhcp::readHardwareAddress(std::uint8_t* mac) const {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    struct ifreq request;
    std::memset(&request, 0, sizeof(request));
    std::snprintf(request.ifr_name, IFNAMSIZ, "%s", interface_.c_str());

    const bool ok = ::ioctl(fd, SIOCGIFHWADDR, &request) >= 0;
    if (ok) {
        std::memcpy(mac, request.ifr_hwaddr.sa_data, net::dhcp::kHardwareBytes);
    }
    ::close(fd);
    return ok;
}

std::uint32_t Tc002Dhcp::readInterfaceAddress() const {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return 0;
    }

    struct ifreq request;
    std::memset(&request, 0, sizeof(request));
    request.ifr_addr.sa_family = AF_INET;
    std::snprintf(request.ifr_name, IFNAMSIZ, "%s", interface_.c_str());

    std::uint32_t address = 0;
    if (::ioctl(fd, SIOCGIFADDR, &request) >= 0) {
        struct sockaddr_in in;
        std::memcpy(&in, &request.ifr_addr, sizeof(in));
        address = ntohl(in.sin_addr.s_addr);
    }
    ::close(fd);
    return address;
}

bool Tc002Dhcp::openPacketSocket() {
    packetFd_ = ::socket(AF_PACKET, SOCK_DGRAM, static_cast<int>(htons(ETH_P_IP)));
    if (packetFd_ < 0) {
        // Not fatal. A kernel without AF_PACKET can still renew a lease; it
        // just cannot bootstrap from no address at all.
        return false;
    }

    struct sock_fprog program;
    program.len = static_cast<unsigned short>(sizeof(kPort68) / sizeof(kPort68[0]));
    program.filter = kPort68;
    ::setsockopt(packetFd_, SOL_SOCKET, SO_ATTACH_FILTER, &program, sizeof(program));

    struct sockaddr_ll local;
    std::memset(&local, 0, sizeof(local));
    local.sll_family = AF_PACKET;
    local.sll_protocol = htons(ETH_P_IP);
    local.sll_ifindex = ifIndex_;

    if (::bind(packetFd_, reinterpret_cast<struct sockaddr*>(&local), sizeof(local)) < 0) {
        ::close(packetFd_);
        packetFd_ = -1;
        return false;
    }

    ::fcntl(packetFd_, F_SETFL, O_NONBLOCK);
    return true;
}

bool Tc002Dhcp::openUdpSocket() {
    udpFd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (udpFd_ < 0) {
        return false;
    }

    const int one = 1;
    ::setsockopt(udpFd_, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    ::setsockopt(udpFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    // Pin it to the interface. Without this a broadcast goes out of whichever
    // route the kernel likes, which on a device that is about to become an
    // access point is not necessarily the one we mean.
    struct ifreq bindTo;
    std::memset(&bindTo, 0, sizeof(bindTo));
    std::snprintf(bindTo.ifr_name, IFNAMSIZ, "%s", interface_.c_str());
    ::setsockopt(udpFd_, SOL_SOCKET, SO_BINDTODEVICE, &bindTo, sizeof(bindTo));

    struct sockaddr_in local;
    std::memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(kClientPort);

    if (::bind(udpFd_, reinterpret_cast<struct sockaddr*>(&local), sizeof(local)) < 0) {
        ::close(udpFd_);
        udpFd_ = -1;
        return false;
    }

    ::fcntl(udpFd_, F_SETFL, O_NONBLOCK);
    return true;
}

bool Tc002Dhcp::begin(const char* interfaceName, const std::string& hostname,
                      std::uint64_t nowMillis) {
    end();

    interface_ = interfaceName != nullptr ? interfaceName : "wlan0";
    ifIndex_ = static_cast<int>(::if_nametoindex(interface_.c_str()));
    if (ifIndex_ == 0) {
        note("dhcp: no interface " + interface_);
        return false;
    }

    std::uint8_t mac[net::dhcp::kHardwareBytes] = {0, 0, 0, 0, 0, 0};
    if (!readHardwareAddress(mac)) {
        note("dhcp: cannot read the hardware address");
        return false;
    }

    openPacketSocket();
    if (!openUdpSocket()) {
        note("dhcp: port 68 is taken");
        end();
        return false;
    }

    hostname_ = hostname;
    client_.setHostname(hostname);

    // Ask for whatever is already configured. On this device that is the
    // address the vendor application obtained, and getting it back is what
    // makes taking over the lease invisible to everything else.
    const std::uint32_t existing = readInterfaceAddress();
    client_.setPreferredAddress(existing);

    // The MAC is as good a per-device seed as exists here, mixed with the
    // clock so two boots do not open with the same transaction id.
    const std::uint32_t fromMac =
        (static_cast<std::uint32_t>(mac[2]) << 24) | (static_cast<std::uint32_t>(mac[3]) << 16) |
        (static_cast<std::uint32_t>(mac[4]) << 8) | static_cast<std::uint32_t>(mac[5]);
    const std::uint32_t seed = fromMac ^ static_cast<std::uint32_t>(nowMillis);

    client_.start(mac, seed, nowMillis);
    running_ = true;

    if (packetFd_ < 0) {
        note("dhcp: no packet socket, broadcasting over UDP");
    } else if (existing != 0) {
        note("dhcp: asking to keep " + net::dhcp::formatIpv4(existing));
    } else {
        note("dhcp: asking for an address");
    }
    return true;
}

bool Tc002Dhcp::restart() {
    if (interface_.empty()) {
        return false;  // never begun, so there is nothing to come back to
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const std::uint64_t millis = static_cast<std::uint64_t>(now.tv_sec) * 1000u +
                                 static_cast<std::uint64_t>(now.tv_nsec) / 1000000u;

    // Copied, because begin() calls end(), which is free to clear them.
    const std::string interfaceName = interface_;
    const std::string hostname = hostname_;
    return begin(interfaceName.c_str(), hostname, millis);
}

void Tc002Dhcp::end() noexcept {
    client_.stop();
    running_ = false;
    if (packetFd_ >= 0) {
        ::close(packetFd_);
        packetFd_ = -1;
    }
    if (udpFd_ >= 0) {
        ::close(udpFd_);
        udpFd_ = -1;
    }
}

void Tc002Dhcp::send(const net::dhcp::DhcpClient::Packet& packet) {
    const bool broadcast = packet.destination == net::dhcp::kBroadcastAddress;

    if (broadcast && packetFd_ >= 0) {
        std::uint8_t frame[net::ipv4::kHeaderBytes + net::dhcp::kMaxMessageBytes];
        const std::size_t size =
            net::ipv4::encapsulate(packet.data, packet.size, 0, net::dhcp::kBroadcastAddress,
                                   kClientPort, kServerPort, static_cast<std::uint16_t>(ident_++),
                                   frame, sizeof(frame));
        if (size == 0) {
            return;
        }

        struct sockaddr_ll to;
        std::memset(&to, 0, sizeof(to));
        to.sll_family = AF_PACKET;
        to.sll_protocol = htons(ETH_P_IP);
        to.sll_ifindex = ifIndex_;
        to.sll_halen = static_cast<unsigned char>(ETH_ALEN);
        std::memset(to.sll_addr, 0xFF, ETH_ALEN);

        ::sendto(packetFd_, frame, size, MSG_DONTWAIT,
                 reinterpret_cast<struct sockaddr*>(&to), sizeof(to));
        return;
    }

    if (udpFd_ < 0) {
        return;
    }

    struct sockaddr_in to;
    std::memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(kServerPort);
    to.sin_addr.s_addr = htonl(broadcast ? net::dhcp::kBroadcastAddress : packet.destination);

    ::sendto(udpFd_, packet.data, packet.size, MSG_DONTWAIT,
             reinterpret_cast<struct sockaddr*>(&to), sizeof(to));
}

void Tc002Dhcp::drainOne(int fd, bool framed, std::uint64_t nowMillis) {
    if (fd < 0) {
        return;
    }

    std::uint8_t buffer[kReceiveBytes];
    for (int i = 0; i < kMaxPacketsPerTick; ++i) {
        const ssize_t got = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (got <= 0) {
            return;  // drained, or nothing there
        }

        const std::size_t size = static_cast<std::size_t>(got);
        if (!framed) {
            client_.receive(buffer, size, nowMillis);
            continue;
        }

        // The packet socket hands up whole IP packets, and the filter only
        // narrows them - it does not verify a checksum.
        net::ipv4::Datagram datagram;
        if (!net::ipv4::extract(buffer, size, datagram)) {
            continue;
        }
        if (datagram.destinationPort != kClientPort || datagram.sourcePort != kServerPort) {
            continue;
        }
        client_.receive(datagram.payload, datagram.payloadSize, nowMillis);
    }
}

void Tc002Dhcp::drain(std::uint64_t nowMillis) {
    drainOne(packetFd_, true, nowMillis);
    drainOne(udpFd_, false, nowMillis);
}

bool Tc002Dhcp::setInterfaceAddress(std::uint32_t address, std::uint32_t mask) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    struct ifreq request;
    std::memset(&request, 0, sizeof(request));
    std::snprintf(request.ifr_name, IFNAMSIZ, "%s", interface_.c_str());

    fillSockaddr(&request.ifr_addr, address);
    bool ok = ::ioctl(fd, SIOCSIFADDR, &request) >= 0;

    if (ok && mask != 0) {
        fillSockaddr(&request.ifr_netmask, mask);
        ok = ::ioctl(fd, SIOCSIFNETMASK, &request) >= 0;
    }

    // Setting an address does not necessarily bring the interface up, and an
    // interface that is down has an address nobody can reach.
    if (ok && ::ioctl(fd, SIOCGIFFLAGS, &request) >= 0) {
        request.ifr_flags = static_cast<short>(request.ifr_flags | IFF_UP | IFF_RUNNING);
        ::ioctl(fd, SIOCSIFFLAGS, &request);
    }

    ::close(fd);
    return ok;
}

bool Tc002Dhcp::setDefaultRoute(std::uint32_t router) {
    if (router == 0) {
        return true;  // nothing to do, and not a failure
    }

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    struct rtentry route;
    std::memset(&route, 0, sizeof(route));
    fillSockaddr(&route.rt_dst, 0);
    fillSockaddr(&route.rt_genmask, 0);
    fillSockaddr(&route.rt_gateway, router);
    route.rt_flags = RTF_UP | RTF_GATEWAY;
    route.rt_dev = const_cast<char*>(interface_.c_str());

    // The old default has to go first: the kernel refuses a duplicate, and
    // then the route in place is one pointing at a gateway that may have
    // changed. Failure here is expected on the first call and ignored.
    ::ioctl(fd, SIOCDELRT, &route);
    const bool ok = ::ioctl(fd, SIOCADDRT, &route) >= 0;

    ::close(fd);
    return ok;
}

void Tc002Dhcp::writeResolvConf(std::uint32_t dns) {
    if (dns == 0) {
        return;
    }

    // /etc is not writable on every build of this platform, and a device
    // without a resolver still answers on its address - which is what the
    // configuration page needs. So this is attempted and not insisted upon.
    const std::string line = "nameserver " + net::dhcp::formatIpv4(dns) + "\n";
    FILE* file = std::fopen("/etc/resolv.conf", "w");
    const bool wroteEtc = file != nullptr;
    if (file == nullptr) {
        file = std::fopen("/tmp/resolv.conf", "w");
    }
    if (file == nullptr) {
        return;
    }
    std::fwrite(line.data(), 1, line.size(), file);
    std::fclose(file);

    // Note for whoever needs DNS here later: this file is the reason
    // hostname resolution does not work on this device. The resolver only
    // reads /etc/resolv.conf, that path is on the read-only rootfs, and it
    // lists 114.114.114.114 first - a China-only service that does not answer
    // from elsewhere, so getaddrinfo stalls rather than failing.
    //
    // A bind mount of /tmp/resolv.conf over it was tried and backed out: it
    // is unproven, and writeResolvConf runs on every renewal, so it would
    // stack a fresh mount each time. The clock sidesteps the whole problem by
    // defaulting to a numeric NTP address (ClockSettings::ntpServer).
    (void)wroteEtc;
}

bool Tc002Dhcp::applyLease(const net::dhcp::Lease& granted) {
    if (observeOnly_) {
        char watched[80];
        std::snprintf(watched, sizeof(watched), "dhcp: would set %s/%d for %us",
                      net::dhcp::formatIpv4(granted.address).c_str(),
                      net::dhcp::prefixLength(granted.mask),
                      static_cast<unsigned>(granted.leaseSeconds));
        note(watched);
        return true;
    }

    if (!setInterfaceAddress(granted.address, granted.mask)) {
        note("dhcp: could not set " + net::dhcp::formatIpv4(granted.address));
        return false;
    }
    setDefaultRoute(granted.router);
    writeResolvConf(granted.dns);

    char text[80];
    std::snprintf(text, sizeof(text), "dhcp: %s/%d for %us",
                  net::dhcp::formatIpv4(granted.address).c_str(),
                  net::dhcp::prefixLength(granted.mask),
                  static_cast<unsigned>(granted.leaseSeconds));
    note(text);
    return true;
}

void Tc002Dhcp::tick(std::uint64_t nowMillis) {
    if (!running_) {
        return;
    }

    drain(nowMillis);

    net::dhcp::DhcpClient::Packet packet;
    if (client_.tick(nowMillis, packet)) {
        send(packet);
    }

    if (client_.takeAcquired()) {
        applyLease(client_.lease());
    }

    if (client_.takeLost()) {
        if (observeOnly_) {
            note("dhcp: would drop the address, lease gone");
            return;
        }
        // The address has to come off: keeping an expired lease is how two
        // devices end up sharing one. This does cut every open connection,
        // including whichever one is reading this log line - but the
        // alternative is a device that is quietly wrong on the network, which
        // is worse and much harder to notice.
        setInterfaceAddress(0, 0);
        note("dhcp: lease gone, asking again");
    }
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
