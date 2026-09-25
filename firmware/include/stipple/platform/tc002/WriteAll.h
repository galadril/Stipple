// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <errno.h>
#include <unistd.h>

#include <cstddef>

namespace stipple {
namespace platform {
namespace tc002 {

/// Write the whole buffer, or say it could not.
///
/// `::write` is allowed to write fewer bytes than asked and to fail with
/// EINTR, and both are ignored by a bare call. On this device that is not
/// academic: the panel is latched by a single byte to a GPIO, and a strobe
/// that silently does not happen means 3072 bytes reach the driver chips and
/// nothing appears - the failure that cost most of the bring-up's confusing
/// days.
///
/// It also stops the build failing. Ubuntu's GCC marks `write` as
/// `warn_unused_result`, so ignoring it is an error under `-Werror`, and
/// casting the result away would be lying to the compiler about a return
/// value that genuinely matters here.
inline bool writeAll(int fd, const void* data, std::size_t size) noexcept {
    const char* at = static_cast<const char*>(data);
    std::size_t written = 0;

    while (written < size) {
        const ssize_t chunk = ::write(fd, at + written, size - written);
        if (chunk > 0) {
            written += static_cast<std::size_t>(chunk);
            continue;
        }
        // A signal arriving mid-write is not a failure, and retrying is the
        // documented response.
        if (chunk < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }

    return true;
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
