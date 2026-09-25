/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Berry's platform hooks, for a device with no console and no filesystem.
 *
 * Berry's own default port writes to stdout and opens files. Neither exists
 * here in any useful sense: there is no terminal attached, and a script that
 * can open a file can read the Wi-Fi credentials. The file functions are
 * compiled out by BE_USE_FILE_SYSTEM 0 in berry_conf.h; what remains is
 * output, which goes to the device's ring log so `print` from a script shows
 * up in the web UI where its author is looking.
 */

#include "berry.h"

#include <string.h>

/* Set by the host before any script runs. A null sink is not an error - it
 * simply means nothing is listening yet, which is true during start-up. */
static void (*s_sink)(const char* text, size_t length) = 0;

void stipple_berry_set_log_sink(void (*sink)(const char*, size_t)) {
    s_sink = sink;
}

BERRY_API void be_writebuffer(const char* buffer, size_t length) {
    if (s_sink != 0 && buffer != 0 && length > 0) {
        s_sink(buffer, length);
    }
    /* Deliberately silent otherwise. Falling back to stdout would write into
     * whatever the vendor's process had open on that descriptor. */
}

BERRY_API char* be_readstring(char* buffer, size_t size) {
    /* The REPL's input hook. There is no console on this device, so a script
     * asking for input gets end-of-file rather than blocking the thread that
     * draws the panel. */
    (void)buffer;
    (void)size;
    return 0;
}
