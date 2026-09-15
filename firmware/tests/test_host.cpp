// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/host/ApplicationHost.h"

#include <string>

#include "notrix/platform/simulator/SimulatorPlatform.h"
#include "support/TestFramework.h"

using notrix::Framebuffer;
using notrix::app::AppSource;
using notrix::app::Builtin;
using notrix::host::ApplicationHost;
using notrix::host::BootMode;
using notrix::host::HostConfig;
using notrix::platform::ButtonPhase;
using notrix::platform::InputEvent;
using notrix::platform::RawInput;
using notrix::platform::simulator::SimulatorPlatform;
namespace colors = notrix::colors;

namespace {

int countLit(const Framebuffer& framebuffer) {
    int count = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (framebuffer.at(x, y) != colors::kBlack) {
                ++count;
            }
        }
    }
    return count;
}

HostConfig quietConfig() {
    HostConfig config;
    config.splashMillis = 0;  // most tests are not about the splash
    return config;
}

/// Drive the host over a span of simulated time.
void run(ApplicationHost& host, SimulatorPlatform& platform, std::uint64_t untilMillis,
         int stepMillis = 10) {
    std::uint64_t now = platform.simulatedClock().monotonicMillis();
    while (now < untilMillis) {
        host.tick(now);
        platform.simulatedClock().advance(static_cast<std::uint64_t>(stepMillis));
        now = platform.simulatedClock().monotonicMillis();
    }
}

bool logContains(const ApplicationHost& host, const char* fragment) {
    for (int i = 0; i < host.logger().count(); ++i) {
        if (std::string(host.logger().at(i).message).find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

// --- startup -----------------------------------------------------------------

NOTRIX_TEST(Host, InitialisesAndInstallsTheClock) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());

    NOTRIX_CHECK(host.initialize());
    NOTRIX_CHECK(host.initialized());
    NOTRIX_CHECK(host.bootMode() == BootMode::Normal);

    const notrix::app::App* clock = host.apps().find("clock");
    NOTRIX_CHECK(clock != nullptr);
    NOTRIX_CHECK(clock->builtin == Builtin::Clock);
    NOTRIX_CHECK(clock->source == AppSource::System);
}

NOTRIX_TEST(Host, AdoptsThePanelsFrameInterval) {
    // The panel's floor must win over any configured target rate.
    SimulatorPlatform platform;
    HostConfig config = quietConfig();
    config.frame.targetFps = 120;
    ApplicationHost host(platform, config);
    host.initialize();

    NOTRIX_CHECK(host.scheduler().intervalMillis() >=
                 platform.display().minimumFrameIntervalMillis());
}

NOTRIX_TEST(Host, LogsWhatThisPlatformCannotDo) {
    // A device that cannot be reached should say why rather than look broken.
    notrix::platform::simulator::SimulatorCapabilities none;
    none.network = false;
    SimulatorPlatform platform(none);

    ApplicationHost host(platform, quietConfig());
    host.initialize();

    NOTRIX_CHECK(logContains(host, "no network interface"));
    NOTRIX_CHECK(logContains(host, "no HTTP transport"));
}

NOTRIX_TEST(Host, AppliesStoredBrightnessAtBoot) {
    SimulatorPlatform platform;
    notrix::config::ConfigStore store(platform.storage());
    notrix::config::Config saved;
    saved.display.brightness = 42;
    store.save(saved);

    ApplicationHost host(platform, quietConfig());
    host.initialize();

    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().display.brightness), 42);
    NOTRIX_CHECK_EQ(static_cast<int>(platform.display().brightness()), 42);
}

// --- the loop ----------------------------------------------------------------

NOTRIX_TEST(Host, RendersAndPresentsFrames) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    run(host, platform, 1000);

    NOTRIX_CHECK(host.frameStats().rendered > 0);
    NOTRIX_CHECK(platform.simulatedDisplay().presentCount() > 0);
}

NOTRIX_TEST(Host, StaticContentDoesNotRedrawEveryFrame) {
    // The clock's colon blinks twice a second; it must not cost 30 renders a
    // second to do that.
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000);

    ApplicationHost host(platform, quietConfig());
    host.initialize();

    run(host, platform, 10000);

    NOTRIX_CHECK(host.frameStats().skipped > host.frameStats().rendered);
}

NOTRIX_TEST(Host, TickReportsShutdown) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    NOTRIX_CHECK(host.tick(0));
    host.shutdown();
    NOTRIX_CHECK_FALSE(host.tick(10));
}

NOTRIX_TEST(Host, NextDueLetsTheCallerSleep) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    host.tick(0);

    NOTRIX_CHECK(host.nextDueMillis(0) > 0);
}

// --- the clock face ----------------------------------------------------------

NOTRIX_TEST(Host, ShowsPlaceholderUntilTheWallClockIsSet) {
    // A device that boots before NTP and confidently shows 01:00 is worse than
    // one that admits it does not know.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    const int withoutTime = countLit(host.frame());
    NOTRIX_CHECK(withoutTime > 0);  // "--:--" is drawn, not a blank panel

    platform.simulatedClock().setWallClock(1'700'000'000);
    host.scheduler().invalidate();
    run(host, platform, 400);

    NOTRIX_CHECK(countLit(host.frame()) != withoutTime);
}

// --- splash ------------------------------------------------------------------

NOTRIX_TEST(Host, ShowsASplashBeforeTheClock) {
    SimulatorPlatform platform;
    notrix::platform::NetworkStatus online;
    online.connected = true;
    online.ipv4 = "192.168.1.42";
    platform.simulatedNetwork().setStatus(online);

    HostConfig config;
    config.splashMillis = 3000;
    ApplicationHost host(platform, config);
    host.initialize();

    host.tick(0);
    NOTRIX_CHECK(host.showingSplash());
    NOTRIX_CHECK(countLit(host.frame()) > 0);

    run(host, platform, 3100);
    NOTRIX_CHECK_FALSE(host.showingSplash());
}

NOTRIX_TEST(Host, SplashScrollsSoTheAddressIsReadable) {
    // "0.1.0 - 192.168.1.42" is far wider than 52 pixels; truncating it to
    // "192..." would tell the user nothing.
    SimulatorPlatform platform;
    notrix::platform::NetworkStatus online;
    online.connected = true;
    online.ipv4 = "192.168.1.42";
    platform.simulatedNetwork().setStatus(online);

    HostConfig config;
    config.splashMillis = 5000;
    ApplicationHost host(platform, config);
    host.initialize();

    host.tick(0);
    Framebuffer early = host.frame();

    run(host, platform, 2000);
    NOTRIX_CHECK(host.showingSplash());
    NOTRIX_CHECK(host.frame() != early);
}

NOTRIX_TEST(Host, AnyButtonDismissesTheSplash) {
    SimulatorPlatform platform;
    HostConfig config;
    config.splashMillis = 60000;
    ApplicationHost host(platform, config);
    host.initialize();

    host.tick(0);
    NOTRIX_CHECK(host.showingSplash());

    platform.simulatedInput().pressAndRelease(RawInput::KeyMiddle, 100, 50);
    host.tick(200);

    NOTRIX_CHECK_FALSE(host.showingSplash());
}

NOTRIX_TEST(Host, ThePressThatSkipsTheSplashDoesNothingElse) {
    // Middle is bound to pause. Tapping it to skip the splash must not also
    // pause the carousel — the user asked to move on, not to stop.
    SimulatorPlatform platform;
    HostConfig config;
    config.splashMillis = 60000;
    ApplicationHost host(platform, config);
    host.initialize();
    host.tick(0);

    platform.simulatedInput().pressAndRelease(RawInput::KeyMiddle, 100, 50);
    host.tick(200);

    NOTRIX_CHECK_FALSE(host.showingSplash());
    NOTRIX_CHECK_FALSE(host.carousel().paused());

    // The next press behaves normally.
    platform.simulatedInput().pressAndRelease(RawInput::KeyMiddle, 300, 50);
    host.tick(400);
    NOTRIX_CHECK(host.carousel().paused());
}

NOTRIX_TEST(Host, SplashCanBeDisabled) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    host.tick(0);

    NOTRIX_CHECK_FALSE(host.showingSplash());
}

// --- anti-brick --------------------------------------------------------------

NOTRIX_TEST(Host, MarksTheBootHealthyOnceItIsClearlyRunning) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    NOTRIX_CHECK_FALSE(host.healthy());

    run(host, platform, 3500);

    NOTRIX_CHECK(host.healthy());

    std::string stored;
    NOTRIX_CHECK(platform.storage().read(ApplicationHost::kBootStateKey, stored));
    NOTRIX_CHECK(stored.find("true") != std::string::npos);
}

NOTRIX_TEST(Host, AStaticScreenStillBecomesHealthy) {
    // Regression guard. Health used to require N rendered frames, but dirty
    // rendering means a static clock face legitimately draws once and stops. A
    // frame count alone would leave such a device permanently unhealthy, so
    // every restart would count as a failure and it would fall into safe mode
    // for ever — the opposite of what the anti-brick mechanism is for.
    SimulatorPlatform platform;
    HostConfig config = quietConfig();
    config.installClockApp = false;  // nothing to animate at all
    ApplicationHost host(platform, config);
    host.initialize();

    run(host, platform, 4000);

    NOTRIX_CHECK(host.frameStats().rendered < 3);  // genuinely static
    NOTRIX_CHECK(host.healthy());
}

NOTRIX_TEST(Host, CountsABootThatNeverRenderedAsAFailure) {
    // Initialising and then dying before any frame is the signature of a boot
    // loop, and it must survive the reboot.
    SimulatorPlatform platform;
    {
        ApplicationHost host(platform, quietConfig());
        host.initialize();  // no ticks: never got to a frame
    }

    ApplicationHost second(platform, quietConfig());
    second.initialize();
    NOTRIX_CHECK_EQ(second.bootRecord().consecutiveFailures, std::uint32_t(1));
    NOTRIX_CHECK(second.bootMode() == BootMode::Normal);  // one failure is not enough
}

NOTRIX_TEST(Host, FallsIntoSafeModeAfterRepeatedFailures) {
    // The anti-brick guarantee: a poisonous config or app cannot leave a clock
    // that has to be opened up to recover.
    SimulatorPlatform platform;

    for (int attempt = 0; attempt < 3; ++attempt) {
        ApplicationHost host(platform, quietConfig());
        host.initialize();  // crashes before rendering, three times over
    }

    ApplicationHost recovered(platform, quietConfig());
    recovered.initialize();

    NOTRIX_CHECK(recovered.bootMode() == BootMode::SafeMode);
    NOTRIX_CHECK(logContains(recovered, "safe mode"));
}

NOTRIX_TEST(Host, SafeModeIgnoresStoredSettingsAndApps) {
    SimulatorPlatform platform;

    notrix::config::ConfigStore store(platform.storage());
    notrix::config::Config saved;
    saved.deviceName = "poisoned";
    saved.display.brightness = 3;
    store.save(saved);

    for (int attempt = 0; attempt < 3; ++attempt) {
        ApplicationHost host(platform, quietConfig());
        host.initialize();
    }

    ApplicationHost recovered(platform, quietConfig());
    recovered.initialize();

    NOTRIX_CHECK(recovered.bootMode() == BootMode::SafeMode);
    NOTRIX_CHECK_EQ(recovered.settings().deviceName, std::string("notrix"));
    NOTRIX_CHECK_EQ(recovered.apps().count(), 0);
}

NOTRIX_TEST(Host, SafeModeStillDrawsSomething) {
    // A blank panel is indistinguishable from a dead device.
    SimulatorPlatform platform;
    for (int attempt = 0; attempt < 3; ++attempt) {
        ApplicationHost host(platform, quietConfig());
        host.initialize();
    }

    ApplicationHost recovered(platform, quietConfig());
    recovered.initialize();
    run(recovered, platform, 500);

    NOTRIX_CHECK(countLit(recovered.frame()) > 0);
}

NOTRIX_TEST(Host, HealthyBootClearsTheFailureCount) {
    SimulatorPlatform platform;
    {
        ApplicationHost failed(platform, quietConfig());
        failed.initialize();
    }

    ApplicationHost good(platform, quietConfig());
    good.initialize();
    run(good, platform, 3500);
    NOTRIX_CHECK(good.healthy());

    ApplicationHost next(platform, quietConfig());
    next.initialize();
    NOTRIX_CHECK_EQ(next.bootRecord().consecutiveFailures, std::uint32_t(0));
}

NOTRIX_TEST(Host, UnreadableBootRecordDoesNotStopStartup) {
    SimulatorPlatform platform;
    platform.storage().write(ApplicationHost::kBootStateKey, "{ corrupt");

    ApplicationHost host(platform, quietConfig());
    NOTRIX_CHECK(host.initialize());
    NOTRIX_CHECK(host.bootMode() == BootMode::Normal);
}

// --- API wiring --------------------------------------------------------------

NOTRIX_TEST(Host, ServesTheApi) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    notrix::api::Request request;
    request.method = notrix::api::Method::Get;
    request.path = "/api/v1/apps";

    const notrix::api::Response response = host.handle(request);
    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK(response.body.find("clock") != std::string::npos);
}

NOTRIX_TEST(Host, MutatingApiCallsTriggerARedraw) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 500);

    // Settle into the steady state where nothing needs redrawing.
    host.scheduler().resetStats();
    run(host, platform, 1000);
    const std::uint32_t before = host.frameStats().rendered;

    notrix::api::Request request;
    request.method = notrix::api::Method::Post;
    request.path = "/api/v1/notifications";
    request.body = R"({"text":"Doorbell"})";
    host.handle(request);

    run(host, platform, 1200);
    NOTRIX_CHECK(host.frameStats().rendered > before);
}

NOTRIX_TEST(Host, NotificationsInterruptTheCarousel) {
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 500);

    const Framebuffer clockFace = host.frame();

    notrix::notify::Notification alert;
    alert.text = "Door";
    alert.durationSeconds = 5;
    host.notifications().push(std::move(alert), platform.simulatedClock().monotonicMillis());

    run(host, platform, 1000);
    NOTRIX_CHECK(host.frame() != clockFace);
}
