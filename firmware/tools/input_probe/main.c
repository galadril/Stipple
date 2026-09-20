// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reports what the TC002's physical controls actually send.
//
// /proc/bus/input/devices says event67 declares KEY_UP, KEY_LEFT, KEY_RIGHT and
// KEY_DOWN, and event68 reports ABS_X. Declared is not wired: a device tree can
// name four keys on hardware that has two, and ADR 0016 is openly unsure whether
// a third button exists. Rather than map the enum from a bitmap, this prints
// every event so a person can press each control and read off the truth.
//
// Run it, then press − , press + , press the knob, and turn the knob each way.
//
// Needs nothing from the panel and takes nothing away from zkgui - evdev
// delivers to every reader - so it is safe to run while the stock app is up.

#define _GNU_SOURCE

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

// Declared here rather than included from <linux/input.h>.
//
// Same reasoning as the spidev ioctls in panel_test: the layout is stable
// kernel ABI, and depending on a build-host header to describe a device's
// kernel is an assumption we do not need to make. On 32-bit ARM this is
// 8 + 2 + 2 + 4 = 16 bytes.
struct evdev_event {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03

static const char* key_name(uint16_t code) {
    switch (code) {
        case 103: return "KEY_UP";
        case 105: return "KEY_LEFT";
        case 106: return "KEY_RIGHT";
        case 108: return "KEY_DOWN";
        default: return "KEY_?";
    }
}

static const char* phase_name(int32_t value) {
    switch (value) {
        case 0: return "release";
        case 1: return "press";
        case 2: return "repeat";
        default: return "?";
    }
}

int main(int argc, char** argv) {
    const int seconds = (argc > 1) ? atoi(argv[1]) : 30;

    const char* paths[2] = {"/dev/input/event67", "/dev/input/event68"};
    const char* labels[2] = {"event67/keys", "event68/knob"};
    int fds[2];

    for (int i = 0; i < 2; ++i) {
        fds[i] = open(paths[i], O_RDONLY);
        if (fds[i] < 0) {
            perror(paths[i]);
            return 1;
        }
    }

    printf("listening for %d seconds\n\n", seconds);
    printf("  press -, press +, press the knob, turn the knob each way\n");
    printf("  (if a control prints nothing, it is not wired to these nodes)\n\n");

    // Tracks the knob's absolute position so turns report a delta. ABS_X is a
    // position, not a detent count - converting one into the other is exactly
    // what the real adapter will have to do, so it is worth seeing the raw
    // numbers and the step size here first.
    int32_t last_abs = 0;
    int have_abs = 0;

    struct timeval deadline;
    gettimeofday(&deadline, NULL);
    deadline.tv_sec += seconds;

    for (;;) {
        struct timeval now;
        gettimeofday(&now, NULL);
        if (now.tv_sec >= deadline.tv_sec) {
            break;
        }

        fd_set set;
        FD_ZERO(&set);
        FD_SET(fds[0], &set);
        FD_SET(fds[1], &set);
        const int highest = (fds[0] > fds[1]) ? fds[0] : fds[1];

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        if (select(highest + 1, &set, NULL, NULL, &timeout) <= 0) {
            continue;
        }

        for (int i = 0; i < 2; ++i) {
            if (!FD_ISSET(fds[i], &set)) {
                continue;
            }

            struct evdev_event event;
            const ssize_t got = read(fds[i], &event, sizeof(event));
            if (got != (ssize_t)sizeof(event)) {
                continue;
            }

            if (event.type == EV_SYN) {
                continue;
            }

            if (event.type == EV_KEY) {
                printf("%-13s EV_KEY  code=%-4u %-10s %s\n", labels[i],
                       event.code, key_name(event.code), phase_name(event.value));
            } else if (event.type == EV_ABS) {
                if (have_abs) {
                    printf("%-13s EV_ABS  code=%-4u value=%-8d delta=%+d\n",
                           labels[i], event.code, event.value,
                           event.value - last_abs);
                } else {
                    printf("%-13s EV_ABS  code=%-4u value=%-8d (first)\n",
                           labels[i], event.code, event.value);
                    have_abs = 1;
                }
                last_abs = event.value;
            } else {
                printf("%-13s type=%u code=%u value=%d\n", labels[i], event.type,
                       event.code, event.value);
            }
            fflush(stdout);
        }
    }

    printf("\ndone\n");
    for (int i = 0; i < 2; ++i) {
        close(fds[i]);
    }
    return 0;
}
