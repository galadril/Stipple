// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/scene/Scene.h"

#include "notrix/graphics/Canvas.h"
#include "notrix/text/Scroll.h"
#include "notrix/text/Text.h"

namespace notrix {
namespace scene {
namespace {

constexpr int kMaxDurationSeconds = 3600;

int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int clampToPanel(std::int64_t value) noexcept {
    // Coordinates arrive from the network. Clamping to a generous range keeps
    // arithmetic well away from overflow; Canvas clips the result anyway.
    constexpr std::int64_t kLimit = 4096;
    if (value < -kLimit) {
        return static_cast<int>(-kLimit);
    }
    if (value > kLimit) {
        return static_cast<int>(kLimit);
    }
    return static_cast<int>(value);
}

text::HAlign horizontalAlign(const json::Value& value) noexcept {
    if (value.stringEquals("center")) {
        return text::HAlign::Center;
    }
    if (value.stringEquals("right")) {
        return text::HAlign::Right;
    }
    return text::HAlign::Left;
}

text::VAlign verticalAlign(const json::Value& value) noexcept {
    if (value.stringEquals("middle")) {
        return text::VAlign::Middle;
    }
    if (value.stringEquals("bottom")) {
        return text::VAlign::Bottom;
    }
    return text::VAlign::Top;
}

Rgb colorOr(const json::Value& value, Rgb fallback) noexcept {
    Rgb parsed;
    return parseColor(value, parsed) ? parsed : fallback;
}

}  // namespace

// --- field parsing -----------------------------------------------------------

bool parseColor(const json::Value& value, Rgb& out) noexcept {
    if (!value.valid()) {
        return false;
    }

    if (value.isString()) {
        std::string_view text = value.raw();
        if (!text.empty() && text.front() == '#') {
            text.remove_prefix(1);
        }
        if (text.size() != 6) {
            return false;
        }
        int channels[3] = {0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            const int high = hexValue(text[static_cast<std::size_t>(i) * 2u]);
            const int low = hexValue(text[static_cast<std::size_t>(i) * 2u + 1u]);
            if (high < 0 || low < 0) {
                return false;
            }
            channels[i] = high * 16 + low;
        }
        out = rgb(channels[0], channels[1], channels[2]);
        return true;
    }

    if (value.isArray()) {
        if (value.size() != 3) {
            return false;
        }
        for (int i = 0; i < 3; ++i) {
            if (!value[i].isNumber()) {
                return false;
            }
        }
        out = rgb(static_cast<int>(value[0].toInt()), static_cast<int>(value[1].toInt()),
                  static_cast<int>(value[2].toInt()));
        return true;
    }

    if (value.isNumber()) {
        const std::int64_t packed = value.toInt(-1);
        if (packed < 0 || packed > 0xFFFFFF) {
            return false;
        }
        out = fromPacked(static_cast<std::uint32_t>(packed));
        return true;
    }

    return false;
}

bool parseRect(const json::Value& value, Rect& out) noexcept {
    if (!value.isArray() || value.size() != 4) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!value[i].isNumber()) {
            return false;
        }
    }
    out = Rect{clampToPanel(value[0].toInt()), clampToPanel(value[1].toInt()),
               clampToPanel(value[2].toInt()), clampToPanel(value[3].toInt())};
    return true;
}

// --- element types -----------------------------------------------------------

ElementType elementTypeFromName(std::string_view name) noexcept {
    if (name == "pixel") return ElementType::Pixel;
    if (name == "line") return ElementType::Line;
    if (name == "rect" || name == "rectangle") return ElementType::Rect;
    if (name == "text") return ElementType::Text;
    if (name == "progress") return ElementType::Progress;
    if (name == "graph") return ElementType::Graph;
    if (name == "group") return ElementType::Group;
    if (name == "icon") return ElementType::Icon;
    if (name == "bitmap") return ElementType::Bitmap;
    if (name == "sprite") return ElementType::Sprite;
    if (name == "animation") return ElementType::Animation;
    return ElementType::Unknown;
}

const char* elementTypeName(ElementType type) noexcept {
    switch (type) {
        case ElementType::Pixel: return "pixel";
        case ElementType::Line: return "line";
        case ElementType::Rect: return "rect";
        case ElementType::Text: return "text";
        case ElementType::Progress: return "progress";
        case ElementType::Graph: return "graph";
        case ElementType::Group: return "group";
        case ElementType::Icon: return "icon";
        case ElementType::Bitmap: return "bitmap";
        case ElementType::Sprite: return "sprite";
        case ElementType::Animation: return "animation";
        case ElementType::Unknown: break;
    }
    return "unknown";
}

bool isImplemented(ElementType type) noexcept {
    switch (type) {
        case ElementType::Pixel:
        case ElementType::Line:
        case ElementType::Rect:
        case ElementType::Text:
        case ElementType::Progress:
        case ElementType::Graph:
        case ElementType::Group:
            return true;
        default:
            return false;
    }
}

// --- Scene -------------------------------------------------------------------

const Issue& Scene::issueAt(int index) const noexcept {
    static const Issue empty{};
    if (index < 0 || index >= issueCount_) {
        return empty;
    }
    return issues_[index];
}

void Scene::addIssue(int elementIndex, const char* message) noexcept {
    if (issueCount_ >= kMaxIssues) {
        issueOverflow_ = true;
        return;
    }
    issues_[issueCount_].elementIndex = elementIndex;
    issues_[issueCount_].message = message;
    ++issueCount_;
}

int Scene::elementCount() const noexcept {
    if (!loaded_) {
        return 0;
    }
    return document_.root()["elements"].size();
}

bool Scene::load(std::string_view json, const json::Limits& limits) {
    loaded_ = false;
    name_ = {};
    durationSeconds_ = 0;
    issueCount_ = 0;
    issueOverflow_ = false;
    animates_ = false;

    if (document_.parse(json, limits) != json::Error::None) {
        addIssue(-1, json::describe(document_.error()));
        return false;
    }

    const json::Value root = document_.root();
    if (!root.isObject()) {
        addIssue(-1, "scene must be a JSON object");
        return false;
    }

    const json::Value elements = root["elements"];
    if (!elements.isArray()) {
        addIssue(-1, "scene requires an 'elements' array");
        return false;
    }

    const json::Value name = root["name"];
    if (name.isString()) {
        name_ = name.raw();
    }

    const json::Value duration = root["duration"];
    if (duration.isNumber()) {
        const std::int64_t seconds = duration.toInt(0);
        if (seconds < 0 || seconds > kMaxDurationSeconds) {
            addIssue(-1, "duration out of range");
        } else {
            durationSeconds_ = static_cast<int>(seconds);
        }
    }

    loaded_ = true;
    validate();
    return true;
}

void Scene::validate() {
    const json::Value elements = document_.root()["elements"];
    const int count = elements.size();
    for (int i = 0; i < count; ++i) {
        validateElement(elements[i], i, 0);
    }
}

void Scene::validateElement(const json::Value& element, int reportIndex, int depth) {
    if (depth > kMaxGroupDepth) {
        addIssue(reportIndex, "group nesting is deeper than the renderer will follow");
        return;
    }

    if (!element.isObject()) {
        addIssue(reportIndex, "element must be an object");
        return;
    }

    const json::Value type = element["type"];
    if (!type.isString()) {
        addIssue(reportIndex, "element requires a string 'type'");
        return;
    }

    const ElementType kind = elementTypeFromName(type.toString());
    if (kind == ElementType::Unknown) {
        addIssue(reportIndex, "unknown element type");
        return;
    }
    if (!isImplemented(kind)) {
        // Loud rather than silent: a scene asking for an icon should be told
        // icons do not exist yet, not quietly rendered without one.
        addIssue(reportIndex, "element type not implemented in this phase");
        return;
    }

    Rect rect;
    switch (kind) {
        case ElementType::Rect:
        case ElementType::Text:
        case ElementType::Progress:
        case ElementType::Graph:
        case ElementType::Group:
            if (!parseRect(element["rect"], rect)) {
                addIssue(reportIndex, "element requires 'rect' as [x, y, w, h]");
            }
            break;
        case ElementType::Pixel:
            if (!element["x"].isNumber() || !element["y"].isNumber()) {
                addIssue(reportIndex, "pixel requires numeric 'x' and 'y'");
            }
            break;
        case ElementType::Line:
            if (!element["x1"].isNumber() || !element["y1"].isNumber() ||
                !element["x2"].isNumber() || !element["y2"].isNumber()) {
                addIssue(reportIndex, "line requires numeric 'x1', 'y1', 'x2', 'y2'");
            }
            break;
        default:
            break;
    }

    if (kind == ElementType::Text) {
        const json::Value content = element["text"];
        if (!content.isString()) {
            addIssue(reportIndex, "text element requires a string 'text'");
        }
        // Scrolling text is the only thing that currently makes a scene
        // time-varying, and the frame scheduler needs to know before it decides
        // whether this scene can sit untouched between content changes.
        const json::Value scroll = element["scroll"];
        if (scroll.isString() &&
            text::scrollModeFromName(scroll.raw()) != text::ScrollMode::None) {
            animates_ = true;
        }
    }

    if (kind == ElementType::Graph && !element["values"].isArray()) {
        addIssue(reportIndex, "graph element requires a 'values' array");
    }

    if (kind == ElementType::Group) {
        const json::Value children = element["elements"];
        if (!children.isArray()) {
            addIssue(reportIndex, "group element requires an 'elements' array");
            return;
        }
        // Children are validated too. Previously a malformed element inside a
        // group was silently skipped at render time with no issue reported,
        // which made a broken nested scene look like a rendering bug.
        const int count = children.size();
        for (int i = 0; i < count; ++i) {
            validateElement(children[i], reportIndex, depth + 1);
        }
    }
}

void Scene::render(Canvas& canvas, std::uint64_t elapsedMillis) const {
    if (!loaded_) {
        return;
    }
    const json::Value elements = document_.root()["elements"];
    const int count = elements.size();
    for (int i = 0; i < count; ++i) {
        renderElement(canvas, elements[i], 0, elapsedMillis);
    }
}

void Scene::renderElement(Canvas& canvas,
                          const json::Value& element,
                          int depth,
                          std::uint64_t elapsedMillis) const {
    if (!element.isObject() || depth > kMaxGroupDepth) {
        return;
    }

    const ElementType kind = elementTypeFromName(element["type"].toString());
    if (!isImplemented(kind)) {
        return;
    }

    const Rgb color = colorOr(element["color"], colors::kWhite);

    switch (kind) {
        case ElementType::Pixel: {
            const json::Value x = element["x"];
            const json::Value y = element["y"];
            if (x.isNumber() && y.isNumber()) {
                canvas.pixel(clampToPanel(x.toInt()), clampToPanel(y.toInt()), color);
            }
            break;
        }

        case ElementType::Line: {
            const json::Value x1 = element["x1"];
            const json::Value y1 = element["y1"];
            const json::Value x2 = element["x2"];
            const json::Value y2 = element["y2"];
            if (x1.isNumber() && y1.isNumber() && x2.isNumber() && y2.isNumber()) {
                canvas.line(clampToPanel(x1.toInt()), clampToPanel(y1.toInt()),
                            clampToPanel(x2.toInt()), clampToPanel(y2.toInt()), color);
            }
            break;
        }

        case ElementType::Rect: {
            Rect rect;
            if (!parseRect(element["rect"], rect)) {
                break;
            }
            if (element["fill"].toBool(false)) {
                canvas.fillRect(rect, color);
            } else {
                canvas.rect(rect, color);
            }
            break;
        }

        case ElementType::Text: {
            Rect rect;
            if (!parseRect(element["rect"], rect)) {
                break;
            }
            text::TextStyle style;
            style.font = &text::font5x7();
            style.color = color;
            style.hAlign = horizontalAlign(element["align"]);
            style.vAlign = verticalAlign(element["valign"]);

            const json::Value spacing = element["spacing"];
            if (spacing.isNumber()) {
                style.letterSpacing = clampToPanel(spacing.toInt(1));
            }

            // Render straight from the document's bytes. toString() would
            // allocate on every frame for any text longer than a small-string
            // buffer — which is exactly the text long enough to need scrolling.
            // Only escaped text has to be materialised, and that is rare.
            const json::Value content = element["text"];
            const std::string_view raw = content.isString() ? content.raw() : std::string_view{};

            const json::Value scrollValue = element["scroll"];
            const text::ScrollMode scroll = text::scrollModeFromName(
                scrollValue.isString() ? scrollValue.raw() : std::string_view{});

            const auto render = [&](std::string_view text) {
                if (scroll == text::ScrollMode::None) {
                    text::draw(canvas, text, rect, style);
                } else {
                    text::drawScrolling(canvas, text, rect, style, scroll, elapsedMillis);
                }
            };

            if (raw.find('\\') == std::string_view::npos) {
                render(raw);
            } else {
                render(content.toString());
            }
            break;
        }

        case ElementType::Progress: {
            Rect rect;
            if (!parseRect(element["rect"], rect) || rect.empty()) {
                break;
            }

            const Rgb background = colorOr(element["background"], rgb(20, 20, 20));
            canvas.fillRect(rect, background);

            std::int64_t percent = element["value"].toInt(0);
            if (percent < 0) {
                percent = 0;
            }
            if (percent > 100) {
                percent = 100;
            }

            // Round rather than truncate so 99% is visibly short of full and
            // 100% is genuinely full.
            const int filled = static_cast<int>((percent * rect.w + 50) / 100);
            if (filled > 0) {
                canvas.fillRect(Rect{rect.x, rect.y, filled, rect.h}, color);
            }
            break;
        }

        case ElementType::Graph: {
            Rect rect;
            if (!parseRect(element["rect"], rect) || rect.empty()) {
                break;
            }

            const json::Value values = element["values"];
            const int count = values.size();
            if (count == 0) {
                break;
            }

            // Auto-scale to the data unless bounds are given: a fixed range
            // would flatten a graph of, say, indoor temperature.
            double lowest = values[0].toDouble(0.0);
            double highest = lowest;
            for (int i = 1; i < count; ++i) {
                const double sample = values[i].toDouble(0.0);
                lowest = sample < lowest ? sample : lowest;
                highest = sample > highest ? sample : highest;
            }
            if (element["min"].isNumber()) {
                lowest = element["min"].toDouble(lowest);
            }
            if (element["max"].isNumber()) {
                highest = element["max"].toDouble(highest);
            }

            const double span = highest - lowest;
            ClipScope scope(canvas, rect);

            // Newest sample on the right, oldest scrolled off the left edge.
            const int visible = count < rect.w ? count : rect.w;
            const int firstSample = count - visible;

            for (int i = 0; i < visible; ++i) {
                const double sample = values[firstSample + i].toDouble(lowest);
                const double normalised = span > 0.0 ? (sample - lowest) / span : 0.5;

                int height = static_cast<int>(normalised * rect.h + 0.5);
                if (height < 1) {
                    height = 1;  // a flat series should still be visible
                }
                if (height > rect.h) {
                    height = rect.h;
                }

                const int x = rect.right() - visible + i;
                canvas.vLine(x, rect.bottom() - height, height, color);
            }
            break;
        }

        case ElementType::Group: {
            Rect rect;
            if (!parseRect(element["rect"], rect)) {
                break;
            }
            // Children use panel coordinates and are clipped to the group, so a
            // group cannot scribble outside its own box however its contents are
            // positioned.
            ClipScope scope(canvas, rect);

            const json::Value children = element["elements"];
            const int count = children.size();
            for (int i = 0; i < count; ++i) {
                renderElement(canvas, children[i], depth + 1, elapsedMillis);
            }
            break;
        }

        default:
            break;
    }
}

}  // namespace scene
}  // namespace notrix
