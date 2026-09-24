// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "notrix/platform/PlatformServices.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// Installs a new NOTRIX by writing one file to /data.
///
/// ADR 0021 points the vendor framework at a shim in `/res`, and the shim
/// tries three things in order: an override in `/data`, the copy flashed
/// beside it, then the stock clock. This class writes the first of those, so
/// an update needs no flash, no reset button and no USB stick - and rolling
/// back is deleting a file.
///
/// **This deliberately no longer stages `update.img` on the USB volume.**
/// That was the original job, on the theory that it armed the reset button
/// with a known-good image. `/bin/zkdaemon` then turned out to install
/// whatever sits there on *auto* recovery - no button, no warning - and it
/// reverted a working NOTRIX on real hardware within a minute of boot. A
/// staged image is an armed revert. See
/// docs/research/tc002-platform-findings.md.
class Tc002Upgrade final : public IUpgradeManager {
public:
    /// Where the shim looks first. Matches kCandidates[0] in the shim, and
    /// the two must agree or an update installs somewhere nothing reads.
    static constexpr const char* kApplicationPath = "/data/notrix/libnotrix.so.override";

    /// The copy displaced by the last install, kept for rollback.
    static constexpr const char* kPreviousPath = "/data/notrix/libnotrix.so.previous";

    /// Written here first, then renamed over the target. A rename within one
    /// filesystem is atomic, so the shim never sees a partial library.
    static constexpr const char* kIncomingPath = "/data/notrix/libnotrix.so.incoming";

    std::string applicationPath() const override { return kApplicationPath; }

    std::size_t installedBytes() const override;

    bool hasPrevious() const override;

    bool install(std::string_view image, std::string& problem) override;

    bool rollback(std::string& problem) override;
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
