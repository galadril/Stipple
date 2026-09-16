// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/host/ApplicationHost.h"

#include <string>

#include "notrix/asset/IconStore.h"
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

// --- display power -----------------------------------------------------------

NOTRIX_TEST(Host, DisplayPowerOffBlanksThePanel) {
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000);

    notrix::config::ConfigStore store(platform.storage());
    notrix::config::Config saved;
    saved.display.power = false;
    store.save(saved);

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 1000);

    // Presented, not merely skipped: the panel has to actually go dark rather
    // than hold whatever happened to be on it.
    NOTRIX_CHECK(platform.simulatedDisplay().presentCount() > 0);
    NOTRIX_CHECK_EQ(countLit(host.frame()), 0);
}

NOTRIX_TEST(Host, SwitchingTheDisplayOffTakesEffectImmediately) {
    // The bug this guards: with dirty rendering, a panel switched off mid-minute
    // would otherwise stay lit until the clock next changed.
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000);

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 1000);
    NOTRIX_CHECK(countLit(host.frame()) > 0);

    host.settings().display.power = false;
    run(host, platform, 1200);

    NOTRIX_CHECK_EQ(countLit(host.frame()), 0);
}

NOTRIX_TEST(Host, SwitchingTheDisplayBackOnRestoresIt) {
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000);

    ApplicationHost host(platform, quietConfig());
    host.initialize();

    host.settings().display.power = false;
    run(host, platform, 1000);
    NOTRIX_CHECK_EQ(countLit(host.frame()), 0);

    host.settings().display.power = true;
    run(host, platform, 2000);

    NOTRIX_CHECK(countLit(host.frame()) > 0);
}

NOTRIX_TEST(Host, ADarkPanelOnlyRedrawsForThePeriodicRefresh) {
    // An off switch that still rendered black at 30 FPS would defeat its own
    // purpose. What should remain is the self-healing refresh and nothing else,
    // so this is asserted against the refresh cadence rather than against zero.
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000);

    HostConfig config = quietConfig();
    config.frame.periodicRefreshMillis = 5000;
    ApplicationHost host(platform, config);
    host.initialize();

    host.settings().display.power = false;
    run(host, platform, 1000);
    const int settled = static_cast<int>(host.frameStats().rendered);

    const std::uint64_t spanMillis = 20000;
    run(host, platform, 1000 + spanMillis);

    const int drawn = static_cast<int>(host.frameStats().rendered) - settled;
    const int refreshes = static_cast<int>(spanMillis / config.frame.periodicRefreshMillis);
    NOTRIX_CHECK(drawn <= refreshes + 1);

    // And the comparison that gives the number meaning: a lit clock over the
    // same span redraws many times more often.
    SimulatorPlatform lit;
    lit.simulatedClock().setWallClock(1'700'000'000);
    ApplicationHost litHost(lit, config);
    litHost.initialize();
    run(litHost, lit, 1000);
    const int litSettled = static_cast<int>(litHost.frameStats().rendered);
    run(litHost, lit, 1000 + spanMillis);

    NOTRIX_CHECK(static_cast<int>(litHost.frameStats().rendered) - litSettled > drawn);
}

NOTRIX_TEST(Host, TimeKeepsRunningWhileTheDisplayIsOff) {
    // Switching the panel back on should show the current moment, not resume
    // where it left off.
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000);

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    host.settings().display.power = false;
    run(host, platform, 500);

    platform.simulatedClock().setWallClock(1'700'003'600);  // an hour later
    run(host, platform, 1500);
    host.settings().display.power = true;
    run(host, platform, 3000);

    NOTRIX_CHECK(countLit(host.frame()) > 0);
}

// --- volume and brightness from the buttons ----------------------------------

NOTRIX_TEST(Host, TappingPlusAndMinusChangesVolume) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    const int start = static_cast<int>(host.settings().audio.volumePercent);
    const int step = host.inputMapper().config().volumeStepPercent;

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 100, 50);
    host.tick(200);
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), start + step);

    platform.simulatedInput().pressAndRelease(RawInput::KeyMinus, 300, 50);
    host.tick(400);
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), start);
}

NOTRIX_TEST(Host, VolumeReachesTheSpeaker) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.settings().audio.volumePercent = 100;
    host.initialize();  // re-reads config, so set it again below

    host.settings().audio.volumePercent = 40;
    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 100, 50);
    host.tick(200);

    NOTRIX_CHECK_EQ(static_cast<int>(platform.simulatedAudio().volume()),
                    static_cast<int>(notrix::config::volumeToByte(45)));
}

NOTRIX_TEST(Host, VolumeStopsAtTheEnds) {
    // Holding a button against the end of the range must not wrap around.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    for (int i = 0; i < 40; ++i) {
        platform.simulatedInput().pressAndRelease(
            RawInput::KeyMinus, static_cast<std::uint64_t>(i) * 100u + 100u, 50);
        host.tick(static_cast<std::uint64_t>(i) * 100u + 180u);
    }
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), 0);

    for (int i = 0; i < 40; ++i) {
        platform.simulatedInput().pressAndRelease(
            RawInput::KeyPlus, 10000u + static_cast<std::uint64_t>(i) * 100u, 50);
        host.tick(10000u + static_cast<std::uint64_t>(i) * 100u + 80u);
    }
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), 100);
}

NOTRIX_TEST(Host, HoldingPlusAndMinusChangesBrightness) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    const int start = static_cast<int>(host.settings().display.brightness);
    const int step = host.inputMapper().config().brightnessStep;

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 100, 900);
    host.tick(1100);

    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().display.brightness), start + step);
    NOTRIX_CHECK_EQ(static_cast<int>(platform.display().brightness()), start + step);
}

NOTRIX_TEST(Host, TurningBrightnessUpWakesADarkPanel) {
    // Otherwise the button appears to do nothing on a panel that is switched
    // off, which reads as broken hardware.
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000);
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    host.settings().display.power = false;
    run(host, platform, 1000);
    NOTRIX_CHECK_EQ(countLit(host.frame()), 0);

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 1100, 900);
    run(host, platform, 4000);

    NOTRIX_CHECK(host.settings().display.power);
    NOTRIX_CHECK(countLit(host.frame()) > 0);
}

NOTRIX_TEST(Host, VolumeIsIgnoredWithoutASpeaker) {
    // An absent capability is reported, not faked.
    notrix::platform::simulator::SimulatorCapabilities none;
    none.audio = false;
    SimulatorPlatform platform(none);

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    const int start = static_cast<int>(host.settings().audio.volumePercent);

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 100, 50);
    host.tick(200);

    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), start);
    NOTRIX_CHECK(logContains(host, "no audio output"));
}

// --- clock settings reach the renderer ---------------------------------------

NOTRIX_TEST(Host, StoredClockSettingsBecomeTheRenderedStyle) {
    SimulatorPlatform platform;
    notrix::config::ConfigStore store(platform.storage());
    notrix::config::Config saved;
    saved.clock.theme = "weekday";
    saved.clock.twentyFourHour = false;
    saved.clock.leadingZero = false;
    saved.clock.showAmPm = true;
    saved.clock.color = 0xFF8800u;
    saved.clock.accentColor = 0x00FF00u;
    saved.clock.dateColor = 0xFF00FFu;
    saved.clock.dateOrder = "monthDayYear";
    saved.clock.dateSeparator = "slash";
    saved.clock.dateYear = "fourDigit";
    saved.clock.blinkPeriodMillis = 0;
    store.save(saved);

    ApplicationHost host(platform, quietConfig());
    host.initialize();

    const notrix::apps::ClockStyle style = host.clockStyle();
    NOTRIX_CHECK(style.theme == notrix::apps::ClockTheme::Weekday);
    NOTRIX_CHECK_FALSE(style.twentyFourHour);
    NOTRIX_CHECK_FALSE(style.leadingZero);
    NOTRIX_CHECK(style.showAmPm);
    NOTRIX_CHECK(style.color == notrix::rgb(255, 136, 0));
    NOTRIX_CHECK(style.accentColor == notrix::rgb(0, 255, 0));
    NOTRIX_CHECK(style.dateColor == notrix::rgb(255, 0, 255));
    NOTRIX_CHECK(style.dateOrder == notrix::apps::DateOrder::MonthDayYear);
    NOTRIX_CHECK(style.dateSeparator == notrix::apps::DateSeparator::Slash);
    NOTRIX_CHECK(style.dateYear == notrix::apps::DateYear::FourDigit);
    NOTRIX_CHECK_EQ(static_cast<int>(style.blinkPeriodMillis), 0);
}

NOTRIX_TEST(Host, UnknownClockSettingNamesFallBackInsteadOfFailing) {
    // A config written by a newer build can name a face this one does not have.
    // Degrading to the default beats refusing to show a clock at all.
    SimulatorPlatform platform;
    notrix::config::ConfigStore store(platform.storage());
    notrix::config::Config saved;
    saved.clock.theme = "holographic";
    saved.clock.dateOrder = "stardate";
    store.save(saved);

    ApplicationHost host(platform, quietConfig());
    host.initialize();

    const notrix::apps::ClockStyle style = host.clockStyle();
    NOTRIX_CHECK(style.theme == notrix::apps::ClockTheme::Minimal);
    NOTRIX_CHECK(style.dateOrder == notrix::apps::DateOrder::DayMonthYear);
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

    platform.simulatedInput().pressAndRelease(RawInput::RotaryPress, 100, 50);
    host.tick(200);

    NOTRIX_CHECK_FALSE(host.showingSplash());
}

NOTRIX_TEST(Host, ThePressThatSkipsTheSplashDoesNothingElse) {
    // The knob press is bound to pause. Tapping it to skip the splash must not
    // also pause the carousel — the user asked to move on, not to stop.
    SimulatorPlatform platform;
    HostConfig config;
    config.splashMillis = 60000;
    ApplicationHost host(platform, config);
    host.initialize();
    host.tick(0);

    platform.simulatedInput().pressAndRelease(RawInput::RotaryPress, 100, 50);
    host.tick(200);

    NOTRIX_CHECK_FALSE(host.showingSplash());
    NOTRIX_CHECK_FALSE(host.carousel().paused());

    // The next press behaves normally.
    platform.simulatedInput().pressAndRelease(RawInput::RotaryPress, 300, 50);
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

// --- icon persistence --------------------------------------------------------

NOTRIX_TEST(Host, IconsSurviveAReboot) {
    SimulatorPlatform platform;

    {
        ApplicationHost host(platform, quietConfig());
        host.initialize();

        notrix::asset::Icon icon;
        icon.id = "bell";
        icon.width = 4;
        icon.height = 4;
        icon.frameCount = 2;
        icon.frameMillis = 120;
        icon.hasTransparency = true;
        icon.transparent = colors::kMagenta;
        icon.pixels.assign(32, colors::kYellow);
        host.icons().put(std::move(icon));

        run(host, platform, 3500);  // a tick persists the change
    }

    ApplicationHost rebooted(platform, quietConfig());
    rebooted.initialize();

    const notrix::asset::Icon* restored = rebooted.icons().find("bell");
    NOTRIX_CHECK(restored != nullptr);
    NOTRIX_CHECK_EQ(restored->frameCount, 2);
    NOTRIX_CHECK_EQ(restored->frameMillis, std::uint32_t(120));
    NOTRIX_CHECK(restored->hasTransparency);
}

NOTRIX_TEST(Host, SafeModeDoesNotLoadStoredIcons) {
    // Safe mode ignores everything stored, since stored data is one of the
    // things that could have caused the failures that got us here.
    SimulatorPlatform platform;

    {
        ApplicationHost host(platform, quietConfig());
        host.initialize();
        notrix::asset::Icon icon;
        icon.id = "x";
        icon.width = 2;
        icon.height = 2;
        icon.frameCount = 1;
        icon.pixels.assign(4, colors::kRed);
        host.icons().put(std::move(icon));
        run(host, platform, 3500);
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        ApplicationHost failing(platform, quietConfig());
        failing.initialize();  // never renders
    }

    ApplicationHost recovered(platform, quietConfig());
    recovered.initialize();
    NOTRIX_CHECK(recovered.bootMode() == BootMode::SafeMode);
    NOTRIX_CHECK_EQ(recovered.icons().count(), 0);
}

NOTRIX_TEST(Host, CorruptStoredIconsDoNotStopStartup) {
    SimulatorPlatform platform;
    platform.storage().write(ApplicationHost::kIconStateKey, "NIC\x01\x7f garbage");

    ApplicationHost host(platform, quietConfig());
    NOTRIX_CHECK(host.initialize());
    NOTRIX_CHECK_EQ(host.icons().count(), 0);

    // The unusable blob is dropped rather than re-read every boot, which would
    // make the failure look intermittent.
    std::string leftover;
    NOTRIX_CHECK_FALSE(platform.storage().read(ApplicationHost::kIconStateKey, leftover));
}
