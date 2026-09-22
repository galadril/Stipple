// SPDX-License-Identifier: GPL-3.0-or-later
//
// NOTRIX on the TC002.
//
// Not a demo: this runs ApplicationHost, the same startup, main loop, boot
// record and safe-mode fallback the emulator and the host tests run. The only
// thing that differs is which IPlatformServices it is handed.
//
// The vendor application must not be running - both drive /dev/spidev0.0 and
// neither arbitrates:
//
//     adb shell setprop ctl.stop zkswe
//     adb shell /tmp/notrix_device
//     adb shell setprop ctl.start zkswe
//
// Nothing here is persistent except configuration under /data/notrix. The
// binary lives in /tmp, which is tmpfs, so a power cycle restores the stock
// application no matter how this exits.
//
// Exits cleanly on SIGINT or SIGTERM, blanking the panel on the way out. That
// matters more than it sounds: ApplicationHost only clears its boot marker once
// frames are rendering, so a process killed mid-startup is meant to count as a
// failed boot, and three of those bring the device up in safe mode.

#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "notrix/core/Log.h"
#include "notrix/host/ApplicationHost.h"
#include "notrix/platform/tc002/Tc002Platform.h"

namespace {

volatile std::sig_atomic_t g_stop = 0;

extern "C" void onSignal(int) { g_stop = 1; }

const char* sourceName(notrix::platform::RawInput source) {
    using notrix::platform::RawInput;
    switch (source) {
        case RawInput::KeyMinus: return "-";
        case RawInput::KeyMiddle: return "middle";
        case RawInput::KeyPlus: return "+";
        case RawInput::RotaryPress: return "knob press";
        case RawInput::RotaryLeft: return "knob left";
        case RawInput::RotaryRight: return "knob right";
    }
    return "?";
}

const char* phaseName(notrix::platform::ButtonPhase phase) {
    using notrix::platform::ButtonPhase;
    switch (phase) {
        case ButtonPhase::Down: return "down";
        case ButtonPhase::Up: return "up";
        case ButtonPhase::Tick: return "tick";
    }
    return "?";
}

void sleepMillis(std::uint64_t millis) {
    if (millis == 0) {
        return;
    }
    // nanosleep: the device busybox has no sleep(1), and usleep(3) is
    // deprecated. This is the loop's only timing primitive.
    struct timespec request;
    request.tv_sec = static_cast<time_t>(millis / 1000u);
    request.tv_nsec = static_cast<long>(millis % 1000u) * 1000000L;
    nanosleep(&request, nullptr);
}

}  // namespace

/// The whole of NOTRIX's startup, with two ways in.
///
/// As an executable this is `main`. As a shared library it is called from a
/// static constructor, because that is how the TC002 framework loads its
/// application - `dlopen` on a path from /res/etc/EasyUI.cfg, and
/// constructors run before the caller can look up a single symbol
/// (ADR 0021).
///
/// Deliberately the same function either way. A device build that took a
/// different path from the one tested over ADB would be a second firmware
/// wearing the first one's tests.
int notrixMain(int argc, char** argv) {
    // Flags come out first, so they cannot be mistaken for the positional
    // arguments they follow.
    bool wantDhcp = true;
    bool observeDhcp = false;
    int hotspotAfter = -1;
    int hotspotSeconds = 0;
    const char* positional[3] = {nullptr, nullptr, nullptr};
    int positionals = 0;

    for (int i = 1; i < argc; ++i) {
        const char* argument = argv[i];
        if (std::strcmp(argument, "--no-dhcp") == 0) {
            wantDhcp = false;
        } else if (std::strcmp(argument, "--dhcp-observe") == 0) {
            observeDhcp = true;
        } else if (std::strncmp(argument, "--hotspot-seconds=", 18) == 0) {
            hotspotSeconds = std::atoi(argument + 18);
        } else if (std::strncmp(argument, "--hotspot=", 10) == 0) {
            // Seconds after startup to begin hosting, for a controlled test.
            // It is a delay rather than "now" so the panel, the web UI and
            // the lease are all settled first - a test that starts before
            // the device is up cannot tell a hotspot failure from a boot one.
            hotspotAfter = std::atoi(argument + 10);
        } else if (positionals < 3) {
            positional[positionals++] = argument;
        }
    }

    // Optional cap, mostly so a development run cannot outlive the terminal
    // that started it. Zero or absent means run until signalled.
    const int seconds = (positionals > 0) ? std::atoi(positional[0]) : 0;
    // Absent means "leave the stored setting alone". An earlier version always
    // applied a default here, which silently overrode whatever the user had
    // chosen in the web UI - a development convenience quietly overwriting real
    // configuration is the same class of mistake as a control that lies.
    const bool overrideBrightness = positionals > 1;
    const std::uint8_t brightness =
        overrideBrightness ? static_cast<std::uint8_t>(std::atoi(positional[1])) : 0;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // SIGHUP is ignored, deliberately.
    //
    // A clock is not a terminal job. Started over ADB it inherits that shell
    // as its controlling terminal, and the shell going away - the network
    // dropping, the laptop sleeping, somebody closing a window - sends SIGHUP
    // and kills it. The device then sits dark for no reason anybody watching
    // it could work out.
    //
    // It matters more than tidiness: the hotspot work deliberately takes Wi-Fi
    // down, which takes ADB with it. A firmware that died at that moment could
    // not bring the network back, and the only remaining way in would be a
    // power cycle.
    std::signal(SIGHUP, SIG_IGN);

    notrix::platform::tc002::Tc002Platform platform;
    if (!platform.open()) {
        std::fprintf(stderr,
                     "platform unavailable - is zkgui still running, or /data "
                     "not writable?\n");
        return 1;
    }

    notrix::host::ApplicationHost host(platform);
    if (!host.initialize()) {
        std::fprintf(stderr, "ApplicationHost refused to start\n");
        platform.close();
        return 1;
    }

    // After initialize(), not before: the host pushes the stored setting to the
    // display during startup, so a brightness set earlier is silently replaced
    // by whatever is in config.
    if (overrideBrightness) {
        platform.panel().setBrightness(brightness);
    }

    // Started here rather than in Tc002Platform::open(), because the transport
    // needs a handler and ApplicationHost is the handler — it cannot exist
    // before the platform it is constructed from.
    //
    // Port 80 is what a user typing the device's IP into a browser expects, and
    // the process runs as root on this device so binding it is not a problem.
    constexpr int kHttpPort = 80;
    const bool serving = platform.http().start(kHttpPort, host);

    const auto status = platform.network()->status();
    std::printf("NOTRIX on %s\n", platform.name());
    std::printf("  boot mode   : %s\n",
                notrix::host::bootModeName(host.bootMode()));
    std::printf("  failures    : %u consecutive\n",
                host.bootRecord().consecutiveFailures);
    std::printf("  network     : %s%s%s\n",
                status.connected ? status.ipv4.c_str() : "offline",
                status.hostname.empty() ? "" : " as ",
                status.hostname.c_str());
    std::printf("  wall clock  : %s\n",
                platform.clock().wallClockValid() ? "set" : "unset (no RTC)");
    if (serving && status.connected) {
        std::printf("  web         : http://%s/\n", status.ipv4.c_str());
    } else {
        std::printf("  web         : %s\n",
                    serving ? "listening" : "unavailable (port 80 in use?)");
    }
    std::printf("  brightness  : %u/255%s\n",
                static_cast<unsigned>(platform.display().brightness()),
                overrideBrightness ? " (overridden)" : " (from config)");
    // Reported at startup because the MCU answers within a second or two, and
    // "no telemetry yet" versus "link not open" are different problems.
    if (const auto* power = platform.power()) {
        const auto battery = power->battery();
        if (battery.known) {
            std::printf("  battery     : %d%%  (mcu %s)\n", battery.percent,
                        platform.mcu().version());
        } else {
            std::printf("  battery     : MCU open, no telemetry yet\n");
        }
    } else {
        std::printf("  battery     : unavailable (MCU link not open)\n");
    }
    if (wantDhcp) {
        platform.dhcp().setObserveOnly(observeDhcp);
        const bool started =
            platform.dhcp().begin("wlan0", status.hostname, platform.clock().monotonicMillis());
        std::printf("  dhcp        : %s%s%s\n",
                    started ? "running" : "unavailable",
                    observeDhcp ? " (observing, changes nothing)" : "",
                    (started && platform.dhcp().usingFallback()) ? " (udp fallback)" : "");
        const std::string opening = platform.dhcp().takeEvent();
        if (!opening.empty()) {
            host.logger().info(platform.clock().monotonicMillis(), opening);
            std::printf("                %s\n", opening.c_str());
        }
    } else {
        // Worth printing rather than leaving blank. A device with no client
        // keeps working right up until the lease it inherited runs out, which
        // is the failure this whole thing exists to stop.
        std::printf("  dhcp        : off, on an inherited lease\n");
    }
    std::printf("  clock face  : %s, utc%+d\n",
                host.settings().clock.theme.c_str(),
                host.settings().clock.utcOffsetSeconds / 3600);
    std::printf("\nrunning%s\n\n",
                (seconds > 0) ? " (time limited)" : " - Ctrl-C to stop");

    const std::uint64_t started = platform.clock().monotonicMillis();
    const std::uint64_t deadline =
        (seconds > 0) ? started + static_cast<std::uint64_t>(seconds) * 1000u : 0;

    bool reportedHealthy = false;

    // Long enough for a lease on a slow network, short enough that somebody
    // holding a new clock does not conclude it is broken. A device that
    // decided after five seconds would host every time the router was slow.
    constexpr std::uint64_t kStrandedMillis = 45000;

    bool hotspotStarted = false;
    bool hotspotShowing = false;

    while (g_stop == 0) {
        const std::uint64_t now = platform.clock().monotonicMillis();
        if (deadline != 0 && now >= deadline) {
            break;
        }

        // Drain and forward, rather than letting tick() do it silently.
        //
        // ApplicationHost::pumpInput is exactly this loop, and handleInput is
        // public for precisely this reason, so behaviour is unchanged - tick()
        // simply finds the queue already empty. What it buys is the ability to
        // say whether a button reached the firmware at all, which is otherwise
        // indistinguishable from a button whose action has no visible effect.
        notrix::platform::InputEvent event;
        while (platform.input().poll(event)) {
            std::printf("  input: %-11s %s\n", sourceName(event.source),
                        phaseName(event.phase));
            std::fflush(stdout);
            host.handleInput(event);
        }

        // Before tick(), so a request that arrives between frames is answered
        // this iteration rather than waiting for the next one.
        platform.http().poll(now);

        // Telemetry arrives unprompted and is tiny; draining it here keeps the
        // battery app's source on the same single thread as everything else.
        platform.mcu().poll();

        // Audio is fed here for the same reason the MCU is drained here: on
        // the application loop, a frame at a time, never on a thread and never
        // for longer than one frame's worth of work (blueprint §16).
        platform.audio_out().tick();

        // The broker lives on the same thread as everything else, so nothing
        // arrives while a frame is half-rendered. MqttService owns reconnect
        // policy; this only pumps the socket.
        platform.mqtt()->poll(now);

        // The lease. Nothing else on this device can obtain or renew one, so
        // without this the address is whatever the vendor application got
        // before NOTRIX started - and it expires.
        platform.dhcp().tick(now);

        // Joining a network is the longest-running thing this device does:
        // a hotspot to shut down, a supplicant to restart, an association
        // and then a lease, tens of seconds end to end. Polled here for the
        // same reason as everything else - none of it may block a frame.
        platform.wifi().poll(now);

        // The hotspot, ticked by the thing that is definitely still
        // running. This used to be a detached tool, and both live tests
        // failed on that tool staying alive rather than on anything about
        // hosting: when it stopped, nothing reverted and the device was
        // left with no access point and no station.
        // Three reasons to host, and only the first is a test.
        //
        //   --hotspot=N   asked for explicitly
        //   rescue        somebody held both buttons; stored, so it survives
        //                 the reboot they reach for next
        //   nowhere to go a device with no address and nothing configured
        //
        // The last one is what a new owner meets: an unconfigured clock that
        // cannot reach a network has no other way to explain itself, and
        // waiting for somebody to guess is not a plan.
        const bool askedByFlag =
            hotspotAfter >= 0 &&
            now >= started + static_cast<std::uint64_t>(hotspotAfter) * 1000u;
        // Both of the non-test reasons mean the same thing - this device
        // has no way for anybody to reach it - so both wait for the same
        // evidence rather than for a stored opinion.
        //
        // The rescue flag on its own was not enough of a reason. It is set
        // when somebody holds both buttons, it persists so it survives the
        // reboot they reach for next, and honouring it unconditionally meant
        // a device that had been rescued once hosted a setup network on
        // every boot from then on - seen on hardware, where it looked like a
        // crash. If an address turns up, the device is reachable and setup
        // mode has nothing left to do.
        const bool unreachable =
            !platform.dhcp().bound() && now >= started + kStrandedMillis;
        const bool askedByRescue = host.settings().network.hotspotRequested && unreachable;
        const bool nowhereToGo = host.firstRun() && unreachable;

        if (!hotspotStarted && (askedByFlag || askedByRescue || nowhereToGo)) {
            hotspotStarted = true;
            if (nowhereToGo && !askedByFlag && !askedByRescue) {
                host.logger().info(now, "no network and nothing configured; hosting");
            }
            if (hotspotSeconds > 0) {
                platform.hotspot().setRevertMillis(
                    static_cast<std::uint64_t>(hotspotSeconds) * 1000u);
            }
            if (platform.hotspot().start("NOTRIX-setup", now)) {
                hotspotShowing = true;
                // Said on the panel before anything else, because the panel
                // is the only channel left once the radio changes job.
                host.setNotice("NOTRIX",
                               std::string("join NOTRIX-setup then open ") +
                                   notrix::platform::tc002::Tc002Hotspot::kAddress);
            }
        }

        platform.hotspot().tick(now);

        // Driven by whether it is running, not by who stopped it.
        //
        // Watching tick()'s return only catches the hotspot giving up on its
        // own. A live run joined a network - which stops the hotspot from the
        // other direction entirely - and left the panel showing the setup
        // notice on a device that was already back on the LAN.
        if (hotspotShowing && !platform.hotspot().running()) {
            hotspotShowing = false;
            host.clearNotice();
        }

        // Back on a network, so setup mode is over and must not come back
        // after the next reboot.
        if (platform.dhcp().bound() && !platform.hotspot().running()) {
            host.clearHotspotRequest();
        }
        const std::string hotspotEvent = platform.hotspot().takeEvent();
        if (!hotspotEvent.empty()) {
            host.logger().info(now, hotspotEvent);
            std::printf("  %s\n", hotspotEvent.c_str());
            std::fflush(stdout);
        }
        const std::string leaseEvent = platform.dhcp().takeEvent();
        if (!leaseEvent.empty()) {
            host.logger().info(now, leaseEvent);
            std::printf("  %s\n", leaseEvent.c_str());
            std::fflush(stdout);
        }

        const std::uint32_t renderedBefore = host.frameStats().rendered;

        if (!host.tick(now)) {
            break;
        }

        // Dirty rendering and this panel disagree, and the panel wins.
        //
        // FrameScheduler skips a frame when nothing changed, which is correct
        // everywhere else and fatal here: the driver chips hold an image only
        // while something keeps writing, so a skipped frame is a dark frame. A
        // static clock face - the most common thing this device will ever show
        // - skips almost every tick.
        //
        // refresh() re-sends the bytes already encoded rather than re-rendering,
        // so the saving dirty tracking exists for (not running the renderer) is
        // kept, and only the unavoidable SPI write is repeated.
        if (host.frameStats().rendered == renderedBefore) {
            platform.panel().refresh();
        }

        // Worth saying once: it is the signal that the boot marker has been
        // cleared, so this start will not count toward safe mode.
        if (!reportedHealthy && host.healthy()) {
            reportedHealthy = true;
            std::printf("boot recorded healthy after %u frames\n",
                        host.frameStats().rendered);
            std::fflush(stdout);
        }

        // nextDueMillis is what keeps this loop off the CPU between frames, but
        // it cannot be honoured in full here: it happily returns "nothing due
        // for a second" for a static face, and a second without a write is a
        // second of dark panel. Cap the wait at the display's own minimum
        // interval so the refresh above keeps the image alive, and so a signal
        // is noticed promptly either way.
        const std::uint64_t refreshCap =
            static_cast<std::uint64_t>(platform.display().minimumFrameIntervalMillis());
        const std::uint64_t due = host.nextDueMillis(now);
        const std::uint64_t wait = (due > now) ? (due - now) : 0;
        sleepMillis(wait > refreshCap ? refreshCap : wait);
    }

    std::printf("\nshutting down\n");
    host.shutdown();
    platform.close();

    // skipped should comfortably exceed rendered: a static clock face redraws
    // rarely, and a skipped count of zero means dirty tracking is not working
    // rather than that the device is busy. overruns should be zero - any other
    // value means the panel cannot sustain the configured rate.
    const auto& stats = host.frameStats();
    std::printf("  frames      : %u rendered, %u skipped, %u overruns\n",
                stats.rendered, stats.skipped, stats.overruns);
    std::printf("  worst frame : %u ms\n", stats.worstRenderMillis);
    std::printf("  input drops : %u\n", platform.input().droppedEventCount());
    std::printf("  http        : %u served, %u rejected\n",
                platform.http().servedCount(), platform.http().rejectedCount());
    return 0;
}

#ifdef NOTRIX_AS_LIBRARY

/// Loaded by /bin/zkgui, and never gives the process back.
///
/// The framework `dlopen`s whatever /res/etc/EasyUI.cfg names, then looks up
/// three entry points and runs its own loop. This returns to none of that:
/// the constructor takes the thread and NOTRIX owns the device from here.
///
/// **Which is why the three entry points never had to be worked out.** Their
/// names are obfuscated in libeasyui's .data and it does not matter, because
/// `dlopen` runs constructors first and this one does not come back.
///
/// Nothing is passed in. A library has no argv, and every option this
/// accepts is a development one - the flags exist for ADB sessions, and a
/// device booting into its own firmware wants the stored configuration and
/// nothing else.
__attribute__((constructor)) static void notrixTakesOver() {
    static char program[] = "notrix";
    static char* argv[] = {program, nullptr};
    notrixMain(1, argv);

    // Reached only if the loop stops - a signal, or a display that could not
    // be opened. Returning would hand control back to a framework that is
    // about to start its own UI on a panel NOTRIX has been driving, so the
    // process ends here instead. init does not respawn zkswe on its own, so
    // the device sits reachable over ADB with the panel as NOTRIX left it,
    // which is a far better place to debug from than a fight over the
    // display.
    ::_exit(0);
}

#else

int main(int argc, char** argv) { return notrixMain(argc, argv); }

#endif
