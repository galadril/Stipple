/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Berry's platform hooks, for a device with no console and no filesystem.
 *
 * Berry's own default port writes to stdout and opens files. Neither exists
 * here in any useful sense: there is no terminal attached, and a script that
 * can open a file can read the Wi-Fi credentials.
 *
 * So this port does two things. Output goes to the device's ring log, so
 * `print` from a script shows up in the web UI where its author is looking.
 * And every file and directory call is answered with a refusal - see the
 * bottom of this file for why that is not redundant with berry_conf.h.
 */

#include "berry.h"
#include "be_sys.h"

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

/* --- the filesystem that is not there ------------------------------------
 *
 * Berry declares these in be_sys.h and its compiler references be_fopen even
 * when BE_USE_FILE_SYSTEM is 0, because upstream expects a port to answer.
 * This one answers no.
 *
 * Refusing here rather than relying on the configuration alone is deliberate.
 * BE_USE_FILE_SYSTEM 0 removes the paths that exist today; these stubs remove
 * the outcome. If a later Berry grows a route to a file whose guard we did not
 * notice, it reaches this file and gets nothing - the sandbox holds because
 * there is no implementation to reach, not because a macro was set correctly.
 *
 * There is nothing on this device a script should be reading anyway: the
 * interesting files are the Wi-Fi credentials, the configuration and the
 * firmware itself.
 */

void* be_fopen(const char* filename, const char* modes) {
    (void)filename; (void)modes;
    return 0;
}

int be_fclose(void* hfile) { (void)hfile; return -1; }

size_t be_fwrite(void* hfile, const void* buffer, size_t length) {
    (void)hfile; (void)buffer; (void)length;
    return 0;
}

size_t be_fread(void* hfile, void* buffer, size_t length) {
    (void)hfile; (void)buffer; (void)length;
    return 0;
}

char* be_fgets(void* hfile, void* buffer, int size) {
    (void)hfile; (void)buffer; (void)size;
    return 0;
}

int be_fseek(void* hfile, long offset) { (void)hfile; (void)offset; return -1; }
long int be_ftell(void* hfile) { (void)hfile; return -1; }
long int be_fflush(void* hfile) { (void)hfile; return -1; }
size_t be_fsize(void* hfile) { (void)hfile; return 0; }

/* And the directory half, which is how you find what to open. */
int be_isdir(const char* path) { (void)path; return 0; }
int be_isfile(const char* path) { (void)path; return 0; }
int be_isexist(const char* path) { (void)path; return 0; }
int be_chdir(const char* path) { (void)path; return -1; }
int be_mkdir(const char* path) { (void)path; return -1; }
int be_unlink(const char* filename) { (void)filename; return -1; }

char* be_getcwd(char* buf, size_t size) {
    if (buf != 0 && size > 0) { buf[0] = 0; }
    return buf;
}

int be_dirfirst(bdirinfo* info, const char* path) { (void)info; (void)path; return -1; }
int be_dirnext(bdirinfo* info) { (void)info; return -1; }
int be_dirclose(bdirinfo* info) { (void)info; return -1; }

/* --- open() --------------------------------------------------------------
 *
 * Worth reading carefully, because it is the one that would have got through.
 *
 * `open` is in Berry's builtin constant table unconditionally - see
 * m_builtin in be_baselib.c. BE_USE_FILE_SYSTEM does not gate it. All that
 * macro really does is stop Berry's own default port from implementing
 * be_fopen; the builtin still exists and still calls it. So a port that
 * supplies a working be_fopen - which most do, because the compiler wants one
 * - hands `open()` straight back to every script, with the configuration
 * still reading BE_USE_FILE_SYSTEM 0.
 *
 * Ours refuses, so the failure is an error the script's author sees rather
 * than a file handle they did not expect to get.
 *
 * The implementation normally lives in be_filelib.c, which is excluded from
 * the build (see firmware/CMakeLists.txt). The symbol still has to exist
 * because the constant table names it.
 */
int be_nfunc_open(bvm* vm) {
    be_raise(vm, "unsupported_operation", "scripts cannot open files");
    return 0; /* not reached: be_raise does not return */
}
