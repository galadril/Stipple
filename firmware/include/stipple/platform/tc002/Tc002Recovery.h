// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

#include "stipple/platform/PlatformServices.h"

namespace stipple {
namespace platform {
namespace tc002 {

/// Which image the TC002's recovery button would install, and switching it.
///
/// The vendor's loader installs `/mnt/storage/update.img` when the recovery
/// button is held during power-up, and `/bin/zkdaemon` installs the same file
/// unattended if the application never declares itself running. So that one
/// path decides what "reset" means on this device, and until now it meant
/// "reinstall Stipple" — see ADR 0026.
///
/// **Two images, not three.** `/mnt/storage` is the 8576 KB vfat volume on
/// mtd7. The Stipple image is about 3145 KB and a captured stock image about
/// 2724 KB, which together leave roughly 2700 KB spare — and a third copy
/// does not fit at all. So the armed image lives at `update.img`, the other
/// waits at `spare.img`, and switching renames rather than copies. On vfat a
/// rename is a directory-entry change: no data moves, nothing is read, and
/// 3 MB does not have to be rewritten onto flash that wears out.
///
/// **The window.** With two slots and no room for a third, a swap must pass
/// through a moment with no `update.img`. It is one rename wide. If power is
/// lost inside it the button is disarmed until `repair()` runs, which is why
/// `repair()` exists and why the startup path calls it.
class Tc002Recovery final : public IRecoveryImages {
public:
    /// What the recovery button and zkdaemon's auto-recovery both install.
    static constexpr const char* kArmedPath = "/mnt/storage/update.img";

    /// The image that is not armed, waiting its turn.
    static constexpr const char* kSparePath = "/mnt/storage/spare.img";

    /// Used only inside a swap. Present on boot means a swap was interrupted.
    static constexpr const char* kSwapPath = "/mnt/storage/swap.img";

    /// Which image is armed, and how big it was when we armed it.
    ///
    /// The size is recorded so the claim can be checked rather than trusted.
    /// Anybody can drop an `update.img` on this volume over USB — that is the
    /// documented way to install anything here — and a marker left from
    /// before would then describe a file that is no longer the one it names.
    /// A mismatch reports `Unrecognised`, which is the honest answer.
    static constexpr const char* kMarkerPath = "/mnt/storage/armed.txt";

    /// Where the volume is mounted, for free-space reporting.
    static constexpr const char* kVolumePath = "/mnt/storage";

    Image armed() const override;
    bool available(Image which) const override;
    bool arm(Image which, std::string& problem) override;
    std::size_t freeBytes() const override;

    /// Put an interrupted swap back together, if one was.
    ///
    /// Called from the startup path before anything depends on the button
    /// being armed. Returns true if it changed something, so the caller can
    /// log it — a device that silently repaired itself still had a power cut
    /// at a bad moment, and that is worth a line.
    bool repair();

    /// What `Image` a name means, for the marker and the API. Empty or
    /// unknown text is `Unrecognised` rather than a guess.
    static Image imageFromName(const std::string& name);

    /// The name for an `Image`, as written in the marker and the API.
    static const char* nameForImage(Image which);

private:
    /// Reads the marker, or `Unrecognised` when it is absent, unreadable or
    /// describes a file of the wrong size.
    Image markedImage() const;

    bool writeMarker(Image which, std::size_t bytes) const;
};

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
