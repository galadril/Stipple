// SPDX-License-Identifier: GPL-3.0-or-later
//
// Talks to wpa_supplicant's control socket, read-only.
//
// The device has no wpa_cli binary, but wpa_supplicant is running with
// -C/dev/socket/ and its control socket is there as /dev/socket/wlan0. That
// socket is the whole interface: a UNIX datagram socket taking plain text
// commands and replying in kind. It is how wpa_cli works, and it is a far
// better route than editing wpa_supplicant.conf by hand - the daemon owns that
// file, knows how to write it, and can be asked to save it.
//
// This sends only STATUS and SCAN_RESULTS. Neither changes anything, which is
// the point: ADR 0018 puts scanning before joining precisely so the plumbing
// gets proven while the worst outcome is still an empty list.
//
// Note the client socket. wpa_supplicant replies to the address it was sent
// from, so the caller has to bind one of its own somewhere writable - /tmp
// here, which is tmpfs and gone on reboot.

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define SERVER "/dev/socket/wlan0"

static int control = -1;
/// Sized to sun_path, not larger. A longer buffer would let a path be built
/// that cannot be bound, and the compiler is right to complain about it.
static char clientPath[sizeof(((struct sockaddr_un*)0)->sun_path)];

static int openControl(void) {
    control = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (control < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un local;
    memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;
    snprintf(clientPath, sizeof(clientPath), "/tmp/notrix-wpa-%d", (int)getpid());
    snprintf(local.sun_path, sizeof(local.sun_path), "%s", clientPath);

    if (bind(control, (struct sockaddr*)&local, sizeof(local)) < 0) {
        perror("bind");
        return -1;
    }

    struct sockaddr_un remote;
    memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    snprintf(remote.sun_path, sizeof(remote.sun_path), "%s", SERVER);

    if (connect(control, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
        perror("connect " SERVER);
        return -1;
    }

    // Never block the caller forever. On the device this will sit on the render
    // loop, and a daemon that has gone away must not take the clock with it.
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(control, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return 0;
}

static int ask(const char* command, char* reply, size_t capacity) {
    if (send(control, command, strlen(command), 0) < 0) {
        perror("send");
        return -1;
    }
    const ssize_t got = recv(control, reply, capacity - 1, 0);
    if (got < 0) {
        perror("recv");
        return -1;
    }
    reply[got] = '\0';
    return (int)got;
}

int main(int argc, char** argv) {
    if (openControl() != 0) {
        return 1;
    }

    static char reply[8192];

    if (argc > 1) {
        // An explicit command, for poking at one thing at a time. Still only
        // ever what the caller typed - this tool issues nothing on its own.
        if (ask(argv[1], reply, sizeof(reply)) >= 0) {
            printf("%s\n", reply);
        }
    } else {
        if (ask("STATUS", reply, sizeof(reply)) >= 0) {
            printf("=== STATUS ===\n%s\n", reply);
        }
        if (ask("SCAN_RESULTS", reply, sizeof(reply)) >= 0) {
            printf("=== SCAN_RESULTS ===\n%s\n", reply);
        }
    }

    close(control);
    unlink(clientPath);
    return 0;
}
