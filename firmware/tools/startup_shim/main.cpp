// SPDX-License-Identifier: GPL-3.0-or-later
//
// The eight kilobytes that stop a missing file from becoming a lockout.
//
// `/res/etc/EasyUI.cfg` names one library for the framework to load. Point
// that at NOTRIX in `/data` and a NOTRIX that will not load leaves a device
// with **no application at all** - and on this hardware that is not a mild
// failure. Nothing here obtains an IP address except the application, so no
// application means no DHCP, no ADB, and no USB gadget either, because the
// application is also what switches `otg_role`. A device in that state has
// no way in.
//
// That is not hypothetical. It happened, and the post-mortem is
// docs/adr/0008-installer-helper.md.
//
// So the config points here instead, and this decides:
//
//     /data/notrix/libnotrix.so loads   -> NOTRIX takes the process
//     it does not                       -> the stock clock runs
//
// **The trick is in the link line, not in this file.** This library lists
// libzkgui.so as an ordinary `DT_NEEDED` dependency. The framework calls
// `dlsym` on the handle it got back from `dlopen`, and `dlsym` searches a
// handle's whole dependency tree - so the vendor application's entry points
// are found straight through this shim. Their names are obfuscated in
// libeasyui's `.data` and it does not matter, because nothing here has to
// name them.
//
// Which is the whole point: a broken NOTRIX degrades to a working clock on
// the network, and recovery is copying one file back rather than opening the
// case.

#include <dlfcn.h>

#include <cstdio>
#include <ctime>

namespace {

/// Where NOTRIX lives. `/data` is the only writable persistent filesystem on
/// this device, which is what lets a release be a file copy rather than a
/// flash (ADR 0021).
constexpr const char* kNotrix = "/data/notrix/libnotrix.so";

/// Kept on flash rather than in /tmp, because the one time anybody reads
/// this is after a boot that went wrong - and /tmp does not survive the
/// power cycle that usually follows.
constexpr const char* kJournal = "/data/notrix/startup.log";

void note(const char* what, const char* detail) {
    FILE* journal = std::fopen(kJournal, "a");
    if (journal == nullptr) {
        return;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    std::fprintf(journal, "[%lu] %s%s%s\n", static_cast<unsigned long>(now.tv_sec), what,
                 detail != nullptr ? ": " : "", detail != nullptr ? detail : "");
    std::fclose(journal);
}

}  // namespace

/// Runs when the framework `dlopen`s this library, before it can look up a
/// single symbol.
__attribute__((constructor)) static void chooseApplication() {
    // RTLD_GLOBAL so anything NOTRIX itself loads can resolve against it,
    // and RTLD_NOW so a NOTRIX with an unresolved symbol fails *here* -
    // where there is still a stock clock to fall back to - rather than
    // half-way through running.
    void* notrix = ::dlopen(kNotrix, RTLD_NOW | RTLD_GLOBAL);

    if (notrix != nullptr) {
        // Unreachable in practice. NOTRIX takes the process in its own
        // constructor and does not come back, so returning here means it
        // loaded and then declined to start - worth recording, and worth
        // falling through to the vendor application rather than leaving the
        // device with nothing.
        note("notrix loaded but returned; falling back to the stock clock", nullptr);
        return;
    }

    // The interesting path. dlerror() says whether the file is missing, the
    // architecture is wrong, or a symbol did not resolve - all of which look
    // identical from the outside and are not.
    const char* why = ::dlerror();
    note("no notrix, starting the stock clock", why);

    // Nothing else to do. The framework will dlsym this handle and find the
    // vendor application's entry points through the DT_NEEDED link, exactly
    // as if it had opened libzkgui.so itself.
}
