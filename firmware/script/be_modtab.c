/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Which Berry modules a script can reach.
 *
 * An allowlist, not a filter. Berry's own table registers everything it
 * builds and leaves the choosing to `#if` blocks in the configuration, which
 * means a module enabled by accident is a module scripts can import. Naming
 * them here makes the decision explicit and the review short: if it is not in
 * this file, no script can reach it.
 *
 * The device's own API - drawing, icons, storage, timers, MQTT - is not a
 * module. It is registered as builtins by the host, so a script does not
 * import it and cannot shadow it.
 */

#include "berry.h"

/* Allowed. */
be_extern_native_module(string);
be_extern_native_module(json);
be_extern_native_module(math);
be_extern_native_module(global);
be_extern_native_module(undefined);

/*
 * Deliberately absent, each for its own reason:
 *
 *   os          File and process access. The file functions are already
 *               compiled out, but `os` is the module a future Berry would
 *               naturally grow more of them in.
 *   sys         Interpreter internals, including the module search path.
 *   debug       Stack walking and hooks. The host uses the hook to enforce
 *               an instruction budget; a script that can reach it can
 *               remove its own limit.
 *   solidify    Emits bytecode, which nothing here should produce.
 *   introspect  Reads and writes arbitrary globals by name, which is a way
 *               past every boundary drawn above.
 *   strict      Only changes how sloppy code is reported, and reporting is
 *               the host's job here.
 *   time        Wall-clock and sleep. Scripts get time through the host's
 *               own builtins, which cannot block the thread drawing the
 *               panel.
 */

BERRY_LOCAL const bntvmodule_t* const be_module_table[] = {
    &be_native_module(string),
    &be_native_module(json),
    &be_native_module(math),
    &be_native_module(global),
    &be_native_module(undefined),
    NULL /* do not remove */
};

BERRY_LOCAL bclass_array be_class_table = {
    NULL, /* do not remove */
};
