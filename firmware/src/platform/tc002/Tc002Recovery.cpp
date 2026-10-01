// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/Tc002Recovery.h"

#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace stipple {
namespace platform {
namespace tc002 {
namespace {

/// Run a program and wait for it. True only on a clean exit status 0.
///
/// fork/exec rather than system(3): there is no shell worth invoking here -
/// the device busybox is missing enough builtins that depending on one is a
/// liability - and this way nothing is quoted or interpreted.
bool run(const char* const argv[]) {
    const pid_t pid = ::fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        ::execv(argv[0], const_cast<char* const*>(argv));
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/// Remount the image volume writable, or back again.
///
/// **It is mounted read-only, and that is the vendor's choice rather than an
/// accident.** Measured on hardware:
///
///     /dev/block/mtdblock7 on /mnt/storage type vfat
///         (ro,relatime,...,errors=remount-ro)
///
/// Which makes sense: the same partition is exposed to a computer as USB mass
/// storage, so anything could be writing it from the other side, and vfat
/// has no journal to recover from two writers or an unclean unmount.
///
/// This cost an assumption. The first version of this file renamed files
/// straight away and every write failed with EROFS - found by pushing an
/// image to a real device, not by reading code. So the volume is remounted
/// for exactly as long as the renames take and then put back, because leaving
/// a vfat volume writable on a device that can lose power at any moment is
/// how a recovery image becomes unreadable.
bool remountImages(bool writable) {
    const char* const rw[] = {"/bin/mount", "-o", "remount,rw", Tc002Recovery::kVolumePath,
                              nullptr};
    const char* const ro[] = {"/bin/mount", "-o", "remount,ro", Tc002Recovery::kVolumePath,
                              nullptr};
    return run(writable ? rw : ro);
}

/// Remounts writable for a scope and always puts it back.
///
/// A failure path that forgot the restore would leave the volume writable
/// until the next boot, which is the state this is careful to avoid.
class WritableImages {
public:
    WritableImages() : ok_(remountImages(true)) {}
    ~WritableImages() {
        if (ok_) {
            // Flush before dropping write access, or the rename may still be
            // in the page cache when power goes.
            ::sync();
            remountImages(false);
        }
    }

    WritableImages(const WritableImages&) = delete;
    WritableImages& operator=(const WritableImages&) = delete;

    bool ok() const noexcept { return ok_; }

private:
    bool ok_;
};

/// Size of a file, or 0 when it is not there. Zero doubles as absent on
/// purpose: a zero-length image is as useless as a missing one, and the
/// loader would refuse it either way.
std::size_t sizeOf(const char* path) {
    struct stat info;
    if (::stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        return 0;
    }
    return static_cast<std::size_t>(info.st_size);
}

bool exists(const char* path) { return sizeOf(path) > 0; }

}  // namespace

IRecoveryImages::Image Tc002Recovery::imageFromName(const std::string& name) {
    if (name == "stock") {
        return Image::Stock;
    }
    if (name == "stipple") {
        return Image::Stipple;
    }
    if (name == "nothing") {
        return Image::Nothing;
    }
    return Image::Unrecognised;
}

const char* Tc002Recovery::nameForImage(Image which) {
    switch (which) {
        case Image::Stock:
            return "stock";
        case Image::Stipple:
            return "stipple";
        case Image::Nothing:
            return "nothing";
        case Image::Unrecognised:
            break;
    }
    return "unrecognised";
}

bool Tc002Recovery::writeMarker(Image which, std::size_t bytes) const {
    FILE* file = std::fopen(kMarkerPath, "w");
    if (file == nullptr) {
        return false;
    }
    // Name and size together, because the name alone is a claim and the size
    // is what makes it checkable.
    const int wrote = std::fprintf(file, "%s %lu\n", nameForImage(which),
                                   static_cast<unsigned long>(bytes));
    const bool ok = wrote > 0 && std::fclose(file) == 0;
    return ok;
}

IRecoveryImages::Image Tc002Recovery::markedImage() const {
    FILE* file = std::fopen(kMarkerPath, "r");
    if (file == nullptr) {
        return Image::Unrecognised;
    }
    char name[32] = {0};
    unsigned long bytes = 0;
    const bool read = std::fscanf(file, "%31s %lu", name, &bytes) == 2;
    std::fclose(file);
    if (!read) {
        return Image::Unrecognised;
    }

    // The check that makes the marker worth keeping. A USB stick dropping a
    // new update.img is the documented way to install on this device, and it
    // knows nothing about this file - so a marker that still names the old
    // image would otherwise be believed.
    if (static_cast<std::size_t>(bytes) != sizeOf(kArmedPath)) {
        return Image::Unrecognised;
    }

    const Image named = imageFromName(std::string(name));
    // "nothing" in the marker alongside a file that exists is incoherent.
    return named == Image::Nothing ? Image::Unrecognised : named;
}

IRecoveryImages::Image Tc002Recovery::armed() const {
    if (!exists(kArmedPath)) {
        return Image::Nothing;
    }
    return markedImage();
}

bool Tc002Recovery::available(Image which) const {
    if (which != Image::Stock && which != Image::Stipple) {
        return false;
    }
    if (armed() == which) {
        return true;
    }
    // The spare is whichever one is not armed, so it is available exactly
    // when it exists and the armed image is recognised as the other.
    return exists(kSparePath) && armed() != Image::Unrecognised;
}

bool Tc002Recovery::repair() {
    // A swap leaves swap.img behind only if it was interrupted part way. Put
    // it wherever there is a hole, armed slot first: an armed button matters
    // more than a tidy spare, because the button is somebody's way back.
    if (!exists(kSwapPath)) {
        return false;  // The ordinary case: one stat, nothing to do.
    }

    // Only now, because the check above runs on every boot and remounting a
    // volume to discover there is nothing to do would be a poor trade.
    const WritableImages writable;
    if (!writable.ok()) {
        return false;
    }
    if (!exists(kArmedPath)) {
        if (::rename(kSwapPath, kArmedPath) == 0) {
            // The marker now describes a file that may not be this one, and
            // saying "unrecognised" is better than naming the wrong image.
            ::unlink(kMarkerPath);
            return true;
        }
        return false;
    }
    if (!exists(kSparePath)) {
        return ::rename(kSwapPath, kSparePath) == 0;
    }
    // Both slots full and a leftover besides: the swap finished and only the
    // cleanup did not. Dropping it reclaims three megabytes on an eight
    // megabyte volume, which is not a rounding error here.
    return ::unlink(kSwapPath) == 0;
}

bool Tc002Recovery::arm(Image which, std::string& problem) {
    problem.clear();

    if (which != Image::Stock && which != Image::Stipple) {
        problem = "that is not an image this device can arm";
        return false;
    }

    // Anything left from an interrupted swap is cleared first, or the renames
    // below would trip over it.
    repair();

    if (armed() == which) {
        return true;  // Idempotent on purpose.
    }

    if (!exists(kSparePath)) {
        problem = "the other image is not on this device";
        return false;
    }

    // An unrecognised armed image means somebody put it there and we do not
    // know what it is. Swapping would move it to the spare slot and claim the
    // spare is the image they asked for - which may be true and may not.
    if (armed() == Image::Unrecognised && exists(kArmedPath)) {
        problem =
            "the armed image is not one this device put there, so it will not "
            "be moved; reinstall from a USB stick instead";
        return false;
    }

    // Writable only from here, and put back by the destructor on every path
    // out - including the failure returns below.
    const WritableImages writable;
    if (!writable.ok()) {
        problem = "the image volume could not be made writable";
        return false;
    }

    // Three renames, and the order is the whole of the safety argument.
    //
    // The spare is moved aside first so that the armed slot is emptied as
    // late as possible, and refilled on the very next operation. Between
    // steps 2 and 3 there is no update.img and the button does nothing: one
    // rename wide, metadata only, and repair() puts it right if power is lost
    // inside it. With two slots and no room for a third this window cannot be
    // removed, only made small and recoverable.
    if (::rename(kSparePath, kSwapPath) != 0) {
        problem = "could not move the other image aside";
        return false;
    }
    if (::rename(kArmedPath, kSparePath) != 0) {
        problem = "could not set the armed image aside";
        ::rename(kSwapPath, kSparePath);  // Back as it was.
        return false;
    }
    if (::rename(kSwapPath, kArmedPath) != 0) {
        problem = "could not arm the image";
        // Leaves the armed slot empty, which repair() fixes - and it is
        // called at startup precisely so this cannot persist unnoticed.
        ::rename(kSparePath, kArmedPath);
        return false;
    }

    // Written last. A marker is a claim about a file, so it must not exist
    // before the file it describes - and if this is the step that is lost,
    // armed() reports Unrecognised rather than something false.
    if (!writeMarker(which, sizeOf(kArmedPath))) {
        problem = "the image is armed but could not be recorded";
        return false;
    }
    return true;
}

std::size_t Tc002Recovery::freeBytes() const {
    struct statvfs info;
    if (::statvfs(kVolumePath, &info) != 0) {
        return 0;
    }
    return static_cast<std::size_t>(info.f_bavail) * static_cast<std::size_t>(info.f_frsize);
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
