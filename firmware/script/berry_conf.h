/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Berry's configuration for Stipple. Derived from Berry's own default
 * (third_party/berry, MIT) with the overrides marked below; everything not
 * overridden keeps their value, so a setting added in a later version arrives
 * with their default rather than going missing.
 *
 * This file is the sandbox. Berry is a general-purpose interpreter with a
 * filesystem, a dynamic loader and a bytecode reader, and each of those is a
 * way for an uploaded script to stop being a drawing and start being code
 * running as root on a device on your network.
 *
 * Regenerate the constant tables after changing anything here:
 *   python3 tools/coc/coc -o generate src default -c <this file>
 */

/********************************************************************
** Copyright (c) 2018-2020 Guan Wenliang
** This file is part of the Berry default interpreter.
** skiars@qq.com, https://github.com/Skiars/berry
** See Copyright Notice in the LICENSE file or at
** https://github.com/Skiars/berry/blob/master/LICENSE
********************************************************************/
#ifndef BERRY_CONF_H
#define BERRY_CONF_H

#include <assert.h>

/* Macro: BE_DEBUG
 * Berry interpreter debug switch.
 * Default: 0
 **/
#ifndef BE_DEBUG
#define BE_DEBUG                        0
#endif

/* Macro: BE_LONGLONG_INT
 * Select integer length.
 * If the value is 0, use an integer of type int, use a long
 * integer type when the value is 1, and use a long long integer
 * type when the value is 2.
 * Default: 2
 */
/* Stipple: 32-bit integers: an ARMv7 driving a panel 52 pixels wide has no use for 64-bit arithmetic. */
#define BE_INTGER_TYPE                  1

/* Macro: BE_USE_SINGLE_FLOAT
 * Select floating point precision.
 * Use double-precision floating-point numbers when the value
 * is 0 (default), otherwise use single-precision floating-point
 * numbers.
 * Default: 0
 **/
/* Stipple: Single precision halves the size of every number a script holds. */
#define BE_USE_SINGLE_FLOAT             1

/* Macro: BE_BYTES_MAX_SIZE
 * Maximum size in bytes of a `bytes()` object.
 * Putting too much pressure on the memory allocator can do
 * harm, so we limit the maximum size.
 * Default: 32kb
 **/
#define BE_BYTES_MAX_SIZE               (32*1024)   /* 32 kb default value */

/* Macro: BE_USE_PRECOMPILED_OBJECT
 * Use precompiled objects to avoid creating these objects at
 * runtime. Enable this macro can greatly optimize RAM usage.
 * Default: 1
 **/
#define BE_USE_PRECOMPILED_OBJECT       1

/* Macro: BE_DEBUG_SOURCE_FILE
 * Indicate if each function remembers its source file name
 * 0: do not keep the file name (saves 4 bytes per function)
 * 1: keep the source file name
 * Default: 1
 **/
#define BE_DEBUG_SOURCE_FILE            1

/* Macro: BE_DEBUG_RUNTIME_INFO
 * Set runtime error debugging information.
 * 0: unable to output source file and line number at runtime.
 * 1: output source file and line number information at runtime.
 * 2: the information use uint16_t type (save space).
 * Default: 1
 **/
#define BE_DEBUG_RUNTIME_INFO           1

/* Macro: BE_DEBUG_VAR_INFO
 * Set variable debugging tracking information.
 * 0: disable variable debugging tracking information at runtime.
 * 1: enable variable debugging tracking information at runtime.
 * Default: 1
 **/
/* Stipple: Costs flash to help somebody reading a core dump, which is not how this device is debugged. Source lines stay: a compile error with no line number is useless in the editor that just showed the mistake. */
#define BE_DEBUG_VAR_INFO               0

/* Macro: BE_USE_PERF_COUNTERS
 * Use the obshook function to report low-level actions.
 * Default: 1
 **/
#define BE_USE_PERF_COUNTERS            1

/* Macro: BE_VM_OBSERVABILITY_SAMPLING
 * If BE_USE_PERF_COUNTERS == 1
 * then the observability hook is called regularly in the VM loop
 * allowing to stop infinite loops or too-long running code.
 * The value is a power of 2.
 * Default: 20 - which translates to 2^20 or ~1 million instructions
 **/
/* Stipple: The hook that enforces a per-frame instruction budget. A script with a runaway loop has to lose its frame rather than the panel. */
#define BE_VM_OBSERVABILITY_SAMPLING    16

/* Macro: BE_STACK_TOTAL_MAX
 * Set the maximum total stack size.
 * Default: 20000
 **/
/* Stipple: Berry defaults to 20000 slots. Scripts here draw a 52x16 panel. */
#define BE_STACK_TOTAL_MAX              4000

/* Macro: BE_STACK_FREE_MIN
 * Set the minimum free count of the stack. The stack idles will
 * be checked when a function is called, and the stack will be
 * expanded if the number of free is less than BE_STACK_FREE_MIN.
 * Default: 10
 **/
#define BE_STACK_FREE_MIN               10

/* Macro: BE_STACK_START
 * Set the starting size of the stack at VM creation.
 * Default: 50
 **/
#define BE_STACK_START                  50

/* Macro: BE_CONST_SEARCH_SIZE
 * Constants in function are limited to 255. However the compiler
 * will look for a maximum of pre-existing constants to avoid
 * performance degradation. This may cause the number of constants
 * to be higher than required.
 * Increase is you need to solidify functions.
 * Default: 50
 **/
#define BE_CONST_SEARCH_SIZE            50

/* Macro: BE_STACK_FREE_MIN
 * The short string will hold the hash value when the value is
 * true. It may be faster but requires more RAM.
 * Default: 0
 **/
#define BE_USE_STR_HASH_CACHE           0

/* Macro: BE_USE_FILE_SYSTEM
 * The file system interface will be used when this macro is true
 * or when using the OS module. Otherwise the file system interface
 * will not be used.
 * Default: 0
 **/
/* Stipple: No filesystem. A script has no business opening a file, and the interesting files here are the Wi-Fi credentials and the firmware. */
#define BE_USE_FILE_SYSTEM              0

/* Macro: BE_USE_SCRIPT_COMPILER
 * Enable compiler when BE_USE_SCRIPT_COMPILER is not 0, otherwise
 * disable the compiler.
 * Default: 1
 **/
#define BE_USE_SCRIPT_COMPILER          1

/* Macro: BE_USE_BYTECODE_SAVER
 * Enable save bytecode to file when BE_USE_BYTECODE_SAVER is not 0,
 * otherwise disable the feature.
 * Default: 1
 **/
/* Stipple: Nothing here should emit bytecode. */
#define BE_USE_BYTECODE_SAVER           0

/* Macro: BE_USE_BYTECODE_LOADER
 * Enable load bytecode from file when BE_USE_BYTECODE_LOADER is not 0,
 * otherwise disable the feature.
 * Default: 1
 **/
/* Stipple: The sharper edge of the two. Precompiled bytecode skips the compiler entirely, so every check the parser performs is bypassed. Scripts arrive as source and are compiled here, where they can be rejected. */
#define BE_USE_BYTECODE_LOADER          0

/* Macro: BE_USE_SHARED_LIB
 * Enable shared library  when BE_USE_SHARED_LIB is not 0,
 * otherwise disable the feature.
 * Default: 1
 **/
/* Stipple: No dynamic loading: importing a shared object is arbitrary native code, which is the whole thing this sandbox exists to prevent. */
#define BE_USE_SHARED_LIB               0

/* Macro: BE_USE_OVERLOAD_HASH
 * Allows instances to overload hash methods for use in the
 * built-in Map class. Disable this feature to crop the code
 * size.
 * Default: 1
 **/
#define BE_USE_OVERLOAD_HASH            1

/* Macro: BE_MAX_PARSER_DEPTH
 * Hard limit on parser recursion depth (nested expressions and blocks).
 * Each level costs ~hundreds of bytes of native C stack, so this protects
 * pathological source from overflowing the C stack at compile time.
 * Stored in a bbyte, so values above 255 are clamped.
 * Default: 25 (safe on ESP32 with an 8 KB task stack; well above any
 * realistic hand-written Berry code).
 **/
/* Stipple: The parser runs on untrusted input, and deep recursion on this device is a crash rather than a computation. */
#define BE_MAX_PARSER_DEPTH             16

/* Macro: BE_USE_DEBUG_HOOK
 * Berry debug hook switch.
 * Default: 0
 **/
#define BE_USE_DEBUG_HOOK               0

/* Macro: BE_USE_DEBUG_GC
 * Enable GC debug mode. This causes an actual gc after each
 * allocation. It's much slower and should not be used
 * in production code.
 * Default: 0
 **/
#define BE_USE_DEBUG_GC                  0

/* Macro: BE_USE_DEBUG_STACK
 * Enable Stack Resize debug mode. At each function call
 * the stack is reallocated at a different memory location
 * and the previous location is cleared with toxic data.
 * Default: 0
 **/
#define BE_USE_DEBUG_STACK               0

/* Macro: BE_USE_MEM_ALIGNED
 * Some embedded processors have special memory areas
 * with read/write constraints of being aligned to 32 bits boundaries.
 * This options tries to move such memory areas to this region.
 * Default: 0
 **/
#define BE_USE_MEM_ALIGNED               0

/* Macro: BE_USE_XXX_MODULE
 * These macros control whether the related module is compiled.
 * When they are true, they will enable related modules. At this
 * point you can use the import statement to import the module.
 * They will not compile related modules when they are false.
 **/
#define BE_USE_STRING_MODULE            1
#define BE_USE_JSON_MODULE              1
#define BE_USE_MATH_MODULE              1
#define BE_USE_TIME_MODULE              1
#define BE_USE_OS_MODULE                1
#define BE_USE_GLOBAL_MODULE            1
#define BE_USE_SYS_MODULE               1
#define BE_USE_DEBUG_MODULE             1
#define BE_USE_GC_MODULE                1
#define BE_USE_SOLIDIFY_MODULE          1
#define BE_USE_INTROSPECT_MODULE        1
#define BE_USE_STRICT_MODULE            1

/* Macro: BE_EXPLICIT_XXX
 * If these macros are defined, the corresponding function will
 * use the version defined by these macros. These macro definitions
 * are not required.
 * The default is to use the functions in the standard library.
 **/
#define BE_EXPLICIT_ABORT               abort
#define BE_EXPLICIT_EXIT                exit
#define BE_EXPLICIT_MALLOC              malloc
#define BE_EXPLICIT_FREE                free
#define BE_EXPLICIT_REALLOC             realloc

/* Macro: be_assert
 * Berry debug assertion. Only enabled when BE_DEBUG is active.
 * Default: use the assert() function of the standard library.
 **/
#define be_assert(expr)                 assert(expr)


/* Stipple: the module allowlist, enforced at compile time as well as in
 * be_modtab.c.
 *
 * Switching them off here means the code is not built at all rather than
 * built and then not registered - smaller, and it removes the possibility of
 * reaching one through some other route. Each is off for its own reason:
 *
 *   os          file and process access
 *   sys         interpreter internals, including the module search path
 *   debug       stack walking and hooks; the host uses the hook to enforce
 *               the instruction budget, and a script that can reach it can
 *               remove its own limit
 *   solidify    emits bytecode, which nothing here should produce
 *   introspect  reads and writes arbitrary globals by name, which is a way
 *               past every boundary drawn above
 *   strict      only changes how sloppy code is reported, and reporting is
 *               the host's job here
 *   time        wall clock and sleep; scripts get time from the host's own
 *               builtins, which cannot block the thread drawing the panel
 */
/* Undefined before being redefined, so this block overrides the defaults
 * above rather than colliding with them. Keeping both visible is the point:
 * Berry's value stays on the page next to ours, so a review can see what was
 * changed and a later Berry version that adds a module arrives with their
 * default rather than going missing. */
#undef BE_USE_OS_MODULE
#undef BE_USE_SYS_MODULE
#undef BE_USE_DEBUG_MODULE
#undef BE_USE_SOLIDIFY_MODULE
#undef BE_USE_INTROSPECT_MODULE
#undef BE_USE_STRICT_MODULE
#undef BE_USE_TIME_MODULE

#define BE_USE_OS_MODULE                0
#define BE_USE_SYS_MODULE               0
#define BE_USE_DEBUG_MODULE             0
#define BE_USE_SOLIDIFY_MODULE          0
#define BE_USE_INTROSPECT_MODULE        0
#define BE_USE_STRICT_MODULE            0
#define BE_USE_TIME_MODULE              0

/* Kept: string handling, JSON for parsing payloads, math for the arithmetic
 * an animation needs, and global because the runtime requires it. Redefined
 * to the same value they already have, so the allowlist reads as one list
 * rather than half a list and an absence. */
#undef BE_USE_STRING_MODULE
#undef BE_USE_JSON_MODULE
#undef BE_USE_MATH_MODULE
#undef BE_USE_GLOBAL_MODULE

#define BE_USE_STRING_MODULE            1
#define BE_USE_JSON_MODULE              1
#define BE_USE_MATH_MODULE              1
#define BE_USE_GLOBAL_MODULE            1

#endif
