// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "notrix/platform/PlatformServices.h"

namespace notrix {
namespace platform {
namespace tc002 {

/// Puts a firmware image where the TC002 loader looks for one.
///
/// `/mnt/storage` is the vfat partition on mtd7 — the volume that appears as
/// mass storage when the clock is plugged into a computer, and the directory
/// the loader checks for `update.img` when the reset button is held during
/// power-up. It is mounted read-only in normal operation, so writing means
/// remounting it, writing, and putting it back.
///
/// **Nothing here flashes anything.** It writes one file to a vfat volume.
/// The device does the rest, later, only if somebody holds a button — and
/// triggering an update from software is a separate thing this class
/// deliberately does not offer (ADR 0008).
///
/// The point is narrower than flashing and more immediately useful: a TC002
/// ships a recovery image on that volume which is **not necessarily the
/// firmware it is running**. On the unit this was developed against, holding
/// reset installs an older one. Staging a captured image makes that button a
/// real recovery instead of a downgrade, and costs no flash writes at all.
class Tc002Upgrade final : public IUpgradeManager {
public:
    /// The vfat volume, and the name the loader looks for in it.
    static constexpr const char* kMountPoint = "/mnt/storage";
    static constexpr const char* kImagePath = "/mnt/storage/update.img";

    std::string stagingPath() const override { return kImagePath; }

    bool stage(std::string_view image, std::string& problem) override;

    std::size_t stagedBytes() const override;

private:
    /// Returns false if the mount would not change state. Both directions
    /// are attempted through `mount`, because this busybox has no applets
    /// and `/bin/mount` is the real thing.
    bool remount(bool writable) const;

    /// Written beside the target and renamed into place.
    ///
    /// vfat has no atomic rename guarantee worth leaning on, but the window
    /// is still the difference between "a few milliseconds" and "however
    /// long three megabytes takes over Wi-Fi". A recovery image that is half
    /// written is worse than one that is out of date, because it looks
    /// present.
    static constexpr const char* kTempPath = "/mnt/storage/update.img.part";
};

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
