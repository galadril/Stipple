// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace stipple {

class Canvas;

namespace script {

/// One script, as the rest of the firmware sees it.
///
/// No Berry types here on purpose. This struct crosses into the core, which
/// cannot link an interpreter, so it carries text and numbers and nothing
/// else.
struct Script {
    std::string id;
    std::string name;
    std::string source;

    /// Whether it compiled and has not since failed.
    bool ok = false;

    /// Why it is not ok. Empty when it is.
    ///
    /// Kept rather than only logged, because this is the one piece of
    /// information the author is actually waiting for. The editor shows it
    /// next to the code that caused it.
    std::string problem;

    /// Instructions the last frame spent. A script close to the budget is one
    /// about to break on a busier frame, and that is worth seeing before it
    /// does.
    std::uint32_t lastInstructions = 0;

    /// Bytes its interpreter is holding.
    std::size_t memoryBytes = 0;
};

enum class ScriptPutResult : std::uint8_t {
    Added,
    Replaced,
    InvalidId,
    SourceTooLarge,
    TooManyScripts,
    /// Stored, but it does not run. Deliberately not an error: the source is
    /// saved and `problem` says what is wrong, so the author can fix it in
    /// place rather than losing it to a missing `end`.
    DidNotCompile,
};

const char* describeScriptPut(ScriptPutResult result) noexcept;

/// What the rest of the firmware needs from scripting, and nothing more.
///
/// The core cannot link Berry. ADR 0012 keeps `stipple_core` dependency-free
/// so it compiles unchanged for host, WASM and ARM, and a language runtime is
/// exactly the kind of thing that rule exists to keep out. So scripting sits
/// behind an interface for the same reason hardware does (blueprint §53): the
/// core knows that an app may be a script, that something can draw it and that
/// the API can edit it, and knows nothing about what runs it.
///
/// A build with no scripting passes nothing. That is a supported
/// configuration, not a broken one - and per ADR 0013 the absence has to be
/// visible on the panel and honest over the API, rather than showing as an app
/// that silently draws nothing.
class IScriptRunner {
public:
    virtual ~IScriptRunner() = default;

    // --- rendering -----------------------------------------------------------

    /// Whether a script with this id exists at all.
    virtual bool has(std::string_view id) const noexcept = 0;

    /// Draw one frame. False when it is missing or has failed.
    virtual bool draw(std::string_view id, Canvas& canvas, std::uint64_t elapsedMillis) = 0;

    /// Offer a button press to a script.
    ///
    /// True when the script has an `on_button(name)` and it ran. False when it
    /// has none, so the press falls through to whatever it would normally have
    /// done - a script that does not want the button must not swallow it.
    ///
    /// Only the action button is offered, never the knob. The knob is how
    /// somebody moves between apps, and a script that took it would be a
    /// script you could not leave. The stopwatch made the same call for the
    /// same reason.
    virtual bool button(std::string_view id, std::string_view name) = 0;

    /// Why a script is not running, or an empty view when it is fine.
    ///
    /// The panel shows that this is non-empty; the API and the web UI show
    /// what it says. A script app that has stopped working should say so where
    /// its author can see it, rather than going black and leaving them to
    /// guess between a crash, an empty draw() and a dead device.
    virtual std::string_view problem(std::string_view id) const noexcept = 0;

    // --- editing -------------------------------------------------------------

    virtual ScriptPutResult put(std::string id, std::string name, std::string source) = 0;
    virtual bool remove(std::string_view id) = 0;
    virtual void clear() = 0;

    virtual int count() const noexcept = 0;
    virtual int capacity() const noexcept = 0;
    virtual const Script* at(int index) const noexcept = 0;
    virtual const Script* find(std::string_view id) const noexcept = 0;

    /// Total bytes every interpreter is holding, for diagnostics.
    virtual std::size_t memoryBytes() const noexcept = 0;

    /// Longest source the runner will accept, so the editor can say so before
    /// somebody pastes something too big and loses it.
    virtual std::size_t maxSourceBytes() const noexcept = 0;

    // --- persistence ---------------------------------------------------------

    /// Bumped by anything that changes the library.
    ///
    /// The host writes to storage only when this moves. Scripts live on flash
    /// and flash wears out; rewriting the whole library on every tick because
    /// nothing has changed would be a slow way to destroy the device.
    virtual std::uint32_t revision() const noexcept = 0;

    /// The whole library as one blob, and back again.
    ///
    /// deserialize() returns false on anything it does not recognise. The
    /// caller discards the blob when it does - re-reading the same broken
    /// bytes every boot turns one bad write into a permanent fault that looks
    /// intermittent.
    virtual std::string serialize() const = 0;
    virtual bool deserialize(std::string_view blob) = 0;
};

}  // namespace script
}  // namespace stipple
