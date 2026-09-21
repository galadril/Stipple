// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads the TC002's MCU link and prints the frames it pushes.
//
// The MCU is the only source of battery state on this device: there is no
// /sys/class/power_supply, no hwmon and no IIO. zkgui opens /dev/ttyS1, sends
// exactly one command (a version query) and then listens, so everything else
// the MCU reports is unsolicited telemetry.
//
// Framing, decoded from a capture of zkgui:
//
//     ff 55 <cmd> <len> <payload[len]> <trailer...>
//
//     ff 55 11 00 01 65                  host -> MCU, "what version are you"
//     ff 55 fe 00 02 52 ...              MCU -> host, ack
//     ff 55 11 07 56 31 2e 30 2e 31 37   MCU -> host, "V1.0.17" in ASCII
//     ff 55 03 03 5b 0c 49 02 0a         MCU -> host, three drifting bytes
//     ff 55 02 01 01 01 58               MCU -> host, one flag
//
// This prints them so the 0x03 payload can be identified rather than assumed.
// Run it with the vendor app stopped, or both will read the same port.

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PORT "/dev/ttyS1"

/// Milliseconds since the probe started.
///
/// Without a timestamp a frame log says what the MCU reports but not when, and
/// every question left about this link is a question about timing: whether a
/// command appears only while something is on screen, and whether a flag flips
/// when the cable is pulled. Correlating a log against a person's actions needs
/// a clock on every line.
static long long now_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static long long started = 0;

static void dump(const uint8_t* frame, int length) {
    printf("[%7lld] cmd=%02x len=%02x  payload:", now_millis() - started, frame[2], frame[3]);
    for (int i = 0; i < frame[3]; ++i) {
        printf(" %02x", frame[4 + i]);
    }
    printf("   decimal:");
    for (int i = 0; i < frame[3]; ++i) {
        printf(" %3u", frame[4 + i]);
    }
    printf("   raw:");
    for (int i = 0; i < length; ++i) {
        printf(" %02x", frame[i]);
    }
    printf("\n");
    fflush(stdout);
}

int main(int argc, char** argv) {
    const int seconds = (argc > 1) ? atoi(argv[1]) : 20;
    started = now_millis();

    const int fd = open(PORT, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open " PORT);
        return 1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return 1;
    }

    cfmakeraw(&tty);
    // 1.5 Mbaud, from the port's own configuration rather than a guess.
    cfsetispeed(&tty, B1500000);
    cfsetospeed(&tty, B1500000);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= (unsigned)~CRTSCTS;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;  // 1s read timeout

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return 1;
    }

    printf("listening on " PORT " for %ds\n\n", seconds);

    // Ask the version, exactly as zkgui does. If the MCU answers, the link and
    // the baud rate are both right, which makes everything after it meaningful.
    const uint8_t version[] = {0xff, 0x55, 0x11, 0x00, 0x01, 0x65};
    if (write(fd, version, sizeof(version)) != (ssize_t)sizeof(version)) {
        printf("(version query could not be written)\n");
    }

    // Optional second argument: seconds to wait before switching the
    // microphone on. Sending it immediately after the version query did not
    // work, and the vendor application sends it much later - so the question
    // this answers is whether the MCU needs a gap, or something else entirely.
    const int mic_after = (argc > 2) ? atoi(argv[2]) : -1;
    int mic_sent = 0;
    const uint8_t mic_on[] = {0xff, 0x55, 0x04, 0x01, 0x01, 0x01, 0x5a};

    uint8_t buffer[512];
    int held = 0;

    // Wall clock rather than a timeout counter: the old loop only advanced when
    // a read timed out, so a link that never went quiet ran indefinitely.
    const long long deadline = started + (long long)seconds * 1000;
    while (now_millis() < deadline) {
        if (mic_after >= 0 && !mic_sent &&
            now_millis() - started >= (long long)mic_after * 1000) {
            mic_sent = 1;
            const ssize_t wrote = write(fd, mic_on, sizeof(mic_on));
            printf("[%7lld] --- microphone on, write returned %d ---\n",
                   now_millis() - started, (int)wrote);
            fflush(stdout);
        }

        uint8_t chunk[256];
        const ssize_t got = read(fd, chunk, sizeof(chunk));
        if (got <= 0) {
            continue;
        }

        if (held + got > (int)sizeof(buffer)) {
            held = 0;  // resync rather than grow
        }
        memcpy(buffer + held, chunk, (size_t)got);
        held += (int)got;

        // Walk the buffer for ff 55 headers and emit whole frames.
        int at = 0;
        while (at + 4 <= held) {
            if (buffer[at] != 0xff || buffer[at + 1] != 0x55) {
                ++at;
                continue;
            }
            const int payload = buffer[at + 3];
            const int total = 4 + payload + 2;  // header+cmd+len, payload, trailer
            if (at + total > held) {
                break;  // wait for the rest
            }
            dump(buffer + at, total);
            at += total;
        }

        if (at > 0) {
            memmove(buffer, buffer + at, (size_t)(held - at));
            held -= at;
        }
    }

    close(fd);
    return 0;
}
