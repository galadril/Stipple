// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/host/ApplicationHost.h"

#include <string>

#include "notrix/apps/VisualizerApp.h"
#include "notrix/asset/IconStore.h"
#include "notrix/graphics/Canvas.h"
#include "notrix/time/Timezone.h"
#include "notrix/input/Navigator.h"
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

/// Hold the knob, which is the way into settings and back out (ADR 0017).
void holdKnob(ApplicationHost& host, SimulatorPlatform& platform, std::uint64_t atMillis) {
    platform.simulatedInput().pressAndRelease(RawInput::RotaryPress, atMillis, 900);
    host.tick(atMillis + 1000);
}

/// Turn the knob until the named setting is selected, or give up rather than
/// spin forever if it is not reachable.
bool selectSetting(ApplicationHost& host, SimulatorPlatform& platform,
                   notrix::input::SettingSlot slot, std::uint64_t atMillis) {
    for (int i = 0; i < 8; ++i) {
        if (host.navigator().current() == slot) {
            return true;
        }
        platform.simulatedInput().rotate(true, atMillis + static_cast<std::uint64_t>(i) * 200u);
        host.tick(atMillis + static_cast<std::uint64_t>(i) * 200u + 100u);
    }
    return host.navigator().current() == slot;
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

NOTRIX_TEST(Host, StoredUtcOffsetReachesTheClockFace) {
    // The gap that let a whole setting do nothing.
    //
    // clock.utcOffsetSeconds was validated by the API, persisted by the config
    // store and read back correctly, while renderClock took its offset from
    // ISystemClock instead - so every one of those passed and the panel never
    // moved. Nothing asserted the path from stored setting to rendered frame,
    // which is the only assertion that would have caught it.
    //
    // 7200 is Amsterdam in summer, which is where it was found.
    SimulatorPlatform platform;
    platform.simulatedClock().setWallClock(1'700'000'000);

    notrix::config::ConfigStore store(platform.storage());
    notrix::config::Config saved;
    saved.clock.utcOffsetSeconds = 7200;
    store.save(saved);

    ApplicationHost host(platform, quietConfig());
    host.initialize();

    NOTRIX_CHECK_EQ(host.settings().clock.utcOffsetSeconds, 7200);
    NOTRIX_CHECK_EQ(host.clockStyle().utcOffsetSeconds, 7200);
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

NOTRIX_TEST(Host, TappingPlusAndMinusChangesVolumeWhereThereIsASpeaker) {
    // These used to tap volume on hardware reporting no audio output, so they
    // did nothing whatsoever. Volume is back on them now that there is a
    // speaker behind it, which is where it belongs on a device that makes
    // noise: brightness is set once, volume is reached for.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    const int begin = static_cast<int>(host.settings().audio.volumePercent);
    const int step = host.inputMapper().config().volumeStepPercent;

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 100, 50);
    host.tick(200);
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), begin + step);

    platform.simulatedInput().pressAndRelease(RawInput::KeyMinus, 300, 50);
    host.tick(400);
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), begin);
}

NOTRIX_TEST(Host, WithNoSpeakerTheSameTapReachesBrightnessInstead) {
    // The buttons must not go dead again on hardware without audio. The
    // control still means "adjust the thing"; what the thing is depends on
    // what the device can actually do.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = false;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    const int begin = static_cast<int>(host.settings().display.brightness);
    const int step = host.inputMapper().config().brightnessStep;

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 100, 50);
    host.tick(200);
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().display.brightness), begin + step);
}

NOTRIX_TEST(Host, HoldingPlusReachesBrightnessWithoutTouchingVolume) {
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    const int begin = static_cast<int>(host.settings().display.brightness);
    const int volumeBefore = static_cast<int>(host.settings().audio.volumePercent);
    const int step = host.inputMapper().config().brightnessStep;

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 100, 900);
    host.tick(1200);

    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().display.brightness), begin + step);
    // And holding must not also move the thing a tap would have moved.
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), volumeBefore);
}
NOTRIX_TEST(Host, BrightnessFromTheButtonsReachesThePanel) {
    // Changing the stored setting without telling the display would look
    // exactly like a working control and do nothing at all.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    // Held, because a tap reaches volume on a platform with a speaker.
    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 100, 900);
    host.tick(1200);

    NOTRIX_CHECK_EQ(static_cast<int>(platform.simulatedDisplay().brightness()),
                    static_cast<int>(host.settings().display.brightness));
}

NOTRIX_TEST(Host, VolumeReachesTheSpeakerThroughSettings) {
    // Volume is no longer on a button; it lives in settings, where a device
    // with no speaker can decline to offer it at all. The plumbing still has to
    // work, so this drives the real path: hold the knob, turn to VOL, press +.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    host.settings().audio.volumePercent = 40;

    holdKnob(host, platform, 1000);
    NOTRIX_REQUIRE(host.navigator().inSettings());
    NOTRIX_REQUIRE(selectSetting(host, platform, notrix::input::SettingSlot::Volume, 2000));

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 4000, 50);
    host.tick(4100);

    const int step = host.inputMapper().config().volumeStepPercent;
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), 40 + step);
    NOTRIX_CHECK_EQ(static_cast<int>(platform.simulatedAudio().volume()),
                    static_cast<int>(notrix::config::volumeToByte(
                        static_cast<std::uint8_t>(40 + step))));
}

NOTRIX_TEST(Host, ADeviceWithNoSpeakerDoesNotOfferVolume) {
    // ADR 0013 on a panel this size: the honest way to show an absent
    // capability is not to offer the control, rather than to offer one that
    // silently does nothing. This is the defect that put volume on the buttons
    // and left them dead.
    // The simulator claims audio by default, which is how the old dead
    // bindings survived: every test that pressed those buttons had a speaker,
    // and the device does not.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = false;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    holdKnob(host, platform, 1000);
    NOTRIX_REQUIRE(host.navigator().inSettings());

    NOTRIX_CHECK_FALSE(host.navigator().available(notrix::input::SettingSlot::Volume));
    NOTRIX_CHECK_FALSE(selectSetting(host, platform, notrix::input::SettingSlot::Volume, 2000));
}

NOTRIX_TEST(Host, BrightnessStopsAtTheEnds) {
    // Holding a button against the end of the range must not wrap around: a
    // panel that goes from fully dark to fully bright on one more press reads
    // as a fault.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    // Held throughout: a tap reaches volume where there is a speaker.
    for (int i = 0; i < 40; ++i) {
        const std::uint64_t at = static_cast<std::uint64_t>(i) * 1000u + 100u;
        platform.simulatedInput().pressAndRelease(RawInput::KeyMinus, at, 900);
        host.tick(at + 950);
    }
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().display.brightness), 0);

    for (int i = 0; i < 40; ++i) {
        const std::uint64_t at = 60000u + static_cast<std::uint64_t>(i) * 1000u;
        platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, at, 900);
        host.tick(at + 950);
    }
    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().display.brightness), 255);
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

NOTRIX_TEST(Host, ServesTheConfigurationUi) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    notrix::api::Request request;
    request.method = notrix::api::Method::Get;
    request.path = "/";

    const notrix::api::Response response = host.handle(request);
    NOTRIX_CHECK_EQ(response.status, 200);
    NOTRIX_CHECK(response.contentType.find("text/html") != std::string::npos);
    NOTRIX_CHECK(response.body.find("NOTRIX") != std::string::npos);
}

NOTRIX_TEST(Host, TheUiNeverShadowsTheApi) {
    // Static files are tried first, so an asset named like an endpoint could
    // otherwise hide it. Paths under /api/ must always reach the API — including
    // unknown ones, which should get the API's explanatory 404 rather than a
    // bare "no such page".
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    notrix::api::Request request;
    request.method = notrix::api::Method::Get;
    request.path = "/api/v2/device";

    const notrix::api::Response response = host.handle(request);
    NOTRIX_CHECK_EQ(response.status, 404);
    NOTRIX_CHECK(response.body.find("/api/v1") != std::string::npos);
}

NOTRIX_TEST(Host, ServesItsOwnLog) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    notrix::api::Request request;
    request.method = notrix::api::Method::Get;
    request.path = "/api/v1/logs";

    const notrix::api::Response response = host.handle(request);
    NOTRIX_CHECK_EQ(response.status, 200);
    // Boot writes several lines, so this is never legitimately empty.
    NOTRIX_CHECK(response.body.find("NOTRIX starting") != std::string::npos);
    NOTRIX_CHECK(response.body.find("totalWritten") != std::string::npos);
}

NOTRIX_TEST(Host, ReadingTheUiIsNotLogged) {
    // The log is 24 entries. A browser fetching three files per page load would
    // push out everything worth seeing.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    const int before = host.logger().count();

    const char* paths[] = {"/", "/app.css", "/app.js"};
    for (const char* path : paths) {
        notrix::api::Request request;
        request.method = notrix::api::Method::Get;
        request.path = path;
        NOTRIX_CHECK_EQ(host.handle(request).status, 200);
    }

    NOTRIX_CHECK_EQ(host.logger().count(), before);
}

NOTRIX_TEST(Host, MqttStaysOffUntilItIsConfigured) {
    // §20: the device must be fully usable without a broker, and must never dial
    // out to one nobody asked it to talk to.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 2000);

    NOTRIX_CHECK(host.mqttService().state() == notrix::platform::MqttState::Disabled);
    NOTRIX_CHECK(platform.simulatedMqtt().published().empty());
}

NOTRIX_TEST(Host, ConfiguringMqttOverTheApiConnectsIt) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    notrix::api::Request request;
    request.method = notrix::api::Method::Patch;
    request.path = "/api/v1/settings";
    request.body = R"({"mqtt":{"enabled":true,"host":"broker.local"}})";
    NOTRIX_CHECK_EQ(host.handle(request).status, 200);

    run(host, platform, 2000);

    NOTRIX_CHECK(host.mqttService().state() == notrix::platform::MqttState::Connected);
    NOTRIX_CHECK(platform.simulatedMqtt().lastOn(host.mqttService().topics().availability) !=
                 nullptr);
}

NOTRIX_TEST(Host, SafeModeStaysOffTheBroker) {
    // Whatever put the device in safe mode might be reachable from a broker, and
    // a boot loop republishing retained state each time is worse than a quiet
    // one.
    SimulatorPlatform platform;

    notrix::config::ConfigStore store(platform.storage());
    notrix::config::Config saved;
    saved.mqtt.enabled = true;
    saved.mqtt.host = "broker.local";
    store.save(saved);

    platform.storage().write(ApplicationHost::kBootStateKey,
                             R"({"consecutiveFailures":5,"lastBootCompleted":false})");

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 2000);

    NOTRIX_CHECK(host.bootMode() == BootMode::SafeMode);
    NOTRIX_CHECK(platform.simulatedMqtt().published().empty());
}

NOTRIX_TEST(Host, ButtonPressesReachTheBroker) {
    SimulatorPlatform platform;

    // Saved rather than poked in after construction: initialize() loads stored
    // settings over whatever is in memory, so anything set beforehand is lost.
    notrix::config::ConfigStore store(platform.storage());
    notrix::config::Config saved;
    saved.mqtt.enabled = true;
    saved.mqtt.host = "broker.local";
    store.save(saved);

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 1000);

    platform.simulatedMqtt().clear();
    platform.simulatedInput().pressAndRelease(RawInput::RotaryPress, 1100, 50);
    run(host, platform, 1400);

    const notrix::platform::MqttMessage* event =
        platform.simulatedMqtt().lastOn(host.mqttService().topics().button);
    NOTRIX_REQUIRE(event != nullptr);
    NOTRIX_CHECK(std::string(event->payload).find("appAction") != std::string::npos);
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

// --- battery -----------------------------------------------------------------

NOTRIX_TEST(Host, BatteryIsOnlyInstalledWhereOneCanBeReported) {
    // A permanent "NO BATT" card in the rotation of a mains-only panel is the
    // carousel's version of a switch that does nothing.
    SimulatorPlatform mainsOnly;
    ApplicationHost without(mainsOnly, quietConfig());
    without.initialize();
    NOTRIX_CHECK(without.apps().find("battery") == nullptr);

    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    SimulatorPlatform battered(capabilities);
    ApplicationHost with(battered, quietConfig());
    with.initialize();
    NOTRIX_CHECK(with.apps().find("battery") != nullptr);
}

NOTRIX_TEST(Host, OneDetentMovesExactlyOneApp) {
    // Rotary acceleration multiplies fast detents up to 5x, which is right for
    // brightness and wrong for a carousel. With three apps installed it made an
    // ordinary turn jump two to five of them and land somewhere that looked
    // random.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    capabilities.microphone = true;
    SimulatorPlatform platform(capabilities);
    platform.simulatedClock().setWallClock(1'700'000'000);

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    // clock + visualizer + battery
    NOTRIX_CHECK_EQ(host.apps().count(), 3);

    const notrix::app::App* first = host.carousel().active();
    NOTRIX_CHECK(first != nullptr);
    const std::string startId = first->id;

    // Three detents in quick succession - well inside the 120 ms acceleration
    // window, so the mapper will report a repeat above one.
    for (int i = 0; i < 3; ++i) {
        InputEvent tick;
        tick.source = RawInput::RotaryRight;
        tick.phase = ButtonPhase::Tick;
        tick.timestampMillis = platform.simulatedClock().monotonicMillis();
        host.handleInput(tick);
        platform.simulatedClock().advance(20);
    }

    // Three detents, three apps forward. With two system apps installed that
    // is exactly one full lap back to where it started.
    const notrix::app::App* landed = host.carousel().active();
    NOTRIX_CHECK(landed != nullptr);
    NOTRIX_CHECK_EQ(landed->id, startId);
}

NOTRIX_TEST(Host, AMicrophoneThatNeverDeliversSaysSoRatherThanDrawingSilence) {
    // The exact shape of the TC002 bug. The adapter offers itself as an
    // IMicrophone the moment its serial port opens, then never receives an
    // audio frame. The old render path checked only the pointer, so the app
    // drew its baseline: a flat line across the middle of the panel, which
    // reads as a silent room rather than as a device that cannot hear.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.microphone = true;  // present...
    SimulatorPlatform platform(capabilities);
    // ...but never told to hear anything, which is what the device does.

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    NOTRIX_REQUIRE(host.carousel().activate(ApplicationHost::kVisualizerAppId,
                                            platform.simulatedClock().monotonicMillis()));
    run(host, platform, 400);

    // What a flat line would look like: the baseline spans the full width and
    // is two rows tall, and nothing else is drawn.
    const int flatline = Framebuffer::kWidth * 2;
    NOTRIX_CHECK(countLit(host.frame()) != flatline);

    // And what it should look like instead.
    Framebuffer expected;
    notrix::Canvas canvas(expected);
    notrix::apps::renderNoMicrophone(canvas, colors::kWhite);
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            NOTRIX_CHECK(host.frame().at(x, y) == expected.at(x, y));
        }
    }
}

NOTRIX_TEST(Host, TheVisualizerDrawsSoundOnceItActuallyHearsSomething) {
    // The other half: the honesty check must not have broken the working case.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.microphone = true;
    SimulatorPlatform platform(capabilities);

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    NOTRIX_REQUIRE(host.carousel().activate(ApplicationHost::kVisualizerAppId,
                                            platform.simulatedClock().monotonicMillis()));

    platform.simulatedMicrophone().hear(12000);
    run(host, platform, 600);

    Framebuffer noMic;
    notrix::Canvas canvas(noMic);
    notrix::apps::renderNoMicrophone(canvas, colors::kWhite);

    bool differs = false;
    for (int y = 0; y < Framebuffer::kHeight && !differs; ++y) {
        for (int x = 0; x < Framebuffer::kWidth && !differs; ++x) {
            differs = host.frame().at(x, y) != noMic.at(x, y);
        }
    }
    NOTRIX_CHECK(differs);
    NOTRIX_CHECK(countLit(host.frame()) > 0);
}

NOTRIX_TEST(Host, AdjustingVolumeDoesNotReconfigureTheBroker) {
    // A copy of initialize()'s MQTT setup had been spliced into the volume
    // handler. It compiled, because every line of it is a legal statement
    // inside a case block, and no test pressed a volume key while a broker was
    // configured - so every tap of the minus and plus buttons quietly re-ran
    // setContext and configure, and in safe mode wrote "safe mode: MQTT not
    // started" to the ring log on each one.
    //
    // This pins the boundary rather than the symptom: handling an input event
    // is not a configuration event, whatever the action turns out to be.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    const int before = host.logger().count();

    for (int i = 0; i < 8; ++i) {
        InputEvent down;
        down.source = RawInput::KeyPlus;
        down.phase = ButtonPhase::Down;
        down.timestampMillis = platform.simulatedClock().monotonicMillis();
        host.handleInput(down);
        platform.simulatedClock().advance(40);

        InputEvent up;
        up.source = RawInput::KeyPlus;
        up.phase = ButtonPhase::Up;
        up.timestampMillis = platform.simulatedClock().monotonicMillis();
        host.handleInput(up);
        platform.simulatedClock().advance(40);
    }

    // Whatever the button is bound to, pressing it must not talk to MQTT.
    NOTRIX_CHECK_FALSE(logContains(host, "MQTT"));
    NOTRIX_CHECK_FALSE(logContains(host, "safe mode"));
    NOTRIX_CHECK_EQ(host.logger().count(), before);
}

// --- navigating the device itself (ADR 0017) ---------------------------------

NOTRIX_TEST(Host, ThePanelSwitchIsNotOfferedOnThePanel) {
    // It is circular: the control lives on the only surface it switches off,
    // so using it hides the way back. It stays in the settings model and over
    // the API, where a browser can blank the panel and plainly still be used.
    //
    // Turning brightness up already revives a blank panel, which is the
    // gesture someone reaches for anyway - that is the recovery path, and the
    // test below it pins it.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    holdKnob(host, platform, 1000);
    NOTRIX_REQUIRE(host.navigator().inSettings());

    const bool powerBefore = host.settings().display.power;
    for (int i = 0; i < 12; ++i) {
        platform.simulatedInput().rotate(true, 2000 + static_cast<std::uint64_t>(i) * 200u);
        host.tick(2000 + static_cast<std::uint64_t>(i) * 200u + 100u);
        platform.simulatedInput().pressAndRelease(
            RawInput::KeyMinus, 2100 + static_cast<std::uint64_t>(i) * 200u, 50);
        host.tick(2100 + static_cast<std::uint64_t>(i) * 200u + 100u);
    }
    // Nothing reachable from the knob can have switched the panel off.
    NOTRIX_CHECK_EQ(host.settings().display.power, powerBefore);
}

NOTRIX_TEST(Host, TurningBrightnessUpRevivesABlankedPanel) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    host.settings().display.power = false;
    host.settings().display.brightness = 0;

    // Held, because that is what reaches brightness.
    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 1000, 900);
    host.tick(2000);

    NOTRIX_CHECK(host.settings().display.power);
    NOTRIX_CHECK(host.settings().display.brightness > 0);
}

NOTRIX_TEST(Host, TheCarouselDoesNotAdvanceWhileSettingsAreOpen) {
    // It used to. Every few seconds the timer moved the carousel underneath
    // the menu, which started a transition and slid the settings screen
    // sideways like an app - so settings read as a page in the rotation rather
    // than a mode on top of it. Leaving also landed on whatever app the timer
    // had reached rather than the one the user left.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    holdKnob(host, platform, 1000);
    NOTRIX_REQUIRE(host.navigator().inSettings());

    const std::string parked = host.carousel().active()->id;

    // Well past any app's dwell time, with activity so settings stay open.
    for (int i = 0; i < 12; ++i) {
        const std::uint64_t at = 2000 + static_cast<std::uint64_t>(i) * 3000u;
        run(host, platform, at, 100);
        platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, at, 50);
        host.tick(at + 50);
    }

    NOTRIX_CHECK_EQ(host.carousel().active()->id, parked);

    // And leaving puts the user back where they were, with a full turn ahead
    // of the app rather than an instant jump to the next one.
    const std::uint64_t leaveAt = platform.simulatedClock().monotonicMillis() + 100;
    platform.simulatedInput().pressAndRelease(RawInput::KeyMiddle, leaveAt, 50);
    host.tick(leaveAt + 60);
    run(host, platform, leaveAt + 500);
    NOTRIX_CHECK_FALSE(host.navigator().inSettings());
    NOTRIX_CHECK_EQ(host.carousel().active()->id, parked);
}

NOTRIX_TEST(Host, TheKnobMovesBetweenAppsOutsideSettingsAndSettingsInside) {
    // The one rule the whole model rests on: a control means the same thing
    // everywhere, and only what it points at changes.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    const std::string before = host.carousel().active()->id;

    platform.simulatedInput().rotate(true, 500);
    host.tick(600);
    NOTRIX_CHECK(host.carousel().active()->id != before);

    holdKnob(host, platform, 1000);
    NOTRIX_REQUIRE(host.navigator().inSettings());

    const std::string parked = host.carousel().active()->id;
    const notrix::input::SettingSlot start = host.navigator().current();

    platform.simulatedInput().rotate(true, 2000);
    host.tick(2100);

    // The cursor moved; the carousel did not.
    NOTRIX_CHECK(host.navigator().current() != start);
    NOTRIX_CHECK_EQ(host.carousel().active()->id, parked);
}

NOTRIX_TEST(Host, BackLeavesSettingsBeforeAnythingElse) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    holdKnob(host, platform, 1000);
    NOTRIX_REQUIRE(host.navigator().inSettings());

    platform.simulatedInput().pressAndRelease(RawInput::KeyMiddle, 2000, 50);
    host.tick(2100);
    NOTRIX_CHECK_FALSE(host.navigator().inSettings());
}

NOTRIX_TEST(Host, BackReturnsToTheClockWhenThereIsNothingToLeave) {
    // The last step of "back", and the one that makes it predictable: wherever
    // you are, pressing it enough times lands on the clock.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    NOTRIX_REQUIRE(host.carousel().activate(ApplicationHost::kBatteryAppId, 300));
    NOTRIX_REQUIRE(host.carousel().active()->id != std::string(ApplicationHost::kClockAppId));

    platform.simulatedInput().pressAndRelease(RawInput::KeyMiddle, 500, 50);
    host.tick(600);

    NOTRIX_CHECK_EQ(host.carousel().active()->id, std::string(ApplicationHost::kClockAppId));
}

NOTRIX_TEST(Host, SettingsCloseThemselvesIfTheUserWalksAway) {
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    holdKnob(host, platform, 1000);
    NOTRIX_REQUIRE(host.navigator().inSettings());

    run(host, platform, 2000 + notrix::input::Navigator::kIdleExitMillis, 100);
    NOTRIX_CHECK_FALSE(host.navigator().inSettings());
    NOTRIX_CHECK(logContains(host, "settings closed after idle"));
}

NOTRIX_TEST(Host, AdjustingBrightnessWhileBrowsingShowsWhatItChanged) {
    // A brightness step is invisible in daylight and at night reads as the
    // panel having glitched. A control with no feedback is indistinguishable
    // from a broken one, which is how volume sat on these buttons doing
    // nothing without anyone noticing.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 500);

    const Framebuffer quiet = host.frame();

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 600, 50);
    host.tick(700);
    run(host, platform, 800);

    NOTRIX_CHECK(host.frame() != quiet);
}

NOTRIX_TEST(Host, ChangingVolumePlaysTheNewLevel) {
    // Setting a volume you cannot hear is guesswork, and on a panel showing one
    // number at a time the number is the only other feedback there would be.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    host.settings().audio.volumePercent = 40;
    platform.simulatedAudio().clear();

    holdKnob(host, platform, 1000);
    NOTRIX_REQUIRE(selectSetting(host, platform, notrix::input::SettingSlot::Volume, 2000));

    platform.simulatedInput().pressAndRelease(RawInput::KeyPlus, 4000, 50);
    host.tick(4100);

    NOTRIX_CHECK(!platform.simulatedAudio().requests().empty());
    NOTRIX_CHECK(platform.simulatedAudio().requests().front().isTone);
}

NOTRIX_TEST(Host, TurningVolumeDownToSilenceDoesNotBeep) {
    // A confirmation beep for "silence" is a contradiction, and zero is the one
    // setting where the absence of sound is itself the feedback.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);

    const int step = host.inputMapper().config().volumeStepPercent;
    host.settings().audio.volumePercent = static_cast<std::uint8_t>(step);

    holdKnob(host, platform, 1000);
    NOTRIX_REQUIRE(selectSetting(host, platform, notrix::input::SettingSlot::Volume, 2000));
    platform.simulatedAudio().clear();

    platform.simulatedInput().pressAndRelease(RawInput::KeyMinus, 4000, 50);
    host.tick(4100);

    NOTRIX_CHECK_EQ(static_cast<int>(host.settings().audio.volumePercent), 0);
    NOTRIX_CHECK(platform.simulatedAudio().requests().empty());
}

// --- sounds the device makes on its own behalf -------------------------------

NOTRIX_TEST(Host, ANotificationAnnouncesItselfOnce) {
    // Once, not once per frame. The sound marks an event arriving, and a
    // notification that holds the panel for five seconds is one event.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);
    platform.simulatedAudio().clear();

    notrix::notify::Notification alert;
    alert.id = "test";
    alert.text = "HELLO";
    host.notifications().push(alert, platform.simulatedClock().monotonicMillis());

    run(host, platform, 2000);

    NOTRIX_CHECK_EQ(static_cast<int>(platform.simulatedAudio().requests().size()), 1);
    NOTRIX_CHECK_EQ(platform.simulatedAudio().requests().front().sound, std::string("chime"));
}

NOTRIX_TEST(Host, ANotificationCanNameItsOwnSound) {
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 200);
    platform.simulatedAudio().clear();

    notrix::notify::Notification alert;
    alert.id = "test";
    alert.text = "UP";
    alert.sound = "alert";
    host.notifications().push(alert, platform.simulatedClock().monotonicMillis());

    run(host, platform, 2000);

    NOTRIX_REQUIRE(!platform.simulatedAudio().requests().empty());
    NOTRIX_CHECK_EQ(platform.simulatedAudio().requests().front().sound, std::string("alert"));
}

NOTRIX_TEST(Host, NotificationsCanBeSilent) {
    // "none" is a real choice, and the reason the setting is a string rather
    // than a bool: a clock in a bedroom should be able to say nothing.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    host.settings().notifications.sound = "none";
    run(host, platform, 200);
    platform.simulatedAudio().clear();

    notrix::notify::Notification alert;
    alert.id = "quiet";
    alert.text = "SHH";
    host.notifications().push(alert, platform.simulatedClock().monotonicMillis());
    run(host, platform, 2000);

    NOTRIX_CHECK(platform.simulatedAudio().requests().empty());
}

NOTRIX_TEST(Host, TheClockTicksOnlyWhenAskedTo) {
    // Off by default, and not out of timidity: a sound a device makes once a
    // second without being asked is the easiest way to make somebody unplug it.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    platform.simulatedClock().setWallClock(1'700'000'000);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 500);
    platform.simulatedAudio().clear();

    NOTRIX_CHECK_FALSE(host.settings().clock.tick);

    for (int i = 0; i < 5; ++i) {
        platform.simulatedClock().setWallClock(1'700'000'000 + i);
        run(host, platform, 1000 + static_cast<std::uint64_t>(i) * 200u);
    }
    NOTRIX_CHECK(platform.simulatedAudio().requests().empty());
}

NOTRIX_TEST(Host, WhenAskedTheClockAlternatesTickAndTock) {
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    platform.simulatedClock().setWallClock(1'700'000'000);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    host.settings().clock.tick = true;
    run(host, platform, 500);
    platform.simulatedAudio().clear();

    for (int i = 1; i <= 4; ++i) {
        platform.simulatedClock().setWallClock(1'700'000'000 + i);
        run(host, platform, 500 + static_cast<std::uint64_t>(i) * 200u);
    }

    const auto& played = platform.simulatedAudio().requests();
    NOTRIX_REQUIRE(played.size() >= 2);
    // Alternating, so a second sounds like a second rather than a repeated blip.
    for (std::size_t i = 1; i < played.size(); ++i) {
        NOTRIX_CHECK(played[i].sound != played[i - 1].sound);
    }
}

NOTRIX_TEST(Host, TheClockDoesNotTickOverANotification) {
    // Ticking under an alarm is being annoying for nobody's benefit.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.audio = true;
    SimulatorPlatform platform(capabilities);
    platform.simulatedClock().setWallClock(1'700'000'000);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    host.settings().clock.tick = true;
    host.settings().notifications.sound = "none";
    run(host, platform, 500);

    notrix::notify::Notification alert;
    alert.id = "hold";
    alert.text = "BUSY";
    alert.hold = true;
    host.notifications().push(alert, platform.simulatedClock().monotonicMillis());
    run(host, platform, 700);
    platform.simulatedAudio().clear();

    for (int i = 1; i <= 4; ++i) {
        platform.simulatedClock().setWallClock(1'700'000'000 + i);
        run(host, platform, 700 + static_cast<std::uint64_t>(i) * 200u);
    }
    NOTRIX_CHECK(platform.simulatedAudio().requests().empty());
}

// --- app order ---------------------------------------------------------------

NOTRIX_TEST(Host, AppOrderSurvivesARestart) {
    // Blueprint §12 says the app manager owns ordering and it must never be
    // inferred. An order the user arranged is therefore theirs only if it is
    // written down - otherwise every reboot silently overrules them.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    capabilities.microphone = true;
    SimulatorPlatform platform(capabilities);

    std::string reversedFirst;
    {
        ApplicationHost host(platform, quietConfig());
        host.initialize();
        run(host, platform, 200);
        NOTRIX_REQUIRE(host.apps().count() >= 3);

        // Move the last app to the front.
        const std::string last = host.apps().at(host.apps().count() - 1)->id;
        NOTRIX_REQUIRE(host.apps().move(last, 0));
        reversedFirst = last;

        run(host, platform, 1000);
        host.shutdown();
    }

    ApplicationHost restarted(platform, quietConfig());
    restarted.initialize();
    run(restarted, platform, 2000);

    NOTRIX_REQUIRE(restarted.apps().count() >= 3);
    NOTRIX_CHECK_EQ(restarted.apps().at(0)->id, reversedFirst);
}

NOTRIX_TEST(Host, ADisabledAppStaysDisabledAcrossARestart) {
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    SimulatorPlatform platform(capabilities);

    {
        ApplicationHost host(platform, quietConfig());
        host.initialize();
        run(host, platform, 200);
        NOTRIX_REQUIRE(host.apps().setEnabled(ApplicationHost::kBatteryAppId, false));
        run(host, platform, 1000);
        host.shutdown();
    }

    ApplicationHost restarted(platform, quietConfig());
    restarted.initialize();
    run(restarted, platform, 2000);

    const notrix::app::App* battery = restarted.apps().find(ApplicationHost::kBatteryAppId);
    NOTRIX_REQUIRE(battery != nullptr);
    NOTRIX_CHECK_FALSE(battery->enabled);
}

NOTRIX_TEST(Host, AStoredOrderNamingAnAppThatIsGoneStillBoots) {
    // Firmware changes and integrations stop pushing, so an order that names a
    // missing app is normal rather than exceptional. It must not cost the
    // arrangement of the apps that *are* there, and certainly must not stop
    // the device starting.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    SimulatorPlatform platform(capabilities);

    // Written through the store, because initialize() loads settings and
    // would overwrite anything set on the host beforehand - which is also
    // exactly how a real device meets a stored order.
    {
        notrix::config::Config stored;
        notrix::config::AppPreference ghost;
        ghost.id = "an-app-that-never-existed";
        stored.apps.order.push_back(ghost);

        notrix::config::AppPreference battery;
        battery.id = std::string(ApplicationHost::kBatteryAppId);
        stored.apps.order.push_back(battery);

        notrix::config::ConfigStore store(platform.storage());
        NOTRIX_REQUIRE(store.save(stored));
    }

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 500);

    NOTRIX_CHECK(host.healthy());
    // The real app named after the ghost still took the first position.
    NOTRIX_CHECK_EQ(host.apps().at(0)->id, std::string(ApplicationHost::kBatteryAppId));
}

NOTRIX_TEST(Host, AnAppInstalledSinceTheOrderWasSavedAppearsRatherThanVanishing) {
    // Apps not named by the stored order keep their natural position after the
    // ones that are. Dropping them, or sorting them to the front, would both
    // be the device overruling an arrangement it was only asked to restore.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    capabilities.microphone = true;
    SimulatorPlatform platform(capabilities);

    {
        notrix::config::Config stored;
        notrix::config::AppPreference battery;
        battery.id = std::string(ApplicationHost::kBatteryAppId);
        stored.apps.order.push_back(battery);

        notrix::config::ConfigStore store(platform.storage());
        NOTRIX_REQUIRE(store.save(stored));
    }

    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 500);

    NOTRIX_CHECK_EQ(host.apps().at(0)->id, std::string(ApplicationHost::kBatteryAppId));
    // Clock and visualiser were not mentioned, and are still installed.
    NOTRIX_CHECK(host.apps().find(ApplicationHost::kClockAppId) != nullptr);
    NOTRIX_CHECK(host.apps().find(ApplicationHost::kVisualizerAppId) != nullptr);
}

NOTRIX_TEST(Host, ChangingTheAppDurationTakesEffectWithoutARestart) {
    // It was copied into the carousel in initialize() and nowhere else, so
    // changing it over the API updated the stored setting and did nothing at
    // all until the next restart. From outside, a setting that only applies
    // after a reboot and does not say so is a setting that is ignored.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 500);
    NOTRIX_REQUIRE(host.apps().count() >= 2);

    host.settings().apps.defaultDurationSeconds = 1;
    run(host, platform, 700);

    const std::string before = host.carousel().active()->id;

    // Past one second and well short of two. With only two apps installed, a
    // wider window would advance twice and land back where it started - which
    // is a test that passes for "never moved" and for "moved correctly" alike.
    run(host, platform, 1800);
    NOTRIX_CHECK(host.carousel().active()->id != before);

    // And nowhere near the eight-second default it would have used before.
    NOTRIX_CHECK(host.carousel().config().defaultDurationSeconds == 1);
}

NOTRIX_TEST(Host, ALongerDurationAlsoTakesEffectImmediately) {
    // The other direction, because "it advances sooner" could be satisfied by
    // something ignoring the setting entirely and rotating fast.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 500);

    host.settings().apps.defaultDurationSeconds = 60;
    run(host, platform, 700);

    const std::string before = host.carousel().active()->id;
    run(host, platform, 20000);
    NOTRIX_CHECK_EQ(host.carousel().active()->id, before);
}

NOTRIX_TEST(Host, ATimezoneRuleBeatsTheStoredOffset) {
    // The bug this replaces: a fixed offset is right for about half the year
    // anywhere that observes daylight saving, and wrong the rest of it.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    host.settings().clock.utcOffsetSeconds = 0;
    host.settings().clock.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";

    // Deep winter: one hour ahead of UTC.
    platform.simulatedClock().setWallClock(
        notrix::timezone_::daysFromCivil(2026, 1, 15) * 86400);
    run(host, platform, 300);
    NOTRIX_CHECK_EQ(host.clockStyle().utcOffsetSeconds, 3600);

    // Deep summer: two.
    platform.simulatedClock().setWallClock(
        notrix::timezone_::daysFromCivil(2026, 7, 15) * 86400);
    run(host, platform, 600);
    NOTRIX_CHECK_EQ(host.clockStyle().utcOffsetSeconds, 7200);
}

NOTRIX_TEST(Host, WithoutATimezoneTheStoredOffsetStillApplies) {
    // Every device configured before timezones existed has one of these, and
    // nothing should have to be re-entered to keep working.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    host.settings().clock.timezone.clear();
    host.settings().clock.utcOffsetSeconds = 5 * 3600;
    platform.simulatedClock().setWallClock(
        notrix::timezone_::daysFromCivil(2026, 7, 15) * 86400);
    run(host, platform, 300);

    NOTRIX_CHECK_EQ(host.clockStyle().utcOffsetSeconds, 5 * 3600);
}

NOTRIX_TEST(Host, AnUnparseableRuleFallsBackAndSaysSo) {
    // Falling back silently would leave somebody certain they had set a
    // timezone and puzzled twice a year.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    host.settings().clock.utcOffsetSeconds = 3 * 3600;
    host.settings().clock.timezone = "not a timezone";
    run(host, platform, 300);

    NOTRIX_CHECK_EQ(host.clockStyle().utcOffsetSeconds, 3 * 3600);
    NOTRIX_CHECK(logContains(host, "timezone rule not understood"));
}

NOTRIX_TEST(Host, WithoutAWallClockTheStandardOffsetIsUsed) {
    // Before NTP answers there is no date, so there is no way to know which
    // side of a changeover we are on. Standard time is the honest answer; a
    // coin toss dressed as a summer offset is not.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();

    host.settings().clock.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    run(host, platform, 300);  // wall clock never set

    NOTRIX_CHECK_FALSE(platform.clock().wallClockValid());
    NOTRIX_CHECK_EQ(host.clockStyle().utcOffsetSeconds, 3600);
}

NOTRIX_TEST(Host, ARestoredOrderReachesTheRegistryWithoutARestart) {
    // Restoring a backup writes settings; the apps on screen are in the
    // registry. Without this the arrangement would come back only on the next
    // reboot - the same defect the app duration had, in a place where it is
    // even less visible.
    notrix::platform::simulator::SimulatorCapabilities capabilities;
    capabilities.power = true;
    capabilities.microphone = true;
    SimulatorPlatform platform(capabilities);
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 500);
    NOTRIX_REQUIRE(host.apps().count() >= 3);

    // An arrangement arriving from outside, exactly as a restore delivers it.
    const std::string wanted = host.apps().at(host.apps().count() - 1)->id;
    notrix::config::AppPreference first;
    first.id = wanted;
    host.settings().apps.order.clear();
    host.settings().apps.order.push_back(first);

    run(host, platform, 1000);

    NOTRIX_CHECK_EQ(host.apps().at(0)->id, wanted);
}

NOTRIX_TEST(Host, AnOrderThatAlreadyMatchesIsNotRewritten) {
    // The two directions must not fight. If applying an order counted as a
    // registry change, and writing it back counted as a settings change, the
    // device would save its configuration on every single tick.
    SimulatorPlatform platform;
    ApplicationHost host(platform, quietConfig());
    host.initialize();
    run(host, platform, 1000);

    const int before = host.logger().count();
    run(host, platform, 6000);

    // No errors, and nothing churning: a save failure would log, and a loop
    // would show up as a stream of them.
    NOTRIX_CHECK_EQ(host.logger().count(), before);
}
