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
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define LOG_PATH "/tmp/notrix-spi.log"

// Whole frames, but few of them. The first attempt kept 160 bytes of 400
// frames and recorded nothing but the dark left edge of the panel - the lit
// pixels are further in. Full frames are what make the layout readable, and
// eight of them at ~9 KB of hex each is still nothing against 16 MB of tmpfs.
#define MAX_RECORDS 8

// A frame turned out to be exactly 3072 bytes: 1024 pixels of RGB. Capturing
// all of it means the pixel layout can be recovered by comparing frames.
#define MAX_BYTES 3072

static int (*real_open)(const char*, int, ...);
static int (*real_open64)(const char*, int, ...);
static ssize_t (*real_write)(int, const void*, size_t);
static int (*real_ioctl)(int, unsigned long, ...);
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
    if (records >= MAX_RECORDS) {
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
    if (fd >= 0 && !inside && strstr(path, "spidev") != NULL) {
        inside = 1;
        track(fd, path);
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
    if (fd >= 0 && !inside && strstr(path, "spidev") != NULL) {
        inside = 1;
        track(fd, path);
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
        note("IOCTL fd=%d req=0x%08lx magic=%c nr=%lu size=%lu dir=%lu\n",
             fd, request, (char)((request >> 8) & 0xFF), (request & 0xFF),
             (request >> 16) & 0x3FFF, (request >> 30) & 0x3);
        inside = 0;
    }

    return real_ioctl(fd, request, argument);
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
