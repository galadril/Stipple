// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/platform/tc002/Tc002Upgrade.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace notrix {
namespace platform {
namespace tc002 {
namespace {

/// Run a command and wait for it. Returns its exit status, or -1.
int run(const char* const argv[]) {
    const pid_t pid = ::fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        const int null = ::open("/dev/null", O_RDWR);
        if (null >= 0) {
            ::dup2(null, STDOUT_FILENO);
            ::dup2(null, STDERR_FILENO);
            if (null > STDERR_FILENO) {
                ::close(null);
            }
        }
        ::execv(argv[0], const_cast<char* const*>(argv));
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

}  // namespace

bool Tc002Upgrade::remount(bool writable) const {
    const char* const rw[] = {"/bin/mount", "-o", "remount,rw", kMountPoint, nullptr};
    const char* const ro[] = {"/bin/mount", "-o", "remount,ro", kMountPoint, nullptr};
    return run(writable ? rw : ro) == 0;
}

std::size_t Tc002Upgrade::stagedBytes() const {
    struct stat info;
    if (::stat(kImagePath, &info) != 0) {
        return 0;
    }
    return static_cast<std::size_t>(info.st_size);
}

bool Tc002Upgrade::stage(std::string_view image, std::string& problem) {
    problem.clear();

    if (image.empty()) {
        problem = "nothing to write";
        return false;
    }

    if (!remount(true)) {
        problem = "could not make the storage volume writable";
        return false;
    }

    // Everything from here has to put the mount back, whatever happens.
    const auto finish = [this](bool ok, std::string& out, const char* why) {
        if (!ok && out.empty()) {
            out = why;
        }
        // Flushed before remounting read-only: vfat on a volume a computer
        // may also mount is not somewhere to leave dirty pages.
        ::sync();
        remount(false);
        return ok;
    };

    const int fd = ::open(kTempPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return finish(false, problem, "could not create a file on the storage volume");
    }

    std::size_t written = 0;
    while (written < image.size()) {
        const ssize_t wrote = ::write(fd, image.data() + written, image.size() - written);
        if (wrote <= 0) {
            // Out of space is the likely one: the volume is small and may
            // already hold an image.
            ::close(fd);
            ::unlink(kTempPath);
            return finish(false, problem, "ran out of room on the storage volume");
        }
        written += static_cast<std::size_t>(wrote);
    }

    // Durable before it is visible. Renaming a file whose contents are still
    // in the page cache is how a recovery image ends up truncated by a power
    // cut that happens ten seconds later.
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(kTempPath);
        return finish(false, problem, "could not flush the image to storage");
    }
    ::close(fd);

    if (::rename(kTempPath, kImagePath) != 0) {
        ::unlink(kTempPath);
        return finish(false, problem, "could not put the image in place");
    }

    return finish(true, problem, "");
}

}  // namespace tc002
}  // namespace platform
}  // namespace notrix
