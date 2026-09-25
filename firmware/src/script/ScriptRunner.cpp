// SPDX-License-Identifier: GPL-3.0-or-later
//
// The part of scripting that belongs to the core.
//
// Nothing here knows what an interpreter is. It is in stipple_core rather than
// stipple_script because the API server needs these words and the API server
// cannot link Berry - see ADR 0012 and the note in IScriptRunner.h.
#include "stipple/script/IScriptRunner.h"

namespace stipple {
namespace script {

const char* describeScriptPut(ScriptPutResult result) noexcept {
    switch (result) {
        case ScriptPutResult::Added: return "added";
        case ScriptPutResult::Replaced: return "replaced";
        case ScriptPutResult::InvalidId:
            return "a script id may only contain lowercase letters, digits, dashes and underscores";
        case ScriptPutResult::SourceTooLarge: return "the script is too long";
        case ScriptPutResult::TooManyScripts: return "there is no room for another script";
        case ScriptPutResult::DidNotCompile: return "saved, but it does not compile";
    }
    return "unknown";
}

}  // namespace script
}  // namespace stipple
