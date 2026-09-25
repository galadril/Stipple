// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/script/ScriptHost.h"

extern "C" {
#include "berry.h"
}

#include "stipple/core/Rgb.h"
#include "stipple/graphics/Canvas.h"
#include "stipple/graphics/Framebuffer.h"
#include "stipple/text/Text.h"

namespace stipple {
namespace script {
namespace {

/// The script currently drawing.
///
/// A file-static rather than something carried through Berry, because Berry's
/// native functions receive only a `bvm*` and the render loop is single
/// threaded - there is exactly one script drawing at any moment, and it is
/// this one. Set for the duration of a call and cleared after, so a builtin
/// reached any other way finds nothing and does nothing.
struct Active {
    Canvas* canvas = nullptr;
    std::uint64_t elapsedMillis = 0;
    std::uint32_t heartbeats = 0;
    bool overBudget = false;
};

Active g_active;

/// The instruction budget.
///
/// Berry calls this every 2^BE_VM_OBSERVABILITY_SAMPLING instructions. Raising
/// from here unwinds the script the same way any other error does, which is
/// what the hook is for - a runaway loop loses its frame and the panel keeps
/// rendering.
void observe(bvm* vm, int event, ...) {
    if (event != BE_OBS_VM_HEARTBEAT) {
        return;
    }
    // Sampling is 2^16, so each heartbeat is 65536 instructions.
    g_active.heartbeats += 1;
    const std::uint32_t spent = g_active.heartbeats * 65536u;
    if (spent >= ScriptHost::kInstructionBudget) {
        g_active.overBudget = true;
        be_raise(vm, "stipple_budget", "the script ran too long for one frame");
    }
}

Rgb fromScriptColor(bint packed) {
    const std::uint32_t value = static_cast<std::uint32_t>(packed);
    return rgb(static_cast<std::uint8_t>((value >> 16) & 0xFFu),
               static_cast<std::uint8_t>((value >> 8) & 0xFFu),
               static_cast<std::uint8_t>(value & 0xFFu));
}

int argInt(bvm* vm, int index, int fallback = 0) {
    if (be_isint(vm, index)) {
        return static_cast<int>(be_toint(vm, index));
    }
    if (be_isreal(vm, index)) {
        return static_cast<int>(be_toreal(vm, index));
    }
    return fallback;
}

/* --- the builtins --------------------------------------------------------
 *
 * Named to match AWTRIX NG's documented scripting API, so a script written
 * against that runs here unchanged. The names are an interface reimplemented
 * from its specification; no AWTRIX source was read.
 */

int b_width(bvm* vm) {
    be_pushint(vm, Framebuffer::kWidth);
    be_return(vm);
}

int b_height(bvm* vm) {
    be_pushint(vm, Framebuffer::kHeight);
    be_return(vm);
}

int b_rgb(bvm* vm) {
    const int top = be_top(vm);
    const int r = top >= 1 ? argInt(vm, 1) : 0;
    const int g = top >= 2 ? argInt(vm, 2) : 0;
    const int b = top >= 3 ? argInt(vm, 3) : 0;
    const auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    be_pushint(vm, (clamp8(r) << 16) | (clamp8(g) << 8) | clamp8(b));
    be_return(vm);
}

int b_clear(bvm* vm) {
    if (g_active.canvas != nullptr) {
        const Rgb colour = be_top(vm) >= 1 ? fromScriptColor(be_toint(vm, 1)) : colors::kBlack;
        g_active.canvas->clear(colour);
    }
    be_return_nil(vm);
}

int b_pixel(bvm* vm) {
    if (g_active.canvas != nullptr && be_top(vm) >= 3) {
        // The canvas clips, so coordinates from a script are bounded by the
        // panel rather than trusted.
        g_active.canvas->pixel(argInt(vm, 1), argInt(vm, 2), fromScriptColor(be_toint(vm, 3)));
    }
    be_return_nil(vm);
}

int b_line(bvm* vm) {
    if (g_active.canvas != nullptr && be_top(vm) >= 5) {
        g_active.canvas->line(argInt(vm, 1), argInt(vm, 2), argInt(vm, 3), argInt(vm, 4),
                              fromScriptColor(be_toint(vm, 5)));
    }
    be_return_nil(vm);
}

int b_rect(bvm* vm) {
    if (g_active.canvas != nullptr && be_top(vm) >= 5) {
        g_active.canvas->rect(Rect{argInt(vm, 1), argInt(vm, 2), argInt(vm, 3), argInt(vm, 4)},
                              fromScriptColor(be_toint(vm, 5)));
    }
    be_return_nil(vm);
}

int b_rect_fill(bvm* vm) {
    if (g_active.canvas != nullptr && be_top(vm) >= 5) {
        g_active.canvas->fillRect(
            Rect{argInt(vm, 1), argInt(vm, 2), argInt(vm, 3), argInt(vm, 4)},
            fromScriptColor(be_toint(vm, 5)));
    }
    be_return_nil(vm);
}

int b_text(bvm* vm) {
    int advance = 0;
    if (g_active.canvas != nullptr && be_top(vm) >= 3 && be_isstring(vm, 3)) {
        const char* body = be_tostring(vm, 3);
        const Rgb colour =
            be_top(vm) >= 4 ? fromScriptColor(be_toint(vm, 4)) : colors::kWhite;
        const int x = argInt(vm, 1);
        const int y = argInt(vm, 2);
        text::drawLine(*g_active.canvas, body, x, y, text::font5x7(), colour);
        advance = text::measureLine(body, text::font5x7());
    }
    // AWTRIX NG's text() returns the pixel advance, which scripts use to lay
    // the next thing out.
    be_pushint(vm, advance);
    be_return(vm);
}

int b_text_width(bvm* vm) {
    int width = 0;
    if (be_top(vm) >= 1 && be_isstring(vm, 1)) {
        width = text::measureLine(be_tostring(vm, 1), text::font5x7());
    }
    be_pushint(vm, width);
    be_return(vm);
}

int b_now_ms(bvm* vm) {
    be_pushint(vm, static_cast<bint>(g_active.elapsedMillis));
    be_return(vm);
}

void registerBuiltins(bvm* vm) {
    be_regfunc(vm, "width", b_width);
    be_regfunc(vm, "height", b_height);
    be_regfunc(vm, "rgb", b_rgb);
    be_regfunc(vm, "clear", b_clear);
    be_regfunc(vm, "pixel", b_pixel);
    be_regfunc(vm, "line", b_line);
    be_regfunc(vm, "rect", b_rect);
    be_regfunc(vm, "rect_fill", b_rect_fill);
    be_regfunc(vm, "text", b_text);
    be_regfunc(vm, "text_width", b_text_width);
    be_regfunc(vm, "text_ink_width", b_text_width);
    be_regfunc(vm, "now_ms", b_now_ms);
}

/// The global the instance is stashed under.
///
/// Deliberately a name a script is unlikely to choose. It is not a security
/// boundary - a script could overwrite it - but overwriting it only breaks
/// that script, which is its own business.
constexpr const char* kInstance = "_stipple_app";

}  // namespace

struct ScriptHost::State {
    bvm* vm = nullptr;
};

ScriptHost::ScriptHost() : state_(new State()) {
    state_->vm = be_vm_new();
    if (state_->vm != nullptr) {
        be_set_obs_hook(state_->vm, observe);
        registerBuiltins(state_->vm);
    }
}

ScriptHost::~ScriptHost() {
    if (state_ != nullptr) {
        if (state_->vm != nullptr) {
            be_vm_delete(state_->vm);
        }
        delete state_;
    }
}

bool ScriptHost::load(std::string_view source, std::string& problem) {
    problem.clear();
    ready_ = false;

    if (state_ == nullptr || state_->vm == nullptr) {
        problem = "the script engine is unavailable";
        return false;
    }
    if (source.empty()) {
        problem = "the script is empty";
        return false;
    }
    if (source.size() > kMaxSourceBytes) {
        problem = "the script is too long";
        return false;
    }

    bvm* vm = state_->vm;

    // Compiling is bounded too. A pathological source can make a parser work
    // hard before it produces anything, and this one runs on the thread that
    // draws the panel.
    g_active = Active{};

    if (be_loadbuffer(vm, "app", source.data(), source.size()) != 0) {
        // Berry leaves the message on the stack, and it carries the line
        // number - which is the whole value of it to somebody in the editor.
        problem = be_isstring(vm, -1) ? be_tostring(vm, -1) : "the script would not compile";
        be_pop(vm, 1);
        return false;
    }

    // A script is a chunk that returns its instance, which is the shape
    // AWTRIX NG documents and the shape existing scripts are written in.
    if (be_pcall(vm, 0) != 0) {
        problem = be_isstring(vm, -1) ? be_tostring(vm, -1) : "the script failed while loading";
        be_pop(vm, 2);
        return false;
    }

    if (be_isnil(vm, -1)) {
        problem = "the script did not return an app instance";
        be_pop(vm, 1);
        return false;
    }

    be_setglobal(vm, kInstance);
    be_pop(vm, 1);

    ready_ = true;
    return true;
}

bool ScriptHost::draw(Canvas& canvas, std::uint64_t elapsedMillis, std::string& problem) {
    problem.clear();
    if (!ready_ || state_ == nullptr || state_->vm == nullptr) {
        problem = "no script is loaded";
        return false;
    }

    bvm* vm = state_->vm;

    g_active.canvas = &canvas;
    g_active.elapsedMillis = elapsedMillis;
    g_active.heartbeats = 0;
    g_active.overBudget = false;

    bool ok = false;
    if (be_getglobal(vm, kInstance)) {
        if (be_getmethod(vm, -1, "draw")) {
            // The instance is the receiver, so it moves above the method.
            be_pushvalue(vm, -2);
            if (be_pcall(vm, 1) == 0) {
                ok = true;
                be_pop(vm, 2);
            } else {
                problem = be_isstring(vm, -1) ? be_tostring(vm, -1)
                                              : "the script failed while drawing";
                be_pop(vm, 3);
            }
        } else {
            problem = "the script has no draw() method";
            be_pop(vm, 1);
        }
    } else {
        problem = "the script instance is gone";
    }

    lastInstructions_ = g_active.heartbeats * 65536u;
    if (g_active.overBudget && problem.empty()) {
        problem = "the script ran too long for one frame";
    }

    g_active.canvas = nullptr;

    if (!ok) {
        // Disabled rather than retried. A script that throws thirty times a
        // second fills the log and starves everything else of time, and the
        // author needs to see the first error rather than the ten thousandth.
        ready_ = false;
    }
    return ok;
}

}  // namespace script
}  // namespace stipple
