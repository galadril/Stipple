// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/asset/IconStore.h"

#include <cstring>
#include <string>

#include "stipple/graphics/Canvas.h"
#include "stipple/json/Json.h"
#include "stipple/scene/Scene.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::Rgb;
using stipple::asset::Icon;
using stipple::asset::IconStore;
using stipple::json::Token;
using stipple::scene::ElementType;
using stipple::scene::isImplemented;
using stipple::scene::Scene;
namespace colors = stipple::colors;

namespace {

/// A solid icon of one colour, for tests that care about placement rather than
/// artwork.
Icon solid(std::string id, int size, Rgb color, int frames = 1) {
    Icon icon;
    icon.id = std::move(id);
    icon.width = size;
    icon.height = size;
    icon.frameCount = frames;
    icon.pixels.assign(static_cast<std::size_t>(size * size * frames), color);
    return icon;
}

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

/// Scene plus its backing storage, since a Scene holds views into both.
struct Loaded {
    Token storage[512];
    std::string text;
    Scene scene{storage, 512};
    bool ok = false;

    Loaded(std::string json, const IconStore* icons) : text(std::move(json)) {
        scene.setIconStore(icons);
        ok = scene.load(text);
    }

    Framebuffer render(std::uint64_t elapsedMillis = 0) {
        Framebuffer framebuffer;
        Canvas canvas(framebuffer);
        scene.render(canvas, elapsedMillis);
        return framebuffer;
    }
};

}  // namespace

// --- the store ---------------------------------------------------------------

STIPPLE_TEST(Icons, StoresAndFindsByName) {
    IconStore store;
    STIPPLE_CHECK(store.put(solid("thermometer", 8, colors::kRed)) == IconStore::PutResult::Added);

    STIPPLE_CHECK_EQ(store.count(), 1);
    STIPPLE_CHECK(store.find("thermometer") != nullptr);
    STIPPLE_CHECK(store.find("missing") == nullptr);
    STIPPLE_CHECK_EQ(store.bytesUsed(), std::size_t(8 * 8 * 3));
}

STIPPLE_TEST(Icons, ReplacingReclaimsTheOldBudget) {
    // Updating an icon in place must not fail against a budget it already fits
    // inside.
    IconStore store;
    store.put(solid("a", 16, colors::kRed));
    const std::size_t afterFirst = store.bytesUsed();

    STIPPLE_CHECK(store.put(solid("a", 16, colors::kBlue)) == IconStore::PutResult::Replaced);
    STIPPLE_CHECK_EQ(store.bytesUsed(), afterFirst);
    STIPPLE_CHECK_EQ(store.count(), 1);
}

STIPPLE_TEST(Icons, RejectsMismatchedGeometry) {
    // Trusting a declared width against a shorter buffer is how a blit reads
    // past the end.
    IconStore store;

    Icon lying = solid("bad", 8, colors::kRed);
    lying.width = 16;  // claims 16x8 but holds 8x8 pixels
    STIPPLE_CHECK(store.put(std::move(lying)) == IconStore::PutResult::InvalidGeometry);
    STIPPLE_CHECK_EQ(store.count(), 0);
}

STIPPLE_TEST(Icons, RejectsAbsurdDimensions) {
    IconStore store;

    Icon huge;
    huge.id = "huge";
    huge.width = IconStore::kMaxDimension + 1;
    huge.height = 8;
    huge.frameCount = 1;
    huge.pixels.assign(static_cast<std::size_t>(huge.width * huge.height), colors::kRed);
    STIPPLE_CHECK(store.put(std::move(huge)) == IconStore::PutResult::InvalidGeometry);

    STIPPLE_CHECK(store.put(solid("", 8, colors::kRed)) == IconStore::PutResult::InvalidId);
    STIPPLE_CHECK(store.put(solid(std::string(IconStore::kMaxIdBytes + 1, 'x'), 8, colors::kRed)) ==
                 IconStore::PutResult::InvalidId);
}

STIPPLE_TEST(Icons, EnforcesTheTotalByteBudget) {
    // The limit is bytes, not icons: sixty-four static glyphs and eight
    // animations cost the same RAM and must be governed by the same number.
    IconStore store;

    int stored = 0;
    while (store.put(solid("i" + std::to_string(stored), 16, colors::kRed)) ==
           IconStore::PutResult::Added) {
        ++stored;
        if (stored > 100) {
            break;  // guard against a budget that never fills
        }
    }

    STIPPLE_CHECK(stored > 0);
    STIPPLE_CHECK(store.bytesUsed() <= IconStore::kMaxTotalBytes);
    STIPPLE_CHECK(store.put(solid("overflow", 16, colors::kRed)) ==
                 IconStore::PutResult::BudgetExceeded);
}

STIPPLE_TEST(Icons, RemoveFreesBudget) {
    IconStore store;
    store.put(solid("a", 16, colors::kRed));
    const std::size_t used = store.bytesUsed();
    STIPPLE_CHECK(used > 0);

    STIPPLE_CHECK(store.remove("a"));
    STIPPLE_CHECK_EQ(store.bytesUsed(), std::size_t(0));
    STIPPLE_CHECK_FALSE(store.remove("a"));
}

STIPPLE_TEST(Icons, AnimationPicksFramesByTime) {
    IconStore store;
    Icon animated = solid("spin", 4, colors::kRed, 4);
    animated.frameMillis = 100;
    store.put(std::move(animated));

    const Icon* icon = store.find("spin");
    STIPPLE_CHECK(icon->animated());
    STIPPLE_CHECK_EQ(IconStore::frameAt(*icon, 0), 0);
    STIPPLE_CHECK_EQ(IconStore::frameAt(*icon, 150), 1);
    STIPPLE_CHECK_EQ(IconStore::frameAt(*icon, 350), 3);
    STIPPLE_CHECK_EQ(IconStore::frameAt(*icon, 400), 0);  // wraps
}

STIPPLE_TEST(Icons, ZeroFrameDurationDoesNotDivideByZero) {
    IconStore store;
    Icon icon = solid("x", 4, colors::kRed, 3);
    icon.frameMillis = 0;
    store.put(std::move(icon));

    STIPPLE_CHECK(store.find("x")->frameMillis > 0);
    STIPPLE_CHECK_EQ(IconStore::frameAt(*store.find("x"), 999999), 0);
}

STIPPLE_TEST(Icons, FrameViewsAreBoundsChecked) {
    IconStore store;
    store.put(solid("a", 4, colors::kRed, 2));
    const Icon* icon = store.find("a");

    STIPPLE_CHECK(IconStore::frameView(*icon, 0).valid());
    STIPPLE_CHECK(IconStore::frameView(*icon, 1).valid());
    STIPPLE_CHECK_FALSE(IconStore::frameView(*icon, 2).valid());
    STIPPLE_CHECK_FALSE(IconStore::frameView(*icon, -1).valid());
}

STIPPLE_TEST(Icons, RevisionTracksMutations) {
    IconStore store;
    const std::uint32_t start = store.revision();
    store.put(solid("a", 4, colors::kRed));
    STIPPLE_CHECK(store.revision() != start);
}

// --- scene integration -------------------------------------------------------

STIPPLE_TEST(Icons, IconAndBitmapAreNowImplemented) {
    STIPPLE_CHECK(isImplemented(ElementType::Icon));
    STIPPLE_CHECK(isImplemented(ElementType::Bitmap));
    // Still honestly reported as absent.
    STIPPLE_CHECK_FALSE(isImplemented(ElementType::Sprite));
    STIPPLE_CHECK_FALSE(isImplemented(ElementType::Animation));
}

STIPPLE_TEST(Icons, SceneDrawsAStoredIcon) {
    IconStore store;
    store.put(solid("dot", 4, colors::kGreen));

    Loaded scene(R"({"elements":[{"type":"icon","x":2,"y":3,"icon":"dot"}]})", &store);
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 0);

    const Framebuffer frame = scene.render();
    STIPPLE_CHECK_EQ(countLit(frame), 16);
    STIPPLE_CHECK_EQ(frame.at(2, 3), colors::kGreen);
    STIPPLE_CHECK_EQ(frame.at(5, 6), colors::kGreen);
    STIPPLE_CHECK_EQ(frame.at(6, 7), colors::kBlack);
}

STIPPLE_TEST(Icons, MissingIconIsReportedNotSilentlySkipped) {
    // Drawing nothing looks identical to a layout bug, so say so.
    IconStore store;
    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"nope"}]})", &store);

    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 1);
    STIPPLE_CHECK_EQ(countLit(scene.render()), 0);
}

STIPPLE_TEST(Icons, IconWithoutAStoreIsReported) {
    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"any"}]})", nullptr);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 1);
}

STIPPLE_TEST(Icons, TransparentPixelsAreSkipped) {
    IconStore store;

    Icon icon = solid("keyed", 2, colors::kBlue);
    icon.hasTransparency = true;
    icon.transparent = colors::kMagenta;
    icon.pixels[0] = colors::kMagenta;  // top-left is see-through
    icon.pixels[3] = colors::kMagenta;
    store.put(std::move(icon));

    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"keyed"}]})", &store);
    const Framebuffer frame = scene.render();

    STIPPLE_CHECK_EQ(countLit(frame), 2);
    STIPPLE_CHECK_EQ(frame.at(0, 0), colors::kBlack);
    STIPPLE_CHECK_EQ(frame.at(1, 0), colors::kBlue);
}

STIPPLE_TEST(Icons, IconsAreClippedToThePanel) {
    IconStore store;
    store.put(solid("big", 16, colors::kRed));

    Loaded scene(R"({"elements":[{"type":"icon","x":45,"y":10,"icon":"big"}]})", &store);
    const Framebuffer frame = scene.render();

    // 7 columns and 6 rows remain on the panel.
    STIPPLE_CHECK_EQ(countLit(frame), 7 * 6);
}

STIPPLE_TEST(Icons, AnimatedIconMakesTheSceneAnimate) {
    // The frame scheduler needs to know, or dirty rendering would freeze it.
    IconStore store;
    Icon animated = solid("spin", 4, colors::kRed, 3);
    animated.frameMillis = 100;
    store.put(std::move(animated));

    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"spin"}]})", &store);
    STIPPLE_CHECK(scene.scene.animates());

    IconStore staticStore;
    staticStore.put(solid("still", 4, colors::kRed));
    Loaded stillScene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"still"}]})",
                      &staticStore);
    STIPPLE_CHECK_FALSE(stillScene.scene.animates());
}

STIPPLE_TEST(Icons, AnimatedIconChangesOverTime) {
    IconStore store;
    Icon animated = solid("two", 2, colors::kRed, 2);
    animated.frameMillis = 100;
    // Second frame is a different colour.
    for (std::size_t i = 4; i < animated.pixels.size(); ++i) {
        animated.pixels[i] = colors::kBlue;
    }
    store.put(std::move(animated));

    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"two"}]})", &store);
    STIPPLE_CHECK_EQ(scene.render(0).at(0, 0), colors::kRed);
    STIPPLE_CHECK_EQ(scene.render(150).at(0, 0), colors::kBlue);
}

// --- inline bitmaps ----------------------------------------------------------

STIPPLE_TEST(Icons, InlineBitmapRenders) {
    Loaded scene(R"({"elements":[
        {"type":"bitmap","x":1,"y":1,"width":2,"height":2,
         "pixels":[16711680,65280,255,16776960]}
    ]})", nullptr);

    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 0);

    const Framebuffer frame = scene.render();
    STIPPLE_CHECK_EQ(frame.at(1, 1), colors::kRed);
    STIPPLE_CHECK_EQ(frame.at(2, 1), colors::kGreen);
    STIPPLE_CHECK_EQ(frame.at(1, 2), colors::kBlue);
    STIPPLE_CHECK_EQ(frame.at(2, 2), colors::kYellow);
}

STIPPLE_TEST(Icons, InlineBitmapHonoursATransparentKey) {
    Loaded scene(R"({"elements":[
        {"type":"bitmap","x":0,"y":0,"width":2,"height":1,
         "pixels":[16711680,0],"transparent":0}
    ]})", nullptr);

    const Framebuffer frame = scene.render();
    STIPPLE_CHECK_EQ(countLit(frame), 1);
    STIPPLE_CHECK_EQ(frame.at(0, 0), colors::kRed);
}

STIPPLE_TEST(Icons, MismatchedBitmapLengthIsReported) {
    Loaded scene(R"({"elements":[
        {"type":"bitmap","x":0,"y":0,"width":4,"height":4,"pixels":[1,2,3]}
    ]})", nullptr);

    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK(scene.scene.issueCount() > 0);
    STIPPLE_CHECK_EQ(countLit(scene.render()), 0);
}

// --- golden ------------------------------------------------------------------

STIPPLE_TEST(Icons, ThermometerSceneMatchesGolden) {
    // The blueprint §11 example, now with a real icon instead of the placeholder
    // rectangle that stood in for one.
    IconStore store;

    Icon thermometer;
    thermometer.id = "thermometer";
    thermometer.width = 5;
    thermometer.height = 9;
    thermometer.frameCount = 1;
    thermometer.hasTransparency = true;
    thermometer.transparent = colors::kMagenta;

    const Rgb T = colors::kMagenta;              // transparent
    const Rgb S = stipple::rgb(180, 180, 190);    // glass
    const Rgb M = stipple::rgb(255, 60, 40);      // mercury
    const Rgb pixels[45] = {
        T, S, S, S, T,
        T, S, T, S, T,
        T, S, M, S, T,
        T, S, M, S, T,
        T, S, M, S, T,
        S, M, M, M, S,
        S, M, M, M, S,
        S, M, M, M, S,
        T, S, S, S, T,
    };
    thermometer.pixels.assign(pixels, pixels + 45);
    store.put(std::move(thermometer));

    Loaded scene(R"({"name":"living-room","elements":[
        {"type":"icon","x":1,"y":4,"icon":"thermometer"},
        {"type":"text","rect":[9,0,43,7],"text":"21.4°C","align":"left","color":"#ffaa28"},
        {"type":"text","rect":[9,9,43,7],"text":"Living room","align":"left","color":"#00c8ff"}
    ]})", &store);

    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 0);
    STIPPLE_CHECK_GOLDEN("icon-thermometer", scene.render());
}

// --- persistence -------------------------------------------------------------

STIPPLE_TEST(Icons, RoundTripsThroughStorage) {
    IconStore original;

    Icon flag = solid("flag", 6, colors::kGreen);
    flag.hasTransparency = true;
    flag.transparent = colors::kMagenta;
    flag.pixels[0] = colors::kMagenta;
    original.put(std::move(flag));

    Icon spin = solid("spin", 4, colors::kBlue, 3);
    spin.frameMillis = 250;
    original.put(std::move(spin));

    const std::string blob = original.serialize();

    IconStore restored;
    STIPPLE_CHECK(restored.deserialize(blob));
    STIPPLE_CHECK_EQ(restored.count(), 2);
    STIPPLE_CHECK_EQ(restored.bytesUsed(), original.bytesUsed());

    const Icon* flagBack = restored.find("flag");
    STIPPLE_CHECK(flagBack != nullptr);
    STIPPLE_CHECK_EQ(flagBack->width, 6);
    STIPPLE_CHECK(flagBack->hasTransparency);
    STIPPLE_CHECK_EQ(flagBack->transparent, colors::kMagenta);
    STIPPLE_CHECK_EQ(flagBack->pixels[0], colors::kMagenta);
    STIPPLE_CHECK_EQ(flagBack->pixels[1], colors::kGreen);

    const Icon* spinBack = restored.find("spin");
    STIPPLE_CHECK_EQ(spinBack->frameCount, 3);
    STIPPLE_CHECK_EQ(spinBack->frameMillis, std::uint32_t(250));
}

STIPPLE_TEST(Icons, BinaryIsFarSmallerThanJsonWouldBe) {
    // The reason for a binary format rather than reusing the JSON path: an
    // animation encoded as decimal text would not fit in a storage value.
    IconStore store;
    store.put(solid("a", 16, colors::kRed, 8));

    const std::size_t pixels = 16u * 16u * 8u;
    const std::size_t blob = store.serialize().size();

    STIPPLE_CHECK(blob < pixels * 4u);           // ~3 bytes per pixel plus a header
    STIPPLE_CHECK(blob > pixels * 3u);           // and it really does hold them all
}

STIPPLE_TEST(Icons, EmptyStoreRoundTrips) {
    IconStore empty;
    IconStore restored;
    restored.put(solid("stale", 4, colors::kRed));

    STIPPLE_CHECK(restored.deserialize(empty.serialize()));
    STIPPLE_CHECK_EQ(restored.count(), 0);
}

STIPPLE_TEST(Icons, RejectsCorruptBlobsWithoutCrashing) {
    IconStore store;
    const char* samples[] = {"", "NIC", "NIC\x01", "garbage", "NIC\x02\x01", "\x00\x00\x00\x00"};

    for (const char* sample : samples) {
        IconStore target;
        STIPPLE_CHECK_FALSE(target.deserialize(std::string(sample, std::strlen(sample))));
        STIPPLE_CHECK_EQ(target.count(), 0);
    }
}

STIPPLE_TEST(Icons, EveryTruncationOfAValidBlobIsRejected) {
    // Truncation is what an interrupted write looks like. No prefix may be
    // accepted as a partial set, and none may read past the end.
    IconStore store;
    store.put(solid("a", 5, colors::kRed, 2));
    store.put(solid("b", 4, colors::kBlue));

    const std::string blob = store.serialize();
    for (std::size_t length = 0; length < blob.size(); ++length) {
        IconStore target;
        STIPPLE_CHECK_FALSE(target.deserialize(blob.substr(0, length)));
    }
    IconStore target;
    STIPPLE_CHECK(target.deserialize(blob));
}

STIPPLE_TEST(Icons, TrailingRubbishIsRejected) {
    // Accepting extra bytes would mean a blob could carry something we did not
    // write and did not notice.
    IconStore store;
    store.put(solid("a", 4, colors::kRed));

    IconStore target;
    STIPPLE_CHECK_FALSE(target.deserialize(store.serialize() + "extra"));
}

STIPPLE_TEST(Icons, LyingGeometryInAStoredBlobIsRejected) {
    IconStore store;
    store.put(solid("a", 4, colors::kRed));
    std::string blob = store.serialize();

    // Header is "NIC" + version + count + idLen + "a", so width sits next.
    const std::size_t widthOffset = 3 + 1 + 1 + 1 + 1;
    blob[widthOffset] = static_cast<char>(32);  // claims 32 wide with 4x4 of pixels

    IconStore target;
    STIPPLE_CHECK_FALSE(target.deserialize(blob));
    STIPPLE_CHECK_EQ(target.count(), 0);
}
