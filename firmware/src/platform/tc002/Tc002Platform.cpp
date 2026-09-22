// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/tc002/Tc002Platform.h"

#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "notrix/platform/tc002/WirelessStats.h"

namespace notrix {
namespace platform {
namespace tc002 {
namespace {

/// Same source as Tc002Input's timestamps, and it has to stay that way:
/// InputMapper subtracts one from the other to get press duration, and two
/// different clocks would make a long press come out negative.
std::uint64_t monotonicNowMillis() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000u +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000000u;
}

std::int64_t realtimeNowSeconds() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec);
}

/// A key is usable as a filename only if it is plainly one. No dots-only names,
/// no separators, nothing that could climb out of the directory.
bool isSafeKey(std::string_view key) noexcept {
    if (key.empty() || key.size() > 64) {
        return false;
    }
    for (const char c : key) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    // ".." would be caught by the separator rule above only in company with a
    // slash; reject it outright so no caller can rely on that coincidence.
    return key != "." && key != "..";
}

/// fsync a directory so a rename is durable, not just visible.
void syncDirectory(const std::string& path) noexcept {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
}

}  // namespace

// --- clock ------------------------------------------------------------------

std::uint64_t Tc002Clock::monotonicMillis() const { return monotonicNowMillis(); }

bool Tc002Clock::wallClockValid() const {
    return realtimeNowSeconds() >= kPlausibleEpoch;
}

std::int64_t Tc002Clock::unixSeconds() const { return realtimeNowSeconds(); }

// --- storage ----------------------------------------------------------------

Tc002Storage::Tc002Storage(std::string directory) : directory_(std::move(directory)) {}

bool Tc002Storage::open() {
    if (::mkdir(directory_.c_str(), 0755) != 0) {
        // Already existing is the normal case after the first boot.
        struct stat info;
        if (::stat(directory_.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
            return false;
        }
    }
    return ::access(directory_.c_str(), R_OK | W_OK | X_OK) == 0;
}

std::string Tc002Storage::pathFor(std::string_view key) const {
    if (!isSafeKey(key)) {
        return std::string();
    }
    std::string path = directory_;
    path += '/';
    path.append(key);
    return path;
}

bool Tc002Storage::exists(std::string_view key) const {
    const std::string path = pathFor(key);
    if (path.empty()) {
        return false;
    }
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool Tc002Storage::read(std::string_view key, std::string& out) const {
    out.clear();

    const std::string path = pathFor(key);
    if (path.empty()) {
        return false;
    }

    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    char chunk[1024];
    for (;;) {
        const ssize_t got = ::read(fd, chunk, sizeof(chunk));
        if (got < 0) {
            ::close(fd);
            out.clear();
            return false;
        }
        if (got == 0) {
            break;
        }
        // Bounded: a file larger than the limit is corrupt or hostile, and
        // reading it into memory is exactly what §38 forbids.
        if (out.size() + static_cast<std::size_t>(got) > kMaxValueBytes) {
            ::close(fd);
            out.clear();
            return false;
        }
        out.append(chunk, static_cast<std::size_t>(got));
    }

    ::close(fd);
    return true;
}

bool Tc002Storage::write(std::string_view key, std::string_view value) {
    if (value.size() > kMaxValueBytes) {
        return false;
    }

    const std::string path = pathFor(key);
    if (path.empty()) {
        return false;
    }

    // Write, flush, rename. The temporary lives in the same directory so the
    // rename stays within one filesystem and is therefore atomic.
    const std::string temporary = path + ".tmp";

    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }

    std::size_t written = 0;
    while (written < value.size()) {
        const ssize_t got =
            ::write(fd, value.data() + written, value.size() - written);
        if (got <= 0) {
            ::close(fd);
            ::unlink(temporary.c_str());
            return false;
        }
        written += static_cast<std::size_t>(got);
    }

    // Without this the rename can land before the data does, and a power cut
    // between them leaves a file that exists and is empty - which is worse than
    // one that was never written, because it looks like valid stored state.
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(temporary.c_str());
        return false;
    }
    ::close(fd);

    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        return false;
    }

    syncDirectory(directory_);
    return true;
}

bool Tc002Storage::remove(std::string_view key) {
    const std::string path = pathFor(key);
    if (path.empty()) {
        return false;
    }
    if (::unlink(path.c_str()) != 0) {
        return false;
    }
    syncDirectory(directory_);
    return true;
}

// --- network ----------------------------------------------------------------

namespace {

/// Read a small file whole. Returns empty on any failure, which every caller
/// here treats as "this platform cannot say" rather than as an error.
std::string readSmallFile(const char* path) {
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        return std::string();
    }
    std::string out;
    char chunk[512];
    for (;;) {
        const ssize_t got = ::read(fd, chunk, sizeof(chunk));
        if (got <= 0) {
            break;
        }
        out.append(chunk, static_cast<std::size_t>(got));
        // Bounded, per §38. /proc/net/wireless is a few hundred bytes; a file
        // that keeps producing is not the file this was looking for.
        if (out.size() > 8192) {
            break;
        }
    }
    ::close(fd);
    return out;
}

/// The SSID, via the wireless-extensions ioctl.
///
/// There is no wpa_cli on this device and no iwgetid, so the ioctl is the only
/// route. /proc/net/wireless reports the signal but never the name.
std::string readSsid(const char* interface) {
    const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return std::string();
    }

    struct iwreq request;
    std::memset(&request, 0, sizeof(request));
    std::strncpy(request.ifr_name, interface, IFNAMSIZ - 1);

    char essid[IW_ESSID_MAX_SIZE + 1] = {};
    request.u.essid.pointer = essid;
    request.u.essid.length = IW_ESSID_MAX_SIZE;
    request.u.essid.flags = 0;

    std::string out;
    if (::ioctl(sock, SIOCGIWESSID, &request) == 0) {
        essid[IW_ESSID_MAX_SIZE] = '\0';
        out = essid;
    }
    ::close(sock);
    return out;
}

/// The interface this device joins networks on. Named once rather than spelled
/// at three call sites.
constexpr const char* kWirelessInterface = "wlan0";

}  // namespace

NetworkStatus Tc002Network::status() const {
    NetworkStatus result;

    // Signal strength, which was reported as a flat zero until somebody looked
    // at the tile showing it.
    const wireless::Stats signal =
        wireless::parse(readSmallFile("/proc/net/wireless"), kWirelessInterface);
    result.signalKnown = signal.known;
    result.rssiDbm = signal.levelDbm;
    result.ssid = readSsid(kWirelessInterface);

    char hostname[128] = {};
    if (::gethostname(hostname, sizeof(hostname) - 1) == 0) {
        result.hostname = hostname;
    }

    struct ifaddrs* addresses = nullptr;
    if (::getifaddrs(&addresses) != 0) {
        return result;
    }

    for (struct ifaddrs* entry = addresses; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((entry->ifa_flags & IFF_UP) == 0 || (entry->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        const auto* in = reinterpret_cast<const struct sockaddr_in*>(entry->ifa_addr);
        const std::uint32_t host = ntohl(in->sin_addr.s_addr);

        char text[16];
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u", (host >> 24) & 0xFFu,
                      (host >> 16) & 0xFFu, (host >> 8) & 0xFFu, host & 0xFFu);

        result.ipv4 = text;
        result.connected = true;
        break;
    }

    ::freeifaddrs(addresses);
    return result;
}

// --- platform ---------------------------------------------------------------

bool Tc002Platform::open() {
    // Storage first: if configuration cannot persist, the host should find out
    // before it has drawn anything and started convincing the user otherwise.
    if (!storage_.open()) {
        return false;
    }
    if (!display_.open()) {
        return false;
    }
    if (!input_.open()) {
        display_.close();
        return false;
    }

    // Optional: a clock with no battery reading is still a clock, so a failure
    // here reports absence rather than refusing to start.
    mcu_.open();

    // Best effort, like the MCU. A device whose vendor audio library will
    // not load is still a clock; it just reports no speaker, and every
    // control that would have needed one disappears with it rather than
    // going quiet (ADR 0013).
    audio_.open();
    return true;
}

void Tc002Platform::close() noexcept {
    mcu_.close();
    input_.close();
    display_.close();
}

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
