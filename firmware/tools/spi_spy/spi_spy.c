// SPDX-License-Identifier: GPL-3.0-or-later
//
// Records how the vendor application talks to the LED panel.
//
// The TC002 drives its 52x16 matrix by writing frames over /dev/spidev0.0, and
// the wire format is the one thing in this bring-up that cannot be read out of
// a config file. Rather than guess at it and write bytes to unknown driver
// chips, this asks the code that already knows: an LD_PRELOAD shim that logs
// what zkgui sends, while zkgui draws its ordinary clock face.
//
// It intercepts and immediately forwards. Nothing is altered, delayed, dropped
// or injected - every call reaches the real libc with the arguments it was
// given. The panel keeps working throughout, which is the point: a capture of
// a broken display would tell us nothing.
//
// Written in C rather than C++ so the shim itself needs no libstdc++, and built
// as a shared object so it asks only for GLIBC_2.4 and loads on the device's
// glibc 2.30.
//
// Output goes to /tmp - tmpfs, RAM-backed - and is capped, because tmpfs is
// 16 MB and a 30 FPS panel would fill it in minutes.

#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/// Overridable with STIPPLE_SPY_LOG, so two captures can be taken without one
/// overwriting the other.
#define LOG_PATH "/tmp/stipple-spi.log"

// Whole frames, but few of them. The first attempt kept 160 bytes of 400
// frames and recorded nothing but the dark left edge of the panel - the lit
// pixels are further in. Full frames are what make the layout readable, and
// eight of them at ~9 KB of hex each is still nothing against 16 MB of tmpfs.
#define MAX_RECORDS 600

/// The cap exists because an unfiltered capture takes 3072 bytes forty times a
/// second, and /tmp is RAM on a device with 36 MB of it. With STIPPLE_SPY_ONLY
/// narrowing the watch to one slow link, the same cap is the wrong tool: it
/// stopped a capture of the MCU after nine minutes, silently, right before the
/// thing it was taken to see. STIPPLE_SPY_MAX raises it deliberately, which is
/// the only way it should ever be raised.
static int record_limit(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* text = getenv("STIPPLE_SPY_MAX");
        cached = (text != NULL && text[0] != 0) ? atoi(text) : MAX_RECORDS;
        if (cached < 1) {
            cached = MAX_RECORDS;
        }
    }
    return cached;
}

// A frame turned out to be exactly 3072 bytes: 1024 pixels of RGB. Capturing
// all of it means the pixel layout can be recovered by comparing frames.
#define MAX_BYTES 512

static int (*real_open)(const char*, int, ...);
static int (*real_open64)(const char*, int, ...);
static ssize_t (*real_write)(int, const void*, size_t);
static int (*real_ioctl)(int, unsigned long, ...);
static ssize_t (*real_read)(int, void*, size_t);
static int (*real_close)(int);

// Descriptors currently open on a spidev node. Small fixed array rather than a
// map: a handful of fds, and no allocation inside an interposer.
#define MAX_TRACKED 16
static int tracked[MAX_TRACKED];
static int tracked_count = 0;

static int log_fd = -1;
static int records = 0;

// Guards against the logger's own writes being intercepted and recursing.
static __thread int inside = 0;

static void resolve(void) {
    if (real_write != NULL) {
        return;
    }
    real_open = dlsym(RTLD_NEXT, "open");
    real_open64 = dlsym(RTLD_NEXT, "open64");
    real_write = dlsym(RTLD_NEXT, "write");
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    real_read = dlsym(RTLD_NEXT, "read");
    real_close = dlsym(RTLD_NEXT, "close");
}

static void ensure_log(void) {
    if (log_fd >= 0 || real_open == NULL) {
        return;
    }
    log_fd = real_open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

static void emit(const char* text, size_t length) {
    ensure_log();
    if (log_fd >= 0) {
        real_write(log_fd, text, length);
    }
}

static void note(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length > 0) {
        emit(line, (size_t)length);
    }
}

/// Everything under /dev and /sys, deliberately.
///
/// The narrow version of this filter - spidev, ttyS, /sys, gpio, pwm - cost a
/// whole round of experiments. It proved that zkgui sends the panel nothing we
/// do not, which looked like a finding and was actually a blind spot: ioctls on
/// /dev/mi_sys, /dev/mi_gfx, /dev/fb0 and /dev/oflash were never logged at all,
/// because those paths did not match.
///
/// The panel lights only while zkgui is alive, and a well-formed frame from us
/// lights nothing once it stops, so the enable is real and is on one of the
/// descriptors the old filter ignored. The SigmaStar MI layer is the obvious
/// candidate. Watching everything costs log volume, which is cheap; watching
/// too little costs a day, which is not.
///
/// STIPPLE_SPY_ONLY narrows it to paths containing a given substring. Watching
/// everything is right when hunting for an unknown enable; it is wrong when the
/// hunt is for something slow and the log has to run for minutes, because
/// spidev takes 3072 bytes forty times a second and /tmp is RAM on a device
/// with 36 MB of it. A capture that fills tmpfs does not just truncate, it
/// takes the running application down with it.
static int interesting(const char* path) {
    const char* only = getenv("STIPPLE_SPY_ONLY");
    if (only != NULL && only[0] != '\0') {
        return strstr(path, only) != NULL;
    }
    return strncmp(path, "/dev/", 5) == 0 || strncmp(path, "/sys/", 5) == 0;
}

/// Every path opened, whether tracked or not.
///
/// A well-formed frame written to spidev lit nothing once zkgui was stopped,
/// and the MCU link carries no enable command - so whatever turns the panel on
/// is somewhere neither of those covers. Logging every open is the cheap way to
/// find it, since the answer is more likely a sysfs file than a clever
/// protocol.
static void note_open(const char* path) {
    note("open %s\n", path);
}

static int is_tracked(int fd) {
    for (int i = 0; i < tracked_count; ++i) {
        if (tracked[i] == fd) {
            return 1;
        }
    }
    return 0;
}

static void track(int fd, const char* path) {
    if (tracked_count < MAX_TRACKED) {
        tracked[tracked_count++] = fd;
        note("OPEN fd=%d %s\n", fd, path);
    }
}

static void untrack(int fd) {
    for (int i = 0; i < tracked_count; ++i) {
        if (tracked[i] == fd) {
            tracked[i] = tracked[--tracked_count];
            note("CLOSE fd=%d\n", fd);
            return;
        }
    }
}

/// Hex dump, truncated. The length is always reported in full even when the
/// bytes are not, so a frame size can be trusted from the log.
static void dump(const char* kind, int fd, const unsigned char* data, size_t length) {
    if (records >= record_limit()) {
        return;
    }
    ++records;

    char line[MAX_BYTES * 3 + 64];
    int at = snprintf(line, sizeof(line), "%s fd=%d len=%zu :", kind, fd, length);

    const size_t shown = length < MAX_BYTES ? length : MAX_BYTES;
    for (size_t i = 0; i < shown && at < (int)sizeof(line) - 4; ++i) {
        at += snprintf(line + at, sizeof(line) - (size_t)at, " %02x", data[i]);
    }
    if (shown < length) {
        at += snprintf(line + at, sizeof(line) - (size_t)at, " ...");
    }
    at += snprintf(line + at, sizeof(line) - (size_t)at, "\n");
    emit(line, (size_t)at);

    if (records == MAX_RECORDS) {
        note("--- record limit reached, no longer logging payloads ---\n");
    }
}

// --- interposed calls --------------------------------------------------------

int open(const char* path, int flags, ...) {
    resolve();

    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }

    const int fd = real_open(path, flags, mode);
    if (fd >= 0 && !inside) {
        inside = 1;
        note_open(path);
        if (interesting(path)) {
            track(fd, path);
        }
        inside = 0;
    }
    return fd;
}

int open64(const char* path, int flags, ...) {
    resolve();

    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }

    const int fd = real_open64 != NULL ? real_open64(path, flags, mode)
                                       : real_open(path, flags, mode);
    if (fd >= 0 && !inside) {
        inside = 1;
        note_open(path);
        if (interesting(path)) {
            track(fd, path);
        }
        inside = 0;
    }
    return fd;
}

ssize_t write(int fd, const void* buffer, size_t count) {
    resolve();
    if (!inside && is_tracked(fd)) {
        inside = 1;
        dump("WRITE", fd, (const unsigned char*)buffer, count);
        inside = 0;
    }
    return real_write(fd, buffer, count);
}

int ioctl(int fd, unsigned long request, ...) {
    resolve();

    va_list args;
    va_start(args, request);
    void* argument = va_arg(args, void*);
    va_end(args);

    // Logged before the call, so a request that hangs still leaves a trace of
    // what was attempted.
    if (!inside && is_tracked(fd)) {
        inside = 1;
        // spidev's ioctls carry magic 'k'. The direction, size and number are
        // all encoded in the request, and printing them raw avoids guessing at
        // which kernel's headers this device was built with.
        const unsigned long size = (request >> 16) & 0x3FFF;
        const unsigned long dir = (request >> 30) & 0x3;
        note("IOCTL fd=%d req=0x%08lx magic=%c nr=%lu size=%lu dir=%lu\n",
             fd, request, (char)((request >> 8) & 0xFF), (request & 0xFF),
             size, dir);

        // The payload, not just the request number.
        //
        // Knowing that MI_AO_SetPubAttr marshals a 56-byte struct says
        // nothing about what is *in* it, and the whole reason to watch a
        // vendor library is to avoid guessing at a layout and then writing
        // the guess into a driver. These are bytes the device is known to
        // accept.
        //
        // Bounded by the size the request itself declares, so a malformed
        // request cannot walk off the end of whatever the caller passed.
        if (argument != NULL && (dir & 1) != 0 && size > 0 && size <= MAX_BYTES) {
            dump("  ARG", fd, (const unsigned char*)argument, (size_t)size);
        }
        inside = 0;
    }

    return real_ioctl(fd, request, argument);
}

ssize_t read(int fd, void* buffer, size_t count) {
    resolve();
    const ssize_t got = real_read(fd, buffer, count);
    if (!inside && got > 0 && is_tracked(fd)) {
        inside = 1;
        dump("READ", fd, (const unsigned char*)buffer, (size_t)got);
        inside = 0;
    }
    return got;
}

int close(int fd) {
    resolve();
    if (!inside && is_tracked(fd)) {
        inside = 1;
        untrack(fd);
        inside = 0;
    }
    return real_close(fd);
}
