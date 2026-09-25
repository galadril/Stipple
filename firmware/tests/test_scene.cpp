// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/scene/Scene.h"

#include <string>

#include "stipple/graphics/Canvas.h"
#include "support/Golden.h"
#include "support/TestFramework.h"

using stipple::Canvas;
using stipple::Framebuffer;
using stipple::Rect;
using stipple::Rgb;
using stipple::json::Token;
using stipple::scene::ElementType;
using stipple::scene::elementTypeFromName;
using stipple::scene::isImplemented;
using stipple::scene::parseColor;
using stipple::scene::Scene;
namespace colors = stipple::colors;

namespace {

constexpr int kTokens = 512;

/// Owns the JSON text and token storage alongside the Scene, since the Scene
/// holds views into both.
struct Loaded {
    Token storage[kTokens];
    std::string text;
    Scene scene{storage, kTokens};
    bool ok = false;

    explicit Loaded(std::string json) : text(std::move(json)) { ok = scene.load(text); }

    Framebuffer render() {
        Framebuffer framebuffer;
        Canvas canvas(framebuffer);
        scene.render(canvas);
        return framebuffer;
    }
};

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

}  // namespace

// --- document structure ------------------------------------------------------

STIPPLE_TEST(Scene, LoadsMetadata) {
    Loaded scene(R"({"name":"living-room","duration":10,"elements":[]})");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(std::string(scene.scene.name()), std::string("living-room"));
    STIPPLE_CHECK_EQ(scene.scene.durationSeconds(), 10);
    STIPPLE_CHECK_EQ(scene.scene.elementCount(), 0);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 0);
}

STIPPLE_TEST(Scene, RejectsMalformedJson) {
    Loaded scene("{not json");
    STIPPLE_CHECK_FALSE(scene.ok);
    STIPPLE_CHECK(scene.scene.issueCount() > 0);
}

STIPPLE_TEST(Scene, RejectsNonObjectRoot) {
    Loaded scene("[1,2,3]");
    STIPPLE_CHECK_FALSE(scene.ok);
}

STIPPLE_TEST(Scene, RequiresElementsArray) {
    STIPPLE_CHECK_FALSE(Loaded(R"({"name":"x"})").ok);
    STIPPLE_CHECK_FALSE(Loaded(R"({"elements":{}})").ok);
}

STIPPLE_TEST(Scene, RejectsOutOfRangeDuration) {
    Loaded scene(R"({"duration":999999,"elements":[]})");
    STIPPLE_CHECK(scene.ok);  // still renderable
    STIPPLE_CHECK_EQ(scene.scene.durationSeconds(), 0);
    STIPPLE_CHECK(scene.scene.issueCount() > 0);
}

// --- validation --------------------------------------------------------------

STIPPLE_TEST(Scene, UnimplementedTypesFailLoudly) {
    // Blueprint §19: document incompatibilities rather than silently accepting
    // fields. A scene asking for a sprite must be told sprites do not exist yet.
    Loaded scene(R"({"elements":[{"type":"sprite","x":0,"y":0,"sprite":"walk"}]})");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 1);
    STIPPLE_CHECK_EQ(scene.scene.issueAt(0).elementIndex, 0);

    STIPPLE_CHECK(isImplemented(ElementType::Text));
    STIPPLE_CHECK(isImplemented(ElementType::Icon));     // implemented in Phase 6
    STIPPLE_CHECK_FALSE(isImplemented(ElementType::Sprite));
    STIPPLE_CHECK_FALSE(isImplemented(ElementType::Animation));
}

STIPPLE_TEST(Scene, UnknownTypeIsReported) {
    Loaded scene(R"({"elements":[{"type":"teleporter"}]})");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 1);
}

STIPPLE_TEST(Scene, MissingRequiredFieldsAreReported) {
    STIPPLE_CHECK(Loaded(R"({"elements":[{"type":"text","rect":[0,0,10,7]}]})")
                     .scene.issueCount() > 0);  // no "text"
    STIPPLE_CHECK(Loaded(R"({"elements":[{"type":"text","text":"hi"}]})")
                     .scene.issueCount() > 0);  // no "rect"
    STIPPLE_CHECK(Loaded(R"({"elements":[{"type":"pixel","x":1}]})")
                     .scene.issueCount() > 0);  // no "y"
    STIPPLE_CHECK(Loaded(R"({"elements":[{"type":"graph","rect":[0,0,10,8]}]})")
                     .scene.issueCount() > 0);  // no "values"
}

STIPPLE_TEST(Scene, OneBadElementDoesNotDiscardTheRest) {
    // A single malformed element must not blank an otherwise working screen.
    Loaded scene(R"({"elements":[
        {"type":"teleporter"},
        {"type":"rect","rect":[0,0,52,16],"color":"#ffffff","fill":true}
    ]})");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 1);
    STIPPLE_CHECK_EQ(countLit(scene.render()), 832);
}

STIPPLE_TEST(Scene, IssueStorageIsBounded) {
    std::string json = R"({"elements":[)";
    for (int i = 0; i < 40; ++i) {
        if (i > 0) {
            json += ',';
        }
        json += R"({"type":"nope"})";
    }
    json += "]}";

    Loaded scene(json);
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), Scene::kMaxIssues);
    STIPPLE_CHECK(scene.scene.issueOverflow());
}

// --- colour and rect parsing -------------------------------------------------

STIPPLE_TEST(Scene, ParsesEveryColourForm) {
    Loaded scene(R"({"elements":[
        {"type":"pixel","x":0,"y":0,"color":"#ff0000"},
        {"type":"pixel","x":1,"y":0,"color":"00ff00"},
        {"type":"pixel","x":2,"y":0,"color":[0,0,255]},
        {"type":"pixel","x":3,"y":0,"color":16776960}
    ]})");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 0);

    const Framebuffer frame = scene.render();
    STIPPLE_CHECK_EQ(frame.at(0, 0), colors::kRed);
    STIPPLE_CHECK_EQ(frame.at(1, 0), colors::kGreen);
    STIPPLE_CHECK_EQ(frame.at(2, 0), colors::kBlue);
    STIPPLE_CHECK_EQ(frame.at(3, 0), colors::kYellow);
}

STIPPLE_TEST(Scene, MissingColourDefaultsToWhite) {
    Loaded scene(R"({"elements":[{"type":"pixel","x":5,"y":5}]})");
    STIPPLE_CHECK_EQ(scene.render().at(5, 5), colors::kWhite);
}

STIPPLE_TEST(Scene, RejectsInvalidColours) {
    Rgb color;

    // Exercise parseColor directly: these are field-level rejections, not
    // whole-scene ones.
    Token storage[64];
    stipple::json::Document document(storage, 64);
    const std::string text = R"({"a":"#zzzzzz","b":"#fff","c":[1,2],"d":-5,"e":true})";
    STIPPLE_CHECK(document.parse(text) == stipple::json::Error::None);

    STIPPLE_CHECK_FALSE(parseColor(document.root()["a"], color));
    STIPPLE_CHECK_FALSE(parseColor(document.root()["b"], color));
    STIPPLE_CHECK_FALSE(parseColor(document.root()["c"], color));
    STIPPLE_CHECK_FALSE(parseColor(document.root()["d"], color));
    STIPPLE_CHECK_FALSE(parseColor(document.root()["e"], color));
}

// --- rendering ---------------------------------------------------------------

STIPPLE_TEST(Scene, RendersOutlineAndFilledRects) {
    Loaded outline(R"({"elements":[{"type":"rect","rect":[0,0,4,4],"color":"#ffffff"}]})");
    STIPPLE_CHECK_EQ(countLit(outline.render()), 12);  // perimeter only

    Loaded filled(
        R"({"elements":[{"type":"rect","rect":[0,0,4,4],"color":"#ffffff","fill":true}]})");
    STIPPLE_CHECK_EQ(countLit(filled.render()), 16);
}

STIPPLE_TEST(Scene, ElementsAreClippedToThePanel) {
    Loaded scene(
        R"({"elements":[{"type":"rect","rect":[-100,-100,10000,10000],"color":"#ffffff","fill":true}]})");
    STIPPLE_CHECK_EQ(countLit(scene.render()), 832);
}

STIPPLE_TEST(Scene, AbsurdCoordinatesDoNotMisbehave) {
    // Coordinates arrive from the network; huge values must clamp, not overflow.
    Loaded scene(R"({"elements":[
        {"type":"pixel","x":999999999999,"y":-999999999999},
        {"type":"line","x1":-99999999,"y1":0,"x2":99999999,"y2":0,"color":"#ffffff"}
    ]})");
    STIPPLE_CHECK(scene.ok);
    const Framebuffer frame = scene.render();
    STIPPLE_CHECK_EQ(countLit(frame), 52);  // just the horizontal line
}

STIPPLE_TEST(Scene, ProgressFillsProportionally) {
    const auto litAt = [](int percent) {
        Loaded scene(R"({"elements":[{"type":"progress","rect":[0,0,50,2],"value":)" +
                     std::to_string(percent) + R"(,"color":"#ffffff","background":"#000000"}]})");
        return countLit(scene.render());
    };

    STIPPLE_CHECK_EQ(litAt(0), 0);
    STIPPLE_CHECK_EQ(litAt(50), 50);    // 25 columns x 2 rows
    STIPPLE_CHECK_EQ(litAt(100), 100);  // full 50 x 2
}

STIPPLE_TEST(Scene, ProgressClampsOutOfRangeValues) {
    Loaded under(
        R"({"elements":[{"type":"progress","rect":[0,0,50,2],"value":-40,"color":"#ffffff","background":"#000000"}]})");
    STIPPLE_CHECK_EQ(countLit(under.render()), 0);

    Loaded over(
        R"({"elements":[{"type":"progress","rect":[0,0,50,2],"value":400,"color":"#ffffff","background":"#000000"}]})");
    STIPPLE_CHECK_EQ(countLit(over.render()), 100);
}

STIPPLE_TEST(Scene, GraphAutoScalesAndStaysInItsRect) {
    Loaded scene(
        R"({"elements":[{"type":"graph","rect":[10,4,20,8],"values":[1,5,3,9,2],"color":"#00ff00"}]})");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 0);

    const Framebuffer frame = scene.render();
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (frame.at(x, y) != colors::kBlack) {
                STIPPLE_CHECK(x >= 10 && x < 30);
                STIPPLE_CHECK(y >= 4 && y < 12);
            }
        }
    }
}

STIPPLE_TEST(Scene, FlatGraphStillDrawsSomething) {
    // A constant series has zero span; it must not vanish or divide by zero.
    Loaded scene(
        R"({"elements":[{"type":"graph","rect":[0,0,10,8],"values":[5,5,5,5],"color":"#ffffff"}]})");
    STIPPLE_CHECK(countLit(scene.render()) > 0);
}

STIPPLE_TEST(Scene, GroupClipsItsChildren) {
    // A child positioned outside the group must not escape it.
    Loaded scene(R"({"elements":[{"type":"group","rect":[0,0,10,8],"elements":[
        {"type":"rect","rect":[0,0,52,16],"color":"#ffffff","fill":true}
    ]}]})");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(countLit(scene.render()), 80);  // 10 x 8, not the whole panel
}

STIPPLE_TEST(Scene, NestedGroupsAreBounded) {
    // Deeply nested groups must terminate; the JSON depth limit and the render
    // depth guard both apply.
    std::string json = R"({"elements":[)";
    int opened = 0;
    for (int i = 0; i < 12; ++i) {
        json += R"({"type":"group","rect":[0,0,52,16],"elements":[)";
        ++opened;
    }
    json += R"({"type":"pixel","x":0,"y":0})";
    for (int i = 0; i < opened; ++i) {
        json += "]}";
    }
    json += "]}";

    Loaded scene(json);
    scene.render();  // must simply terminate
    STIPPLE_CHECK(true);
}

STIPPLE_TEST(Scene, RendersTextWithAlignment) {
    // 'H' lights both outer columns of its cell, so the ink reaches the box
    // edge exactly. A glyph like 'I' does not, and would land a pixel short.
    Loaded scene(R"({"elements":[
        {"type":"text","rect":[0,0,52,7],"text":"HH","align":"right","color":"#ffffff"}
    ]})");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 0);

    const Framebuffer frame = scene.render();
    int rightmost = -1;
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            if (frame.at(x, y) != colors::kBlack && x > rightmost) {
                rightmost = x;
            }
        }
    }
    STIPPLE_CHECK_EQ(rightmost, Framebuffer::kWidth - 1);
}

STIPPLE_TEST(Scene, TypeNameRoundTrips) {
    STIPPLE_CHECK(elementTypeFromName("rectangle") == ElementType::Rect);
    STIPPLE_CHECK(elementTypeFromName("rect") == ElementType::Rect);
    STIPPLE_CHECK(elementTypeFromName("") == ElementType::Unknown);
    STIPPLE_CHECK_EQ(std::string(stipple::scene::elementTypeName(ElementType::Group)),
                    std::string("group"));
}

// --- golden ------------------------------------------------------------------

STIPPLE_TEST(Scene, BlueprintExampleMatchesGolden) {
    // Close to the blueprint §11 sample, with the icon replaced by a rect since
    // icons are not implemented yet.
    Loaded scene(R"({
      "name": "living-room",
      "duration": 10,
      "elements": [
        {"type":"rect","rect":[1,4,7,7],"color":"#ff5000","fill":true},
        {"type":"text","rect":[11,0,40,7],"text":"21.4°C","align":"left","color":"#ffaa28"},
        {"type":"text","rect":[11,9,40,7],"text":"Living room","align":"left","color":"#00c8ff"}
      ]
    })");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 0);

    STIPPLE_CHECK_GOLDEN("scene-living-room", scene.render());
}

STIPPLE_TEST(Scene, DashboardMatchesGolden) {
    Loaded scene(R"({
      "elements": [
        {"type":"text","rect":[0,0,26,7],"text":"CPU","align":"left","color":"#ffffff"},
        {"type":"progress","rect":[0,8,26,3],"value":72,"color":"#00ff00","background":"#003300"},
        {"type":"graph","rect":[28,1,24,14],"values":[3,5,4,8,6,9,7,12,10,14,11,15],
         "color":"#00aaff"},
        {"type":"line","x1":27,"y1":0,"x2":27,"y2":15,"color":"#303030"}
      ]
    })");
    STIPPLE_CHECK(scene.ok);
    STIPPLE_CHECK_EQ(scene.scene.issueCount(), 0);

    STIPPLE_CHECK_GOLDEN("scene-dashboard", scene.render());
}
