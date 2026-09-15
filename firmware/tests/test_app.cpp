// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/app/Carousel.h"

#include <string>

#include "support/TestFramework.h"

using notrix::app::App;
using notrix::app::AppRegistry;
using notrix::app::AppSource;
using notrix::app::Carousel;
using notrix::app::CarouselConfig;

namespace {

App makeApp(std::string id, int durationSeconds = 0, bool enabled = true) {
    App app;
    app.id = std::move(id);
    app.name = app.id;
    app.sceneJson = R"({"elements":[]})";
    app.durationSeconds = durationSeconds;
    app.enabled = enabled;
    return app;
}

App makeTemporary(std::string id, std::uint64_t expiresAtMillis) {
    App app = makeApp(std::move(id));
    app.source = AppSource::Temporary;
    app.expiresAtMillis = expiresAtMillis;
    return app;
}

/// Ids in display order, as one string — the clearest way to assert ordering.
std::string order(const AppRegistry& registry) {
    std::string result;
    for (int i = 0; i < registry.count(); ++i) {
        if (i > 0) {
            result += ',';
        }
        result += registry.at(i)->id;
    }
    return result;
}

std::string activeOf(const Carousel& carousel) {
    return std::string(carousel.activeId());
}

}  // namespace

// --- registry ----------------------------------------------------------------

NOTRIX_TEST(AppRegistry, PreservesInsertionOrder) {
    // Blueprint §12: ordering is owned explicitly, never inferred from container
    // iteration. Ids chosen so that hash or alphabetical order would differ.
    AppRegistry registry;
    registry.put(makeApp("zulu"));
    registry.put(makeApp("alpha"));
    registry.put(makeApp("mike"));

    NOTRIX_CHECK_EQ(order(registry), std::string("zulu,alpha,mike"));
}

NOTRIX_TEST(AppRegistry, ReplacingAnAppKeepsItsPosition) {
    // An integration refreshing its app must not shuffle the carousel.
    AppRegistry registry;
    registry.put(makeApp("a"));
    registry.put(makeApp("b"));
    registry.put(makeApp("c"));

    App updated = makeApp("b");
    updated.name = "renamed";
    NOTRIX_CHECK(registry.put(updated) == AppRegistry::PutResult::Replaced);

    NOTRIX_CHECK_EQ(order(registry), std::string("a,b,c"));
    NOTRIX_CHECK_EQ(registry.find("b")->name, std::string("renamed"));
}

NOTRIX_TEST(AppRegistry, RejectsInvalidAndOversizedApps) {
    AppRegistry registry;
    NOTRIX_CHECK(registry.put(makeApp("")) == AppRegistry::PutResult::InvalidId);

    App huge = makeApp("big");
    huge.sceneJson = std::string(AppRegistry::kMaxSceneBytes + 1, 'x');
    NOTRIX_CHECK(registry.put(huge) == AppRegistry::PutResult::SceneTooLarge);

    App longId = makeApp(std::string(AppRegistry::kMaxIdBytes + 1, 'i'));
    NOTRIX_CHECK(registry.put(longId) == AppRegistry::PutResult::InvalidId);
}

NOTRIX_TEST(AppRegistry, IsBounded) {
    AppRegistry registry;
    for (int i = 0; i < AppRegistry::kMaxApps; ++i) {
        NOTRIX_CHECK(registry.put(makeApp("app" + std::to_string(i))) ==
                     AppRegistry::PutResult::Added);
    }
    NOTRIX_CHECK(registry.put(makeApp("overflow")) == AppRegistry::PutResult::Full);
    NOTRIX_CHECK_EQ(registry.count(), AppRegistry::kMaxApps);
}

NOTRIX_TEST(AppRegistry, MoveReorders) {
    AppRegistry registry;
    registry.put(makeApp("a"));
    registry.put(makeApp("b"));
    registry.put(makeApp("c"));

    NOTRIX_CHECK(registry.move("c", 0));
    NOTRIX_CHECK_EQ(order(registry), std::string("c,a,b"));

    NOTRIX_CHECK(registry.move("c", 2));
    NOTRIX_CHECK_EQ(order(registry), std::string("a,b,c"));

    NOTRIX_CHECK_FALSE(registry.move("c", 99));
    NOTRIX_CHECK_FALSE(registry.move("missing", 0));
}

NOTRIX_TEST(AppRegistry, ClearKeepsSystemApps) {
    // Something must still be on screen after a bad API call wipes user apps.
    AppRegistry registry;
    App clock = makeApp("clock");
    clock.source = AppSource::System;
    registry.put(clock);
    registry.put(makeApp("weather"));

    registry.clear();
    NOTRIX_CHECK_EQ(order(registry), std::string("clock"));
}

NOTRIX_TEST(AppRegistry, ExpiresOnlyTemporaryApps) {
    AppRegistry registry;
    registry.put(makeApp("permanent"));
    registry.put(makeTemporary("flash", 5000));

    NOTRIX_CHECK_EQ(registry.expire(4999), 0);
    NOTRIX_CHECK_EQ(registry.expire(5000), 1);
    NOTRIX_CHECK_EQ(order(registry), std::string("permanent"));
}

NOTRIX_TEST(AppRegistry, RevisionTracksMutations) {
    // Anything caching a view into an app's scene JSON relies on this.
    AppRegistry registry;
    const std::uint32_t start = registry.revision();

    registry.put(makeApp("a"));
    NOTRIX_CHECK(registry.revision() != start);

    const std::uint32_t afterPut = registry.revision();
    registry.setEnabled("a", false);
    NOTRIX_CHECK(registry.revision() != afterPut);

    const std::uint32_t afterToggle = registry.revision();
    registry.setEnabled("a", false);  // no actual change
    NOTRIX_CHECK_EQ(registry.revision(), afterToggle);
}

NOTRIX_TEST(AppRegistry, EnabledNavigationWraps) {
    AppRegistry registry;
    registry.put(makeApp("a"));
    registry.put(makeApp("b", 0, false));
    registry.put(makeApp("c"));

    NOTRIX_CHECK_EQ(registry.enabledCount(), 2);
    NOTRIX_CHECK_EQ(registry.nextEnabled(0), 2);  // skips disabled b
    NOTRIX_CHECK_EQ(registry.nextEnabled(2), 0);  // wraps
    NOTRIX_CHECK_EQ(registry.previousEnabled(0), 2);
}

NOTRIX_TEST(AppRegistry, NavigationWithNothingEnabledReturnsNothing) {
    AppRegistry registry;
    registry.put(makeApp("a", 0, false));

    NOTRIX_CHECK_EQ(registry.firstEnabled(), -1);
    NOTRIX_CHECK_EQ(registry.nextEnabled(0), -1);
    NOTRIX_CHECK_EQ(registry.previousEnabled(0), -1);
}

NOTRIX_TEST(AppRegistry, SingleEnabledAppNavigatesToItself) {
    AppRegistry registry;
    registry.put(makeApp("only"));
    NOTRIX_CHECK_EQ(registry.nextEnabled(0), 0);
    NOTRIX_CHECK_EQ(registry.previousEnabled(0), 0);
}

// --- carousel ----------------------------------------------------------------

NOTRIX_TEST(Carousel, ActivatesTheFirstEnabledAppOnFirstTick) {
    AppRegistry registry;
    registry.put(makeApp("a", 0, false));
    registry.put(makeApp("b"));

    Carousel carousel(registry);
    NOTRIX_CHECK(carousel.active() == nullptr);

    carousel.tick(0);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));
}

NOTRIX_TEST(Carousel, AdvancesAfterTheAppDuration) {
    AppRegistry registry;
    registry.put(makeApp("a", 5));
    registry.put(makeApp("b", 5));

    Carousel carousel(registry);
    carousel.tick(0);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));

    carousel.tick(4999);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));

    NOTRIX_CHECK(carousel.tick(5000));
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));
}

NOTRIX_TEST(Carousel, PerAppDurationBeatsTheDefault) {
    AppRegistry registry;
    registry.put(makeApp("quick", 2));
    registry.put(makeApp("slow", 0));  // uses the default

    CarouselConfig config;
    config.defaultDurationSeconds = 10;
    Carousel carousel(registry, config);

    carousel.tick(0);
    NOTRIX_CHECK_EQ(carousel.activeDurationSeconds(), 2);

    carousel.tick(2000);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("slow"));
    NOTRIX_CHECK_EQ(carousel.activeDurationSeconds(), 10);
}

NOTRIX_TEST(Carousel, RotationWrapsAndSkipsDisabled) {
    AppRegistry registry;
    registry.put(makeApp("a", 1));
    registry.put(makeApp("b", 1, false));
    registry.put(makeApp("c", 1));

    Carousel carousel(registry);
    carousel.tick(0);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));

    carousel.tick(1000);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("c"));  // b skipped

    carousel.tick(2000);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));  // wrapped
}

NOTRIX_TEST(Carousel, PauseFreezesAutomaticRotation) {
    AppRegistry registry;
    registry.put(makeApp("a", 1));
    registry.put(makeApp("b", 1));

    Carousel carousel(registry);
    carousel.tick(0);
    carousel.setPaused(true);

    carousel.tick(60000);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));

    carousel.setPaused(false);
    carousel.tick(60001);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));
}

NOTRIX_TEST(Carousel, ManualRotationWorksWhilePaused) {
    // A button press is explicit user intent and must not be swallowed.
    AppRegistry registry;
    registry.put(makeApp("a"));
    registry.put(makeApp("b"));

    Carousel carousel(registry);
    carousel.tick(0);
    carousel.setPaused(true);

    NOTRIX_CHECK(carousel.next(100));
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));

    NOTRIX_CHECK(carousel.previous(200));
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));
}

NOTRIX_TEST(Carousel, ManualRotationResetsTheDwellTimer) {
    AppRegistry registry;
    registry.put(makeApp("a", 5));
    registry.put(makeApp("b", 5));

    Carousel carousel(registry);
    carousel.tick(0);
    carousel.next(4000);  // b becomes active at t=4000

    carousel.tick(8000);  // only 4s into b
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));

    carousel.tick(9000);  // now 5s
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));
}

NOTRIX_TEST(Carousel, PinHoldsOneApp) {
    AppRegistry registry;
    registry.put(makeApp("a", 1));
    registry.put(makeApp("b", 1));

    Carousel carousel(registry);
    carousel.tick(0);
    NOTRIX_CHECK(carousel.pin("b", 0));
    NOTRIX_CHECK(carousel.isPinned());

    carousel.tick(60000);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));

    carousel.unpin();
    carousel.tick(120000);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));
}

NOTRIX_TEST(Carousel, ManualRotationClearsThePin) {
    AppRegistry registry;
    registry.put(makeApp("a"));
    registry.put(makeApp("b"));

    Carousel carousel(registry);
    carousel.tick(0);
    carousel.pin("a", 0);

    carousel.next(100);
    NOTRIX_CHECK_FALSE(carousel.isPinned());
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));
}

NOTRIX_TEST(Carousel, PinningAnUnknownOrDisabledAppFails) {
    AppRegistry registry;
    registry.put(makeApp("a"));
    registry.put(makeApp("off", 0, false));

    Carousel carousel(registry);
    NOTRIX_CHECK_FALSE(carousel.pin("nope", 0));
    NOTRIX_CHECK_FALSE(carousel.pin("off", 0));
    NOTRIX_CHECK_FALSE(carousel.isPinned());
}

NOTRIX_TEST(Carousel, DeletingThePinnedAppResumesRotation) {
    // Freezing on a screen that no longer exists would look like a crash.
    AppRegistry registry;
    registry.put(makeApp("a", 1));
    registry.put(makeApp("b", 1));

    Carousel carousel(registry);
    carousel.tick(0);
    carousel.pin("b", 0);

    registry.remove("b");
    carousel.tick(1000);

    NOTRIX_CHECK_FALSE(carousel.isPinned());
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));
}

NOTRIX_TEST(Carousel, DeletingTheActiveAppAdvancesToItsNeighbour) {
    AppRegistry registry;
    registry.put(makeApp("a", 100));
    registry.put(makeApp("b", 100));
    registry.put(makeApp("c", 100));

    Carousel carousel(registry);
    carousel.tick(0);
    carousel.next(0);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));

    registry.remove("b");
    carousel.tick(1);

    // Resumes where b sat rather than restarting at the beginning.
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("c"));
}

NOTRIX_TEST(Carousel, DisablingTheActiveAppMovesOn) {
    AppRegistry registry;
    registry.put(makeApp("a", 100));
    registry.put(makeApp("b", 100));

    Carousel carousel(registry);
    carousel.tick(0);
    registry.setEnabled("a", false);
    carousel.tick(1);

    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));
}

NOTRIX_TEST(Carousel, EmptyRegistryHasNoActiveApp) {
    AppRegistry registry;
    Carousel carousel(registry);

    carousel.tick(0);
    carousel.tick(10000);
    NOTRIX_CHECK(carousel.active() == nullptr);
    NOTRIX_CHECK(carousel.activeId().empty());
}

NOTRIX_TEST(Carousel, AllDisabledHasNoActiveApp) {
    AppRegistry registry;
    registry.put(makeApp("a", 0, false));
    registry.put(makeApp("b", 0, false));

    Carousel carousel(registry);
    carousel.tick(0);
    NOTRIX_CHECK(carousel.active() == nullptr);
}

NOTRIX_TEST(Carousel, SingleAppStaysActive) {
    AppRegistry registry;
    registry.put(makeApp("only", 1));

    Carousel carousel(registry);
    carousel.tick(0);
    carousel.tick(5000);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("only"));
}

NOTRIX_TEST(Carousel, ExpiredTemporaryAppsDisappearOnTick) {
    AppRegistry registry;
    registry.put(makeApp("home", 100));
    registry.put(makeTemporary("alert", 3000));

    Carousel carousel(registry);
    carousel.tick(0);
    carousel.next(0);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("alert"));

    carousel.tick(3000);
    NOTRIX_CHECK_EQ(registry.count(), 1);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("home"));
}

NOTRIX_TEST(Carousel, BackwardsClockDoesNotSkipAhead) {
    AppRegistry registry;
    registry.put(makeApp("a", 5));
    registry.put(makeApp("b", 5));

    Carousel carousel(registry);
    carousel.tick(10000);
    NOTRIX_CHECK_EQ(carousel.dwellMillis(5000), std::uint64_t(0));

    carousel.tick(5000);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));
}

NOTRIX_TEST(Carousel, ResetClearsPositionPinAndDwellTimer) {
    // The dwell timer is an absolute timestamp. Resetting without clearing it
    // leaves the carousel comparing new times against an old start point, and it
    // can sit frozen until the clock catches up.
    AppRegistry registry;
    registry.put(makeApp("a", 1));
    registry.put(makeApp("b", 1));

    Carousel carousel(registry);
    carousel.tick(0);
    carousel.next(50000);
    carousel.pin("b", 50000);
    carousel.setPaused(true);

    carousel.reset(0);

    NOTRIX_CHECK(carousel.active() == nullptr);
    NOTRIX_CHECK_FALSE(carousel.isPinned());
    NOTRIX_CHECK_FALSE(carousel.paused());

    // Rotation works again from a fresh clock rather than being stuck.
    carousel.tick(0);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));
    carousel.tick(1000);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("b"));
}

NOTRIX_TEST(Carousel, ZeroDefaultDurationDoesNotSpin) {
    // A misconfigured zero must not advance the carousel every frame.
    AppRegistry registry;
    registry.put(makeApp("a"));
    registry.put(makeApp("b"));

    CarouselConfig config;
    config.defaultDurationSeconds = 0;
    Carousel carousel(registry, config);

    carousel.tick(0);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));
    carousel.tick(500);
    NOTRIX_CHECK_EQ(activeOf(carousel), std::string("a"));
}
