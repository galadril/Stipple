// SPDX-License-Identifier: GPL-3.0-or-later
//
// The eight kilobytes that stop a missing file from becoming a lockout.
//
// `/res/etc/EasyUI.cfg` names one library for the framework to load. Point
// that at STIPPLE in `/data` and a STIPPLE that will not load leaves a device
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
//     /data/stipple/libstipple.so loads   -> STIPPLE takes the process
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
// Which is the whole point: a broken STIPPLE degrades to a working clock on
// the network, and recovery is copying one file back rather than opening the
// case.

#include <dlfcn.h>

#include <cstdio>
#include <ctime>

namespace {

/// Tried in order, first one that loads wins.
///
/// 1. **An override in /data.** This is how a STIPPLE update lands without
///    flashing, and - more importantly - how it is rolled back: delete one
///    file and the device returns to the version that was flashed with it.
///
/// 2. **The copy flashed beside this shim.** Known-good, because it shipped
///    as one image with the shim that loads it, and read-only, because /res
///    is squashfs. A device can always reach this.
///
/// 3. Neither, and the stock clock runs.
///
/// **Note what is deliberately absent: `/data/stipple/libstipple.so`.** An
/// earlier design loaded exactly that and nothing else, which meant a stale
/// copy in /data silently shadowed a freshly flashed one - a correct image
/// would be flashed and then load the old broken code out of /data, with
/// nothing on the panel to say so. Seen on hardware, twice, and diagnosed
/// as a bad flash both times. The override path is spelled differently so
/// that cannot happen by accident: an override is something somebody put
/// there on purpose.
constexpr const char* kCandidates[] = {
    "/data/stipple/libstipple.so.override",
    "/res/lib/libstipple.so",
};

/// Kept on flash rather than in /tmp, because the one time anybody reads
/// this is after a boot that went wrong - and /tmp does not survive the
/// power cycle that usually follows.
constexpr const char* kJournal = "/data/stipple/startup.log";

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
    for (const char* const candidate : kCandidates) {
        // RTLD_GLOBAL so anything STIPPLE itself loads can resolve against
        // it, and RTLD_NOW so a STIPPLE with an unresolved symbol fails
        // *here* - where there is still a next candidate, and a stock clock
        // behind that - rather than half-way through running.
        void* stipple = ::dlopen(candidate, RTLD_NOW | RTLD_GLOBAL);
        if (stipple != nullptr) {
            // Unreachable in practice: STIPPLE takes the process in its own
            // constructor and does not come back. Reaching here means it
            // loaded and declined to start, which is worth recording and
            // worth carrying on from.
            note("loaded but returned, trying the next", candidate);
            continue;
        }

        // Only interesting for the override, and only worth a line when
        // somebody actually put one there. "No override present" is the
        // ordinary case and would be noise on every single boot.
        const char* why = ::dlerror();
        if (candidate != kCandidates[0]) {
            note("could not load", why);
        }
    }

    // Nothing loaded. The framework will dlsym this handle and find the
    // vendor application's entry points through the DT_NEEDED link, exactly
    // as if it had opened libzkgui.so itself - so the device is a working
    // clock on the network rather than a device nobody can reach.
    note("no stipple, starting the stock clock", nullptr);
}
