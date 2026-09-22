// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starts the hotspot, waits, and puts the network back - on its own.
//
// Testing a hotspot on this device cannot be done carefully: one radio cannot
// be an access point and a station at once, so the moment this runs, Wi-Fi
// drops and every remote way of talking to the device goes with it. There is
// no way to watch it fail and intervene.
//
// So it does not need watching. It detaches from the shell that launched it,
// ignores the hangup that follows when ADB dies, runs the access point for a
// fixed time and then restores the station whatever happened. The worst case
// is a few minutes of no network, not a device that has to be power-cycled.
//
// That is also why this is a separate tool rather than a flag on the firmware.
// The firmware's own trigger is a *stored* setting, and a device that boots
// into hotspot mode because a test left a flag behind is exactly the failure
// this is trying to avoid.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <unistd.h>

#include "notrix/platform/tc002/Tc002Dhcp.h"
#include "notrix/platform/tc002/Tc002Hotspot.h"

namespace {

std::uint64_t monotonicMillis() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000u +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000000u;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string ssid = argc > 1 ? argv[1] : "NOTRIX-setup";
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 180;

    std::printf("starting '%s' for %ds; the network will drop and come back\n",
                ssid.c_str(), seconds);
    std::fflush(stdout);

    // Detach before anything else. The parent returns so adb does not sit
    // waiting on a connection that is about to be cut, and the child outlives
    // the shell.
    //
    // **The parent waits for the child to say it has detached**, and that is
    // not tidiness. Returning immediately closes the adb session, and adbd
    // kills the process group on its way out - so the child was being killed
    // microseconds old, before it had run a single line. A whole live test
    // was spent looking for a hotspot that no process had ever tried to
    // start. A pipe rather than a sleep, because the race is real and a
    // sleep only makes it less likely.
    int detached[2];
    if (::pipe(detached) < 0) {
        std::perror("pipe");
        return 1;
    }

    const pid_t child = fork();
    if (child < 0) {
        std::perror("fork");
        return 1;
    }
    if (child > 0) {
        ::close(detached[1]);
        char ready = 0;
        ::read(detached[0], &ready, 1);
        ::close(detached[0]);
        return 0;
    }
    ::close(detached[0]);

    signal(SIGHUP, SIG_IGN);
    setsid();

    // Now it is out of the shell's process group and cannot be taken down
    // with it. Only now may the parent go.
    const char ready = 1;
    ssize_t ignored = ::write(detached[1], &ready, 1);
    (void)ignored;
    ::close(detached[1]);

    // Everything from here is narrated, because the first two live runs were
    // both diagnosed from the outside - once from a user saying the hotspot
    // was there, once from it never appearing at all. A detached process that
    // says nothing about which step it reached is a process that has to be
    // guessed at, and guessing costs a power cycle.
    //
    // stdout is whatever the caller redirected it to, and the child keeps
    // that fd after the parent has gone. Run it as:
    //   /tmp/notrix_hotspot_test NOTRIX-setup 180 > /tmp/hotspot.out 2>&1
    const auto say = [](const char* what) {
        std::printf("[%llu] %s\n",
                    static_cast<unsigned long long>(monotonicMillis()), what);
        std::fflush(stdout);
    };

    say("detached");

    // The client comes first and is handed over, because the revert needs it.
    // Last time this tool stopped the access point, restarted wpa_supplicant
    // and left a device nobody could reach: an association is not an address,
    // and on this platform nothing else asks for one.
    notrix::platform::tc002::Tc002Dhcp dhcp;
    const bool haveDhcp = dhcp.begin("wlan0", "notrix", monotonicMillis());
    say(haveDhcp ? "dhcp client ready" : "dhcp client would not start");
    {
        const std::string opening = dhcp.takeEvent();
        if (!opening.empty()) {
            std::printf("    %s\n", opening.c_str());
            std::fflush(stdout);
        }
    }

    notrix::platform::tc002::Tc002Hotspot hotspot;
    hotspot.useDhcp(&dhcp);

    const std::uint64_t startedAt = monotonicMillis();

    say("starting the access point");
    if (!hotspot.start(ssid, startedAt)) {
        // start() already restored the station on its way out.
        const std::string why = hotspot.takeEvent();
        std::printf("    failed: %s\n", why.empty() ? "no reason given" : why.c_str());
        std::fflush(stdout);
        return 1;
    }
    {
        const std::string up = hotspot.takeEvent();
        if (!up.empty()) {
            std::printf("    %s\n", up.c_str());
            std::fflush(stdout);
        }
    }

    // Slept in short steps rather than one long one, so a kill lands promptly
    // if somebody does get to a shell.
    const std::uint64_t until = startedAt + static_cast<std::uint64_t>(seconds) * 1000u;
    while (monotonicMillis() < until) {
        sleep(1);
        // Ticked, so a daemon that died is noticed and reverts rather than
        // leaving an access point that serves nothing for the full run.
        if (hotspot.tick(monotonicMillis())) {
            const std::string why = hotspot.takeEvent();
            std::printf("    reverted early: %s\n",
                        why.empty() ? "no reason given" : why.c_str());
            std::fflush(stdout);
            break;
        }
    }

    say("stopping, giving the radio back");
    hotspot.stop();
    {
        const std::string back = hotspot.takeEvent();
        if (!back.empty()) {
            std::printf("    %s\n", back.c_str());
            std::fflush(stdout);
        }
    }

    // Pumped afterwards so the restored client actually gets through a
    // handshake before this process exits and stops calling it.
    const std::uint64_t settle = monotonicMillis() + 20000u;
    while (monotonicMillis() < settle && !dhcp.bound()) {
        dhcp.tick(monotonicMillis());
        const std::string event = dhcp.takeEvent();
        if (!event.empty()) {
            std::printf("    %s\n", event.c_str());
            std::fflush(stdout);
        }
        usleep(50000);
    }
    say(dhcp.bound() ? "address back, done" : "no address after the revert");
    return 0;
}
