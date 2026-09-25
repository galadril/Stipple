// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "stipple/core/Geometry.h"
#include "stipple/core/Rgb.h"
#include "stipple/asset/IconStore.h"
#include "stipple/json/Json.h"

namespace stipple {

class Canvas;

namespace scene {

/// Element kinds from blueprint §11.
///
/// `Sprite` and `Animation` are recognised but not yet implemented. They are
/// listed so a scene using them fails with a clear "not implemented" rather than
/// being silently dropped — blueprint §19's rule about documenting
/// incompatibilities instead of quietly accepting fields.
enum class ElementType {
    Unknown,
    Pixel,
    Line,
    Rect,
    Text,
    Progress,
    Graph,
    Group,
    Icon,
    Bitmap,
    Sprite,
    Animation,
};

ElementType elementTypeFromName(std::string_view name) noexcept;
const char* elementTypeName(ElementType type) noexcept;

/// True for kinds this phase can actually draw.
bool isImplemented(ElementType type) noexcept;

struct Issue {
    /// Index within the scene's `elements` array, or -1 for a document-level
    /// problem.
    int elementIndex = -1;
    const char* message = nullptr;
};

/// A parsed, validated scene.
///
/// Rendering walks the JSON document directly rather than building a second,
/// typed copy of it. The document is already a compact bounded structure, and
/// duplicating it would double the memory a scene costs for no gain (§38).
/// Validation runs once at load so errors surface when a scene is submitted,
/// not on the frame that happens to draw it.
///
/// The JSON text passed to `load` must outlive the Scene.
class Scene {
public:
    static constexpr int kMaxIssues = 8;
    /// Groups nest, so rendering is bounded independently of the JSON depth
    /// limit that got the document this far.
    static constexpr int kMaxGroupDepth = 8;

    Scene(json::Token* tokens, int capacity) noexcept : document_(tokens, capacity) {}

    /// Icons are looked up here by name. Without a store, `icon` elements report
    /// an issue rather than rendering nothing silently.
    void setIconStore(const asset::IconStore* icons) noexcept { icons_ = icons; }
    const asset::IconStore* iconStore() const noexcept { return icons_; }

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    /// Parse and validate. Returns false if the scene cannot be rendered at all;
    /// individual bad elements are reported as issues but do not fail the load,
    /// so one malformed element cannot blank an otherwise working screen.
    bool load(std::string_view json, const json::Limits& limits = json::Limits{});

    bool loaded() const noexcept { return loaded_; }
    json::Error parseError() const noexcept { return document_.error(); }
    std::size_t parseErrorOffset() const noexcept { return document_.errorOffset(); }

    /// Validation problems, capped at kMaxIssues. `issueOverflow()` reports
    /// whether more were found than could be stored.
    int issueCount() const noexcept { return issueCount_; }
    const Issue& issueAt(int index) const noexcept;
    bool issueOverflow() const noexcept { return issueOverflow_; }

    std::string_view name() const noexcept { return name_; }

    /// Display time in seconds. Zero means "use the app default".
    int durationSeconds() const noexcept { return durationSeconds_; }

    int elementCount() const noexcept;

    /// Does this scene change over time?
    ///
    /// True when any element scrolls (and, later, animates). The frame scheduler
    /// uses this to decide whether the scene must be redrawn every frame or can
    /// sit untouched until its content changes — the difference between a static
    /// clock face costing ~0 CPU and costing 30 renders a second.
    bool animates() const noexcept { return animates_; }

    /// Draw every renderable element, in document order. Elements that failed
    /// validation are skipped.
    ///
    /// `elapsedMillis` drives time-varying elements — currently scrolling text.
    /// It is passed in rather than read from a clock so a scene renders
    /// identically in a test, the emulator and on the device.
    void render(Canvas& canvas, std::uint64_t elapsedMillis = 0) const;

private:
    void addIssue(int elementIndex, const char* message) noexcept;
    void validate();
    /// `reportIndex` is the top-level element this belongs to, so an issue
    /// inside a nested group still points somewhere the caller can find.
    void validateElement(const json::Value& element, int reportIndex, int depth);
    void renderElement(Canvas& canvas,
                       const json::Value& element,
                       int depth,
                       std::uint64_t elapsedMillis) const;

    json::Document document_;
    const asset::IconStore* icons_ = nullptr;
    bool loaded_ = false;
    std::string_view name_;
    int durationSeconds_ = 0;
    Issue issues_[kMaxIssues];
    int issueCount_ = 0;
    bool issueOverflow_ = false;
    bool animates_ = false;
};

// --- shared field parsing ----------------------------------------------------

/// Accepts `"#rrggbb"`, `"rrggbb"`, `[r, g, b]`, or a packed number `0xRRGGBB`.
/// Several forms exist because integrations differ: Home Assistant templates
/// emit hex strings, while hand-written scenes are usually clearer as arrays.
bool parseColor(const json::Value& value, Rgb& out) noexcept;

/// Accepts `[x, y, w, h]`.
bool parseRect(const json::Value& value, Rect& out) noexcept;

}  // namespace scene
}  // namespace stipple
