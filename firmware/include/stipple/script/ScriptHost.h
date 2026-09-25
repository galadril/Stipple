// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace stipple {

class Canvas;

namespace script {

/// Runs one Berry script against the panel.
///
/// A script is a Berry class with a `draw()` method, the shape AWTRIX NG
/// documents and the shape existing scripts are written in. The host compiles
/// the source, constructs the class once, and calls `draw()` on the frames it
/// is on screen for.
///
/// **The script is untrusted.** It arrives over the network from whoever can
/// reach the device, and it runs on the thread that draws the panel. Three
/// things follow, and none of them are optional:
///
///   - The interpreter is built without a filesystem, a dynamic loader or a
///     bytecode reader (firmware/script/berry_conf.h), so the only things a
///     script can reach are the builtins registered here.
///   - Every call is bounded by an instruction budget. A script that loops
///     for ever loses its frame; the device keeps rendering.
///   - A script that fails is disabled rather than retried every frame. A
///     broken script that throws thirty times a second would fill the log and
///     starve everything else of time.
class ScriptHost {
public:
    /// Instructions a single call may spend.
    ///
    /// Generous for anything drawing a 52x16 panel and far short of a frame's
    /// worth of time on this hardware, which is the balance wanted: an honest
    /// script should never meet it, and a runaway one should meet it quickly.
    static constexpr std::uint32_t kInstructionBudget = 200000;

    /// Longest source a script may have. Bounded like everything else here -
    /// this arrives over the network and is held in RAM.
    static constexpr std::size_t kMaxSourceBytes = 16u * 1024u;

    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    /// Compile and instantiate. False leaves `problem` describing why, with a
    /// line number where Berry gave one - a compile error without one is
    /// useless in the editor that just showed the author their mistake.
    bool load(std::string_view source, std::string& problem);

    /// Run the script's `draw()` for one frame.
    ///
    /// False means the script failed and has been disabled; `problem` says
    /// what happened. The canvas may have been partly drawn to, which is
    /// deliberate - a script that dies half way through has still told you
    /// something, and blanking it would hide the evidence.
    bool draw(Canvas& canvas, std::uint64_t elapsedMillis, std::string& problem);

    /// Whether a script is loaded and has not failed.
    bool ready() const noexcept { return ready_; }

    /// Instructions the last call spent, for the diagnostics the web UI
    /// shows. A script that is close to the budget is one about to break on a
    /// slower frame.
    std::uint32_t lastInstructions() const noexcept { return lastInstructions_; }

private:
    struct State;
    State* state_;
    bool ready_ = false;
    std::uint32_t lastInstructions_ = 0;
};

}  // namespace script
}  // namespace stipple
