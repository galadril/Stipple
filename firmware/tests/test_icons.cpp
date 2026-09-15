// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/asset/IconStore.h"

#include <string>

#include "notrix/graphics/Canvas.h"
#include "notrix/json/Json.h"
#include "notrix/scene/Scene.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using notrix::Canvas;
using notrix::Framebuffer;
using notrix::Rgb;
using notrix::asset::Icon;
using notrix::asset::IconStore;
using notrix::json::Token;
using notrix::scene::ElementType;
using notrix::scene::isImplemented;
using notrix::scene::Scene;
namespace colors = notrix::colors;

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

NOTRIX_TEST(Icons, StoresAndFindsByName) {
    IconStore store;
    NOTRIX_CHECK(store.put(solid("thermometer", 8, colors::kRed)) == IconStore::PutResult::Added);

    NOTRIX_CHECK_EQ(store.count(), 1);
    NOTRIX_CHECK(store.find("thermometer") != nullptr);
    NOTRIX_CHECK(store.find("missing") == nullptr);
    NOTRIX_CHECK_EQ(store.bytesUsed(), std::size_t(8 * 8 * 3));
}

NOTRIX_TEST(Icons, ReplacingReclaimsTheOldBudget) {
    // Updating an icon in place must not fail against a budget it already fits
    // inside.
    IconStore store;
    store.put(solid("a", 16, colors::kRed));
    const std::size_t afterFirst = store.bytesUsed();

    NOTRIX_CHECK(store.put(solid("a", 16, colors::kBlue)) == IconStore::PutResult::Replaced);
    NOTRIX_CHECK_EQ(store.bytesUsed(), afterFirst);
    NOTRIX_CHECK_EQ(store.count(), 1);
}

NOTRIX_TEST(Icons, RejectsMismatchedGeometry) {
    // Trusting a declared width against a shorter buffer is how a blit reads
    // past the end.
    IconStore store;

    Icon lying = solid("bad", 8, colors::kRed);
    lying.width = 16;  // claims 16x8 but holds 8x8 pixels
    NOTRIX_CHECK(store.put(std::move(lying)) == IconStore::PutResult::InvalidGeometry);
    NOTRIX_CHECK_EQ(store.count(), 0);
}

NOTRIX_TEST(Icons, RejectsAbsurdDimensions) {
    IconStore store;

    Icon huge;
    huge.id = "huge";
    huge.width = IconStore::kMaxDimension + 1;
    huge.height = 8;
    huge.frameCount = 1;
    huge.pixels.assign(static_cast<std::size_t>(huge.width * huge.height), colors::kRed);
    NOTRIX_CHECK(store.put(std::move(huge)) == IconStore::PutResult::InvalidGeometry);

    NOTRIX_CHECK(store.put(solid("", 8, colors::kRed)) == IconStore::PutResult::InvalidId);
    NOTRIX_CHECK(store.put(solid(std::string(IconStore::kMaxIdBytes + 1, 'x'), 8, colors::kRed)) ==
                 IconStore::PutResult::InvalidId);
}

NOTRIX_TEST(Icons, EnforcesTheTotalByteBudget) {
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

    NOTRIX_CHECK(stored > 0);
    NOTRIX_CHECK(store.bytesUsed() <= IconStore::kMaxTotalBytes);
    NOTRIX_CHECK(store.put(solid("overflow", 16, colors::kRed)) ==
                 IconStore::PutResult::BudgetExceeded);
}

NOTRIX_TEST(Icons, RemoveFreesBudget) {
    IconStore store;
    store.put(solid("a", 16, colors::kRed));
    const std::size_t used = store.bytesUsed();
    NOTRIX_CHECK(used > 0);

    NOTRIX_CHECK(store.remove("a"));
    NOTRIX_CHECK_EQ(store.bytesUsed(), std::size_t(0));
    NOTRIX_CHECK_FALSE(store.remove("a"));
}

NOTRIX_TEST(Icons, AnimationPicksFramesByTime) {
    IconStore store;
    Icon animated = solid("spin", 4, colors::kRed, 4);
    animated.frameMillis = 100;
    store.put(std::move(animated));

    const Icon* icon = store.find("spin");
    NOTRIX_CHECK(icon->animated());
    NOTRIX_CHECK_EQ(IconStore::frameAt(*icon, 0), 0);
    NOTRIX_CHECK_EQ(IconStore::frameAt(*icon, 150), 1);
    NOTRIX_CHECK_EQ(IconStore::frameAt(*icon, 350), 3);
    NOTRIX_CHECK_EQ(IconStore::frameAt(*icon, 400), 0);  // wraps
}

NOTRIX_TEST(Icons, ZeroFrameDurationDoesNotDivideByZero) {
    IconStore store;
    Icon icon = solid("x", 4, colors::kRed, 3);
    icon.frameMillis = 0;
    store.put(std::move(icon));

    NOTRIX_CHECK(store.find("x")->frameMillis > 0);
    NOTRIX_CHECK_EQ(IconStore::frameAt(*store.find("x"), 999999), 0);
}

NOTRIX_TEST(Icons, FrameViewsAreBoundsChecked) {
    IconStore store;
    store.put(solid("a", 4, colors::kRed, 2));
    const Icon* icon = store.find("a");

    NOTRIX_CHECK(IconStore::frameView(*icon, 0).valid());
    NOTRIX_CHECK(IconStore::frameView(*icon, 1).valid());
    NOTRIX_CHECK_FALSE(IconStore::frameView(*icon, 2).valid());
    NOTRIX_CHECK_FALSE(IconStore::frameView(*icon, -1).valid());
}

NOTRIX_TEST(Icons, RevisionTracksMutations) {
    IconStore store;
    const std::uint32_t start = store.revision();
    store.put(solid("a", 4, colors::kRed));
    NOTRIX_CHECK(store.revision() != start);
}

// --- scene integration -------------------------------------------------------

NOTRIX_TEST(Icons, IconAndBitmapAreNowImplemented) {
    NOTRIX_CHECK(isImplemented(ElementType::Icon));
    NOTRIX_CHECK(isImplemented(ElementType::Bitmap));
    // Still honestly reported as absent.
    NOTRIX_CHECK_FALSE(isImplemented(ElementType::Sprite));
    NOTRIX_CHECK_FALSE(isImplemented(ElementType::Animation));
}

NOTRIX_TEST(Icons, SceneDrawsAStoredIcon) {
    IconStore store;
    store.put(solid("dot", 4, colors::kGreen));

    Loaded scene(R"({"elements":[{"type":"icon","x":2,"y":3,"icon":"dot"}]})", &store);
    NOTRIX_CHECK(scene.ok);
    NOTRIX_CHECK_EQ(scene.scene.issueCount(), 0);

    const Framebuffer frame = scene.render();
    NOTRIX_CHECK_EQ(countLit(frame), 16);
    NOTRIX_CHECK_EQ(frame.at(2, 3), colors::kGreen);
    NOTRIX_CHECK_EQ(frame.at(5, 6), colors::kGreen);
    NOTRIX_CHECK_EQ(frame.at(6, 7), colors::kBlack);
}

NOTRIX_TEST(Icons, MissingIconIsReportedNotSilentlySkipped) {
    // Drawing nothing looks identical to a layout bug, so say so.
    IconStore store;
    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"nope"}]})", &store);

    NOTRIX_CHECK(scene.ok);
    NOTRIX_CHECK_EQ(scene.scene.issueCount(), 1);
    NOTRIX_CHECK_EQ(countLit(scene.render()), 0);
}

NOTRIX_TEST(Icons, IconWithoutAStoreIsReported) {
    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"any"}]})", nullptr);
    NOTRIX_CHECK_EQ(scene.scene.issueCount(), 1);
}

NOTRIX_TEST(Icons, TransparentPixelsAreSkipped) {
    IconStore store;

    Icon icon = solid("keyed", 2, colors::kBlue);
    icon.hasTransparency = true;
    icon.transparent = colors::kMagenta;
    icon.pixels[0] = colors::kMagenta;  // top-left is see-through
    icon.pixels[3] = colors::kMagenta;
    store.put(std::move(icon));

    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"keyed"}]})", &store);
    const Framebuffer frame = scene.render();

    NOTRIX_CHECK_EQ(countLit(frame), 2);
    NOTRIX_CHECK_EQ(frame.at(0, 0), colors::kBlack);
    NOTRIX_CHECK_EQ(frame.at(1, 0), colors::kBlue);
}

NOTRIX_TEST(Icons, IconsAreClippedToThePanel) {
    IconStore store;
    store.put(solid("big", 16, colors::kRed));

    Loaded scene(R"({"elements":[{"type":"icon","x":45,"y":10,"icon":"big"}]})", &store);
    const Framebuffer frame = scene.render();

    // 7 columns and 6 rows remain on the panel.
    NOTRIX_CHECK_EQ(countLit(frame), 7 * 6);
}

NOTRIX_TEST(Icons, AnimatedIconMakesTheSceneAnimate) {
    // The frame scheduler needs to know, or dirty rendering would freeze it.
    IconStore store;
    Icon animated = solid("spin", 4, colors::kRed, 3);
    animated.frameMillis = 100;
    store.put(std::move(animated));

    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"spin"}]})", &store);
    NOTRIX_CHECK(scene.scene.animates());

    IconStore staticStore;
    staticStore.put(solid("still", 4, colors::kRed));
    Loaded stillScene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"still"}]})",
                      &staticStore);
    NOTRIX_CHECK_FALSE(stillScene.scene.animates());
}

NOTRIX_TEST(Icons, AnimatedIconChangesOverTime) {
    IconStore store;
    Icon animated = solid("two", 2, colors::kRed, 2);
    animated.frameMillis = 100;
    // Second frame is a different colour.
    for (std::size_t i = 4; i < animated.pixels.size(); ++i) {
        animated.pixels[i] = colors::kBlue;
    }
    store.put(std::move(animated));

    Loaded scene(R"({"elements":[{"type":"icon","x":0,"y":0,"icon":"two"}]})", &store);
    NOTRIX_CHECK_EQ(scene.render(0).at(0, 0), colors::kRed);
    NOTRIX_CHECK_EQ(scene.render(150).at(0, 0), colors::kBlue);
}

// --- inline bitmaps ----------------------------------------------------------

NOTRIX_TEST(Icons, InlineBitmapRenders) {
    Loaded scene(R"({"elements":[
        {"type":"bitmap","x":1,"y":1,"width":2,"height":2,
         "pixels":[16711680,65280,255,16776960]}
    ]})", nullptr);

    NOTRIX_CHECK(scene.ok);
    NOTRIX_CHECK_EQ(scene.scene.issueCount(), 0);

    const Framebuffer frame = scene.render();
    NOTRIX_CHECK_EQ(frame.at(1, 1), colors::kRed);
    NOTRIX_CHECK_EQ(frame.at(2, 1), colors::kGreen);
    NOTRIX_CHECK_EQ(frame.at(1, 2), colors::kBlue);
    NOTRIX_CHECK_EQ(frame.at(2, 2), colors::kYellow);
}

NOTRIX_TEST(Icons, InlineBitmapHonoursATransparentKey) {
    Loaded scene(R"({"elements":[
        {"type":"bitmap","x":0,"y":0,"width":2,"height":1,
         "pixels":[16711680,0],"transparent":0}
    ]})", nullptr);

    const Framebuffer frame = scene.render();
    NOTRIX_CHECK_EQ(countLit(frame), 1);
    NOTRIX_CHECK_EQ(frame.at(0, 0), colors::kRed);
}

NOTRIX_TEST(Icons, MismatchedBitmapLengthIsReported) {
    Loaded scene(R"({"elements":[
        {"type":"bitmap","x":0,"y":0,"width":4,"height":4,"pixels":[1,2,3]}
    ]})", nullptr);

    NOTRIX_CHECK(scene.ok);
    NOTRIX_CHECK(scene.scene.issueCount() > 0);
    NOTRIX_CHECK_EQ(countLit(scene.render()), 0);
}

// --- golden ------------------------------------------------------------------

NOTRIX_TEST(Icons, ThermometerSceneMatchesGolden) {
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
    const Rgb S = notrix::rgb(180, 180, 190);    // glass
    const Rgb M = notrix::rgb(255, 60, 40);      // mercury
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

    NOTRIX_CHECK(scene.ok);
    NOTRIX_CHECK_EQ(scene.scene.issueCount(), 0);
    NOTRIX_CHECK_GOLDEN("icon-thermometer", scene.render());
}
