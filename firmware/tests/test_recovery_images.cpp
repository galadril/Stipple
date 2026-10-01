// SPDX-License-Identifier: GPL-3.0-or-later
//
// `/api/v1/system/recovery` — which firmware the device's own recovery button
// would install.
//
// The behaviour under test is not "can a file be renamed". It is that the
// device answers honestly about a control somebody reaches for when nothing
// else works. This project's author held that button to return to stock and
// got a progress bar, a green tick and Stipple again — because the button
// installs whatever image is waiting, and the waiting one was Stipple. ADR
// 0026.
//
// So the cases worth testing are the ones where the honest answer is awkward:
// nothing armed at all, an image somebody else put there, and an arm that
// cannot be done. A route reporting a cheerful default for any of those would
// be worse than no route, because it would be believed.
#include "stipple/api/ApiServer.h"

#include <string>

#include "stipple/json/Json.h"
#include "stipple/platform/simulator/SimulatorPlatform.h"
#include "support/TestFramework.h"

using stipple::api::ApiContext;
using stipple::api::ApiServer;
using stipple::api::Request;
using stipple::api::Response;
using stipple::platform::IRecoveryImages;
using stipple::platform::simulator::SimulatorPlatform;

namespace {

using Image = IRecoveryImages::Image;

/// The two slots, in memory.
///
/// Models the adapter's *observable* behaviour rather than its renames: which
/// image is armed, which waits, and that a switch is a swap between exactly
/// two — because a third copy does not fit on an 8.4 MB volume.
class FakeImages final : public IRecoveryImages {
public:
    Image armedImage = Image::Stipple;
    Image spareImage = Image::Stock;
    bool armFails = false;

    Image armed() const override { return armedImage; }

    bool available(Image which) const override {
        if (which != Image::Stock && which != Image::Stipple) {
            return false;
        }
        return armedImage == which || spareImage == which;
    }

    bool arm(Image which, std::string& problem) override {
        problem.clear();
        if (which != Image::Stock && which != Image::Stipple) {
            problem = "that is not an image this device can arm";
            return false;
        }
        if (armedImage == which) {
            return true;  // Idempotent.
        }
        if (spareImage != which) {
            problem = "the other image is not on this device";
            return false;
        }
        if (armFails) {
            problem = "could not arm the image";
            return false;
        }
        const Image wasArmed = armedImage;
        armedImage = which;
        spareImage = wasArmed;
        return true;
    }

    std::size_t freeBytes() const override { return 2707968; }
};

class PlatformWithImages final : public SimulatorPlatform {
public:
    IRecoveryImages* recoveryImages() override { return &images_; }
    FakeImages images_;
};

struct Fixture {
    PlatformWithImages platform;
    ApiServer server;

    Fixture() : server(makeContext()) {}

    ApiContext makeContext() {
        ApiContext context;
        context.platform = &platform;
        return context;
    }

    Response call(const char* method, std::string body = std::string()) {
        Request request;
        request.method = stipple::api::methodFromName(method);
        request.path = "/api/v1/system/recovery";
        request.body = std::move(body);
        return server.handle(request, 0);
    }
};

/// Keeps the text alive alongside the document, because a Value points into
/// it. Same shape as test_firmware_update.cpp.
struct Parsed {
    stipple::json::Token tokens[256];
    std::string text;
    stipple::json::Document document{tokens, 256};
    bool ok = false;

    explicit Parsed(std::string body) : text(std::move(body)) {
        ok = document.parse(text) == stipple::json::Error::None;
    }
    stipple::json::Value root() const { return document.root(); }
};

}  // namespace

STIPPLE_TEST(RecoveryImages, APlatformWithoutOneSaysSoRatherThanReportingNothingArmed) {
    // The simulator has no vendor recovery path. "Nothing is armed" would be a
    // different and wrong claim - it reads as a device whose button has been
    // disarmed. ADR 0013: absence is reported, not defaulted.
    SimulatorPlatform bare;
    ApiContext context;
    context.platform = &bare;
    ApiServer server(context);

    Request request;
    request.method = stipple::api::Method::Get;
    request.path = "/api/v1/system/recovery";
    const Response response = server.handle(request, 0);

    STIPPLE_CHECK_EQ(static_cast<int>(response.status), 501);
}

STIPPLE_TEST(RecoveryImages, ItSaysWhatTheButtonWouldDoInWordsNotFilenames) {
    Fixture fixture;
    Parsed parsed(fixture.call("GET").body);
    STIPPLE_REQUIRE(parsed.ok);

    STIPPLE_CHECK_EQ(parsed.root()["armed"].toString(), std::string("stipple"));
    // The point of the route. "armed: stipple" is the filename-level answer,
    // and nobody holds a recovery button thinking in filenames.
    STIPPLE_CHECK_EQ(parsed.root()["means"].toString(), std::string("reinstalls Stipple"));
}

STIPPLE_TEST(RecoveryImages, ArmingStockChangesWhatTheButtonMeans) {
    Fixture fixture;
    const Response response = fixture.call("POST", "{\"arm\":\"stock\"}");
    STIPPLE_CHECK_EQ(static_cast<int>(response.status), 200);

    Parsed parsed(response.body);
    STIPPLE_REQUIRE(parsed.ok);
    STIPPLE_CHECK_EQ(parsed.root()["armed"].toString(), std::string("stock"));
    STIPPLE_CHECK_EQ(parsed.root()["means"].toString(),
                     std::string("returns this device to the stock Ulanzi clock"));

    // A swap, not an overwrite: the spare now holds what was armed, because
    // only two images fit.
    STIPPLE_CHECK(fixture.platform.images_.armedImage == Image::Stock);
    STIPPLE_CHECK(fixture.platform.images_.spareImage == Image::Stipple);
}

STIPPLE_TEST(RecoveryImages, ArmingWhatIsAlreadyArmedSucceedsAndMovesNothing) {
    // Idempotent on purpose: a page that re-arms on every save must not
    // shuffle three megabytes of flash each time, and must not fail either.
    Fixture fixture;
    STIPPLE_CHECK_EQ(static_cast<int>(fixture.call("POST", "{\"arm\":\"stipple\"}").status), 200);
    STIPPLE_CHECK(fixture.platform.images_.armedImage == Image::Stipple);
    STIPPLE_CHECK(fixture.platform.images_.spareImage == Image::Stock);
}

STIPPLE_TEST(RecoveryImages, AnImageTheDeviceDoesNotHaveIsRefusedNotInvented) {
    Fixture fixture;
    fixture.platform.images_.spareImage = Image::Nothing;

    STIPPLE_CHECK_EQ(static_cast<int>(fixture.call("POST", "{\"arm\":\"stock\"}").status), 422);
    // Still armed with what it was. A failed arm must never disarm the button,
    // because the button is somebody's way back.
    STIPPLE_CHECK(fixture.platform.images_.armedImage == Image::Stipple);
}

STIPPLE_TEST(RecoveryImages, AvailabilityIsReportedPerImage) {
    Fixture fixture;
    fixture.platform.images_.spareImage = Image::Nothing;

    Parsed parsed(fixture.call("GET").body);
    STIPPLE_REQUIRE(parsed.ok);

    STIPPLE_CHECK(parsed.root()["available"]["stipple"].toBool(false));
    // Offering "switch to stock" on a device with no stock image would be
    // offering a button that cannot work.
    STIPPLE_CHECK_FALSE(parsed.root()["available"]["stock"].toBool(true));
}

STIPPLE_TEST(RecoveryImages, NothingArmedIsReportedAsSuchRatherThanAsStock) {
    // The worst state to get wrong: the button does nothing and somebody is
    // relying on it. Silence here would be a promise.
    Fixture fixture;
    fixture.platform.images_.armedImage = Image::Nothing;

    Parsed parsed(fixture.call("GET").body);
    STIPPLE_REQUIRE(parsed.ok);

    STIPPLE_CHECK_EQ(parsed.root()["armed"].toString(), std::string("nothing"));
    STIPPLE_CHECK_EQ(parsed.root()["means"].toString(),
                     std::string("nothing - no image is waiting"));
}

STIPPLE_TEST(RecoveryImages, AnImageSomebodyElsePutThereIsNotGuessedAt) {
    // A USB stick is the documented way to install anything on this device,
    // and it knows nothing about our bookkeeping. Claiming to know what it
    // left would be the confident lie ADR 0013 exists to prevent.
    Fixture fixture;
    fixture.platform.images_.armedImage = Image::Unrecognised;

    Parsed parsed(fixture.call("GET").body);
    STIPPLE_REQUIRE(parsed.ok);

    STIPPLE_CHECK_EQ(parsed.root()["armed"].toString(), std::string("unrecognised"));
    STIPPLE_CHECK_EQ(parsed.root()["means"].toString(),
                     std::string("installs an image this device did not put there"));
}

STIPPLE_TEST(RecoveryImages, AFailedArmReportsWhyAndLeavesTheButtonArmed) {
    Fixture fixture;
    fixture.platform.images_.armFails = true;

    STIPPLE_CHECK_EQ(static_cast<int>(fixture.call("POST", "{\"arm\":\"stock\"}").status), 422);
    STIPPLE_CHECK(fixture.platform.images_.armedImage == Image::Stipple);
}

STIPPLE_TEST(RecoveryImages, TheRequestHasToNameAnImage) {
    Fixture fixture;
    STIPPLE_CHECK_EQ(static_cast<int>(fixture.call("POST", "{}").status), 400);
    STIPPLE_CHECK_EQ(static_cast<int>(fixture.call("POST", "{\"arm\":\"something\"}").status), 422);
    STIPPLE_CHECK_EQ(static_cast<int>(fixture.call("POST", "not json").status), 400);
}

STIPPLE_TEST(RecoveryImages, OtherMethodsAreRefused) {
    Fixture fixture;
    STIPPLE_CHECK_EQ(static_cast<int>(fixture.call("DELETE").status), 405);
}
