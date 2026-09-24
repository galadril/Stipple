// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writes one well-formed frame to the TC002's LED matrix.
//
// Everything here comes from watching the vendor application do it, captured by
// firmware/tools/spi_spy/spi_spy.c and recorded in
// docs/research/tc002-platform-findings.md. Nothing is guessed: the device
// node, the SPI settings, the frame size and the pixel stride were all read off
// the wire.
//
// The one thing the capture could not answer is channel order. Every lit pixel
// the vendor drew was 255,255,255, and white looks identical under RGB, GRB and
// BGR. So this draws three bands whose channels are deliberately unequal, and a
// person looks at the panel and says which is which. That is the whole purpose
// of the pattern.
//
// It restores a black frame before exiting, including when interrupted, so the
// panel is never left holding our test image.

#define _GNU_SOURCE

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_PATH "/dev/spidev0.0"

// Panel geometry. The matrix is 52 columns wide but addressed as 64; the last
// twelve are padding the vendor app never lights.
#define PANEL_W 52
#define BUFFER_W 64
#define PANEL_H 16
#define FRAME_BYTES (BUFFER_W * PANEL_H * 3)

// Taken verbatim from the ioctl requests the vendor app issued, rather than
// from <linux/spi/spidev.h>. The capture is ground truth for this kernel; a
// header on the build host is an assumption about it.
#define IOC_WR_MODE 0x40016b01u
#define IOC_WR_LSB_FIRST 0x40016b02u
#define IOC_WR_BITS_PER_WORD 0x40016b03u
#define IOC_WR_MAX_SPEED_HZ 0x40046b04u

static const uint8_t kMode = 0;
static const uint8_t kBitsPerWord = 8;
static const uint8_t kLsbFirst = 0;
static const uint32_t kSpeedHz = 10000000;

static int spi_fd = -1;
static uint8_t frame[FRAME_BYTES];

static void put(int col, int row, uint8_t a, uint8_t b, uint8_t c) {
    if (col < 0 || col >= PANEL_W || row < 0 || row >= PANEL_H) {
        return;
    }
    const size_t at = (size_t)(row * BUFFER_W + col) * 3;
    frame[at] = a;
    frame[at + 1] = b;
    frame[at + 2] = c;
}

static int send_frame(void) {
    const ssize_t written = write(spi_fd, frame, FRAME_BYTES);
    if (written != (ssize_t)FRAME_BYTES) {
        fprintf(stderr, "short write: %zd of %d\n", written, FRAME_BYTES);
        return 1;
    }
    return 0;
}

static void blank_and_close(void) {
    if (spi_fd >= 0) {
        memset(frame, 0, sizeof(frame));
        /* Best effort on the way out: the panel is being blanked as a
           courtesy and the descriptor is closing either way. */
        if (write(spi_fd, frame, FRAME_BYTES) != (ssize_t)FRAME_BYTES) {
            fprintf(stderr, "panel_test: could not blank the panel on exit\n");
        }
        close(spi_fd);
        spi_fd = -1;
    }
}

static void on_signal(int signum) {
    (void)signum;
    blank_and_close();
    _exit(1);
}

int main(int argc, char** argv) {
    const int hold_seconds = (argc > 1) ? atoi(argv[1]) : 12;

    // Frame rate, because it is the open question rather than a setting.
    //
    // A capture of zkgui shows it doing exactly what this does - the same seven
    // spidev ioctls, the same 3072-byte writes, one MCU version query, and no
    // enable step anywhere. So if a well-formed frame from us lights nothing
    // once zkgui is stopped, the difference is not *what* is sent but how often.
    // These driver chips look like they hold an image only while being fed, and
    // at too low a rate the panel is lit for so little of each period that it
    // reads as dark.
    //
    // 0 means "as fast as the bus allows": no sleep at all. At 10 MHz a
    // 3072-byte frame is ~2.5 ms on the wire, so that is somewhere near 400 Hz.
    const int fps = (argc > 2) ? atoi(argv[2]) : 30;

    // Blank the panel even if we are interrupted, so a Ctrl-C does not leave
    // the matrix holding a test pattern until something else redraws it.
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    spi_fd = open(SPI_PATH, O_RDWR);
    if (spi_fd < 0) {
        perror("open " SPI_PATH);
        return 1;
    }

    if (ioctl(spi_fd, IOC_WR_MODE, &kMode) < 0 ||
        ioctl(spi_fd, IOC_WR_LSB_FIRST, &kLsbFirst) < 0 ||
        ioctl(spi_fd, IOC_WR_BITS_PER_WORD, &kBitsPerWord) < 0 ||
        ioctl(spi_fd, IOC_WR_MAX_SPEED_HZ, &kSpeedHz) < 0) {
        perror("spi configuration");
        close(spi_fd);
        return 1;
    }

    printf("panel   : %dx%d visible, %dx%d addressed, %d bytes per frame\n",
           PANEL_W, PANEL_H, BUFFER_W, PANEL_H, FRAME_BYTES);
    printf("spi     : mode %u, %u bits, %s first, %u Hz\n",
           kMode, kBitsPerWord, kLsbFirst ? "LSB" : "MSB", kSpeedHz);

    memset(frame, 0, sizeof(frame));

    // Three bands, each with exactly one channel set. Whichever colour appears
    // on the left is channel 0, and so on - which is the question.
    const int third = PANEL_W / 3;
    for (int row = 2; row < PANEL_H - 2; ++row) {
        for (int col = 0; col < PANEL_W; ++col) {
            if (col < third) {
                put(col, row, 255, 0, 0);
            } else if (col < third * 2) {
                put(col, row, 0, 255, 0);
            } else {
                put(col, row, 0, 0, 255);
            }
        }
    }

    // Two orientation markers, both white so they are channel-agnostic:
    // a single dot at the origin and a two-pixel stub at the far end of row 0.
    // If the panel is mirrored or flipped, these say so immediately.
    put(0, 0, 255, 255, 255);
    put(PANEL_W - 2, 0, 255, 255, 255);
    put(PANEL_W - 1, 0, 255, 255, 255);

    printf("refresh : %s\n",
           (fps > 0) ? "throttled" : "unthrottled (as fast as the bus allows)");

    printf("\nwriting test frame - look at the panel\n");
    printf("  three bands, one channel each, left to right\n");
    printf("  one white dot top-left, two white pixels top-right\n");

    // Re-sent continuously rather than written once.
    //
    // The first attempt wrote a single well-formed frame and nothing appeared,
    // and the obvious suspects both turned out innocent: the backlight stays at
    // 50/255 with bl_power on after zkgui exits, and the MCU link carries no
    // enable command. That leaves refresh - these driver chips appear to hold
    // an image only as long as something keeps feeding them, which is why the
    // vendor app writes repeatedly even for a static clock face.
    const int rate = (fps > 0) ? fps : 400;
    for (int i = 0; i < hold_seconds * rate; ++i) {
        if (send_frame() != 0) {
            blank_and_close();
            return 1;
        }
        if (fps > 0) {
            usleep((useconds_t)(1000000 / fps));
        }
    }

    printf("blanking\n");
    blank_and_close();
    return 0;
}
