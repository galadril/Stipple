// SPDX-License-Identifier: GPL-3.0-or-later
//
// Four symbols, to find out whether STIPPLE can be the thing /bin/zkgui runs.
//
// The vendor launcher is nine kilobytes and imports exactly four functions
// from libeasyui.so:
//
//     EasyUIContext::getInstance()
//     EasyUIContext::initEasyUI()
//     EasyUIContext::runEasyUI()
//     EasyUIContext::deinitEasyUI()
//
// Nothing else in its dependency set needs that library - libzkhardware.so
// and the SigmaStar libraries stand on their own. So a replacement providing
// those four is enough to satisfy the dynamic linker, and runEasyUI() never
// has to return.
//
// **This is why that matters.** The alternative was becoming
// /res/lib/libzkgui.so, the 7.5 MB application the framework dlopens. That
// route means implementing against EasyUI's own C++ ABI, and the three
// symbol names it looks up are obfuscated in .data rather than stored as
// text - so even finding the contract is a reverse-engineering project, and
// meeting it would couple STIPPLE to a vendor framework in exactly the way
// blueprint section 53 exists to prevent.
//
// Shadowing libeasyui.so instead needs four symbols whose names are public,
// mangled in the ordinary way, and visible in the launcher's import table.
//
// And it is testable without touching flash, because init.rc exports
//
//     LD_LIBRARY_PATH /tmp:/res/lib:/lib
//
// with /tmp first. A shim dropped there shadows the real library for the
// next launch and is gone at the next power cycle.
//
// This file proves the hook and nothing else. It does not render, does not
// open the panel, and deliberately does not link STIPPLE: the question is
// whether our code runs at all, and mixing that with a display bring-up
// would make a failure ambiguous.

#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace {

/// Written where a power cycle cannot be blamed for losing it.
void note(const char* what) {
    FILE* log = std::fopen("/tmp/stipple-easyui-shim.log", "a");
    if (log == nullptr) {
        return;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    std::fprintf(log, "[%lu] %s\n", static_cast<unsigned long>(now.tv_sec), what);
    std::fclose(log);
}

}  // namespace

/// Declared, not defined against any vendor header.
///
/// Only the mangled names have to match, and those depend on the class name
/// and the parameter list - never on what is inside. `getInstance` is static
/// and the rest are ordinary members, which is what produces
/// _ZN13EasyUIContext11getInstanceEv and friends.
class EasyUIContext {
public:
    static EasyUIContext* getInstance();
    void initEasyUI();
    void runEasyUI();
    void deinitEasyUI();
};

EasyUIContext* EasyUIContext::getInstance() {
    static EasyUIContext instance;
    note("getInstance");
    return &instance;
}

void EasyUIContext::initEasyUI() { note("initEasyUI"); }

void EasyUIContext::runEasyUI() {
    note("runEasyUI - this is where STIPPLE would take over");

    // Returns, deliberately. The real one would never, but a shim that hung
    // would leave the launcher occupying the panel with no way to tell a
    // successful hook from a wedged one.
    //
    // Letting it return means zkgui exits cleanly, init does not respawn
    // anything unexpected, and the log file says exactly how far it got.
    for (int i = 0; i < 3; ++i) {
        note("  running");
        ::sleep(1);
    }
    note("runEasyUI returning");
}

void EasyUIContext::deinitEasyUI() { note("deinitEasyUI"); }
