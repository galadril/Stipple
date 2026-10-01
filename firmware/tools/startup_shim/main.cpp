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
// **That covers a STIPPLE that will not load. It did not cover a STIPPLE that
// loads and then fails** - a crash three frames in, a hang on storage, a
// script that throws at startup. `dlopen` returned a handle, so this file had
// already finished deciding, and the next boot repeated the identical
// failure for ever. A device in that loop is reachable by nothing at all.
//
// Hence the attempt count, and a ladder that spends the cheap remedies first:
//
//     3 unfinished boots  ->  safe mode: default settings, no stored apps
//                             (STIPPLE's own, in core - not this file)
//     5 unfinished boots  ->  ignore the override: roll back the update
//     8 unfinished boots  ->  the stock clock
//
// Every rung is only reached because the one below it did not help, and the
// count is cleared by STIPPLE on the same signal core uses to clear its own
// boot marker: frames are rendering.
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

/// How many boots in a row have begun without one of them finishing.
///
/// **The gap this closes.** `dlopen` failing is already survivable - the next
/// candidate gets a turn and the stock clock is behind both. What was not
/// survivable is STIPPLE loading *successfully* and then failing afterwards:
/// a crash on the third frame, a hang waiting on storage, an exception in a
/// script that runs at startup. `dlopen` returned a handle, so this shim was
/// already finished and had nothing left to decide, and every subsequent boot
/// repeated the identical failure. A device in that loop is reachable by
/// nothing: no application means no DHCP, no ADB and no USB gadget.
///
/// So the count is written *before* the attempt and cleared by STIPPLE only
/// once it has actually been running for a while. A boot that dies at any
/// point - including one that takes the panel with it - leaves the count
/// incremented, which is the only way the next boot can know.
constexpr const char* kAttempts = "/data/stipple/attempts";

/// What this shim can do, so the application can tell.
///
/// **The shim cannot be updated over the air.** It lives in `/res`, which is
/// read-only squashfs, and the firmware upload replaces only
/// `/data/stipple/libstipple.so.override`. So the pair can be mismatched in
/// one direction for ever: a device flashed long ago runs the newest STIPPLE
/// over the oldest shim, and nothing in the version number says so.
///
/// That matters because features arrive here, not just there. The boot ladder
/// below is useless if the shim predates it, and an application that assumed
/// otherwise would promise a safety net that is not strung up.
///
/// So the shim states its own capability, and absence is as meaningful as any
/// value: no file means a shim from before this existed. The application
/// reports it, and the web UI can say "this update needs a reflash, not an
/// upload" instead of letting somebody find out the hard way.
///
/// Bump when the shim gains something an application can depend on.
///
///     (absent) - predates this marker; no boot ladder
///     1        - attempt counting and the 5/8 fallback ladder
constexpr const char* kFeatures = "/data/stipple/shim";
constexpr unsigned kFeatureLevel = 1;

/// Rewritten on every boot rather than created once, so a downgrade is
/// noticed too - flashing an older image back must not leave the newer
/// claim standing.
void declareFeatures() {
    FILE* file = std::fopen(kFeatures, "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "%u\n", kFeatureLevel);
    std::fclose(file);
}

/// Skip the override at five, everything at eight.
///
/// **These sit deliberately above `HostConfig::safeModeThreshold`, which is
/// three.** STIPPLE already protects itself one layer up: a boot marker in
/// storage, cleared once frames render, and three unfinished boots bring the
/// application up in safe mode with default settings and no stored apps. That
/// catches a poisonous config or a bad app without costing anybody anything.
///
/// If both ladders started at three they would fire on the same boot, and the
/// cheap remedy would be spent at the same moment as the expensive one - the
/// user would lose an over-the-air update to a fault that defaulting their
/// settings had already fixed, with no way to tell which had worked. So the
/// order is: safe mode first, rollback second, stock clock last. Each rung is
/// only reached because the one below it did not help.
///
///     3 unfinished boots  ->  STIPPLE's own safe mode (core, not here)
///     5 unfinished boots  ->  ignore the override: roll back the update
///     8 unfinished boots  ->  stock clock: give up on STIPPLE
///
/// The counts are not the same population. Core's marker needs STIPPLE to
/// have started at all, and a crash in a constructor never gets that far;
/// this file's count is written before anything is attempted and so sees
/// every boot, including the ones core cannot know about.
constexpr unsigned kSkipOverrideAt = 5;
constexpr unsigned kSkipEverythingAt = 8;

unsigned readAttempts() {
    FILE* file = std::fopen(kAttempts, "r");
    if (file == nullptr) {
        return 0;
    }
    unsigned count = 0;
    if (std::fscanf(file, "%u", &count) != 1) {
        count = 0;
    }
    std::fclose(file);
    // A file holding nonsense must not become a permanent refusal to start.
    return count > 1000u ? 0u : count;
}

/// Record the attempt about to be made.
///
/// Failure is deliberately ignored. If `/data` cannot be written then nothing
/// can escalate, and the choice is between trying STIPPLE anyway or refusing
/// to start it for ever on the strength of a counter we could not read. The
/// first is recoverable and the second is the lockout this whole file exists
/// to prevent.
void writeAttempts(unsigned count) {
    FILE* file = std::fopen(kAttempts, "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "%u\n", count);
    std::fclose(file);
}

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
    // Said before anything else, because an application that loads has to be
    // able to find out what loaded it even if this boot then fails.
    declareFeatures();

    // Counted before anything is attempted. A boot that never comes back has
    // to leave evidence behind it, and this is the only moment that is
    // guaranteed to run.
    const unsigned attempts = readAttempts();
    writeAttempts(attempts + 1);

    if (attempts >= kSkipEverythingAt) {
        // Giving up on STIPPLE, on purpose. Said in the journal in those
        // words so the next person reads a decision rather than a mystery.
        note("eight boots did not finish, starting the stock clock", nullptr);
        return;
    }

    const bool skipOverride = attempts >= kSkipOverrideAt;
    if (skipOverride) {
        note("five boots did not finish, ignoring the override", kCandidates[0]);
    }

    for (const char* const candidate : kCandidates) {
        if (skipOverride && candidate == kCandidates[0]) {
            continue;
        }
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
