// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace stipple {

class Canvas;

namespace script {

/// What the core needs from scripting, and nothing more.
///
/// The core cannot link Berry. ADR 0012 keeps `stipple_core` dependency-free
/// so it compiles unchanged for host, WASM and ARM, and a language runtime is
/// exactly the kind of thing that rule exists to keep out. So scripting sits
/// behind an interface for the same reason hardware does (blueprint §53): the
/// core knows that an app may be a script and that something can draw it, and
/// knows nothing about what.
///
/// A build with no scripting passes nothing. That is a supported
/// configuration, not a broken one - and per ADR 0013 the absence has to be
/// visible on the panel rather than showing as an app that silently draws
/// nothing.
class IScriptRunner {
public:
    virtual ~IScriptRunner() = default;

    /// Whether a script with this id exists at all.
    virtual bool has(std::string_view id) const noexcept = 0;

    /// Draw one frame. False when it is missing or has failed.
    virtual bool draw(std::string_view id, Canvas& canvas, std::uint64_t elapsedMillis) = 0;

    /// Why a script is not running, or an empty view when it is fine.
    ///
    /// The panel shows this. A script app that has stopped working should say
    /// so where its author can see it, rather than going black and leaving
    /// them to guess between a crash, an empty draw() and a dead device.
    virtual std::string_view problem(std::string_view id) const noexcept = 0;
};

}  // namespace script
}  // namespace stipple
