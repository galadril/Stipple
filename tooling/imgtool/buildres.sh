#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rebuild a TC002 `res` partition with one line changed.
#
# **The image this produces contains no NOTRIX code.** That is the point of
# ADR 0021: the only difference from the captured original is the
# `startupLibPath` field in /res/etc/EasyUI.cfg, pointed at a writable
# location. NOTRIX itself lives in /data and is updated by copying a file,
# so this image is built once and is the same for every release after.
#
# It runs inside the pinned container and not on the host, for a reason that
# is not obvious: the `res` filesystem stores uid/gid 1000 and modes like
# 0770, and extracting it onto a bind mount from Windows silently flattens
# both to root/0777. Rebuilding from that would change the ownership of
# every file on the partition. Everything here stays on the container's own
# filesystem, and only the finished image is copied out.
#
# Nothing here touches a device.

set -euo pipefail

CAPTURE="${1:?usage: buildres.sh <res-raw.bin> <output.squashfs> [startupLibPath]}"
OUTPUT="${2:?usage: buildres.sh <res-raw.bin> <output.squashfs> [startupLibPath]}"
# Where the framework is pointed. Defaults to the shim rather than straight
# at NOTRIX, because a missing NOTRIX must leave a working clock rather than
# a device with no way in - see ADR 0008 for what happens otherwise.
STARTUP_LIB="${3:-/res/lib/libnotrixboot.so}"
SHIM_SOURCE="/src/firmware/tools/startup_shim/main.cpp"

CONFIG="etc/EasyUI.cfg"
VENDOR_LIB="/res/lib/libzkgui.so"

WORK="$(mktemp -d /work-XXXXXX 2>/dev/null || mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "capture     $CAPTURE"
echo "startup     $STARTUP_LIB"

unsquashfs -d "$WORK/tree" "$CAPTURE" > /dev/null

if [ ! -f "$WORK/tree/$CONFIG" ]; then
    echo "error: $CONFIG is not in this image; is it really a res partition?" >&2
    exit 1
fi

if ! grep -q "$VENDOR_LIB" "$WORK/tree/$CONFIG"; then
    # Refused rather than guessed at. A config that does not say what was
    # expected is one nobody has looked at, and editing it blind is how a
    # device ends up with a startupLibPath pointing nowhere.
    echo "error: $CONFIG does not contain $VENDOR_LIB" >&2
    echo "       refusing to edit a configuration this tool does not recognise" >&2
    exit 1
fi

# Rewritten in place, preserving ownership and mode, because those are part
# of what makes this image identical to the original everywhere else.
#
# The timestamps are put back too. That is not tidiness: an image whose only
# difference is one line of one file can be checked by diffing it against the
# capture, and a reviewer should not have to mentally discount two changed
# mtimes to see that. `diff -rq` against the original should print exactly
# one line, and it does.
sed -i "s|$VENDOR_LIB|$STARTUP_LIB|" "$WORK/tree/$CONFIG"
touch -r "$WORK/tree/lib" "$WORK/tree/$CONFIG"
touch -r "$WORK/tree/lib" "$WORK/tree/etc"

echo "--- the only change ---"
grep startupLibPath "$WORK/tree/$CONFIG"

# The vendor application is deliberately left in place. Nothing overwrites
# it, so the stock experience is one config field away - and a NOTRIX that
# will not load leaves a device that still boots rather than one that does
# not.
if [ ! -f "$WORK/tree/lib/libzkgui.so" ]; then
    echo "warning: the vendor application is missing from this image" >&2
fi

# Build the shim into the image, linked against the vendor application.
#
# That link is the whole mechanism: the framework calls dlsym on the handle
# it opened, dlsym searches a handle's dependency tree, and so the vendor's
# entry points are found through the shim without anything here knowing what
# they are called. Their names are obfuscated and stay irrelevant.
#
# Built here rather than shipped, because it links against the vendor library
# from *this* capture - which never leaves the machine it was captured on.
if [ "$STARTUP_LIB" = "/res/lib/libnotrixboot.so" ]; then
    if [ ! -f "$SHIM_SOURCE" ]; then
        echo "error: $SHIM_SOURCE is missing" >&2
        exit 1
    fi
    echo "--- building the startup shim ---"
    arm-linux-gnueabihf-g++ -shared -fPIC -Os -std=c++17         -o "$WORK/tree/lib/libnotrixboot.so" "$SHIM_SOURCE"         -L"$WORK/tree/lib" -Wl,--no-as-needed -l:libzkgui.so -Wl,--as-needed -ldl

    # Ownership and mode have to match everything else on this partition, or
    # the image stops being one line different from the original.
    chown --reference="$WORK/tree/lib/libzkgui.so" "$WORK/tree/lib/libnotrixboot.so"
    chmod --reference="$WORK/tree/lib/libzkgui.so" "$WORK/tree/lib/libnotrixboot.so"

    # Timestamps taken from the vendor library beside it, and the directory's
    # own mtime put back after the write. Adding a file bumps the parent
    # directory, and an image whose only differences are structural is one a
    # reviewer can check by diffing - they should not have to discount dates.
    touch -r "$WORK/tree/lib/libzkgui.so" "$WORK/tree/lib/libnotrixboot.so"
    touch -r "$WORK/tree/lib/libzkgui.so" "$WORK/tree/lib"

    # Checked, not assumed. --as-needed is the default and drops a library
    # whose symbols are never referenced - which is exactly this one, since
    # the whole point is to forward symbols we never name. Without the
    # DT_NEEDED entry dlsym finds nothing and the fallback delivers the very
    # lockout it exists to prevent.
    if ! arm-linux-gnueabihf-readelf -d "$WORK/tree/lib/libnotrixboot.so"         | grep -q 'NEEDED.*libzkgui\.so'; then
        echo "error: the shim does not depend on libzkgui.so" >&2
        echo "       without that link the vendor application cannot be reached" >&2
        exit 1
    fi

    echo "    $(ls -la "$WORK/tree/lib/libnotrixboot.so" | awk '{print $5}') bytes"
    echo "    needs: $(arm-linux-gnueabihf-readelf -d "$WORK/tree/lib/libnotrixboot.so"         | grep NEEDED | awk '{print $5}' | tr -d '[]' | tr '
' ' ')"
fi

# Put NOTRIX itself in the image.
#
# This is the difference between an image that installs NOTRIX and one that
# merely *points* at it. The earlier design shipped only the shim and left
# NOTRIX in /data, which is fine right up until /data holds an old copy or no
# copy: the flash succeeds, the shim loads whatever is in /data, and the
# device runs last week's build - or the stock clock - with nothing to say
# why. That happened twice on real hardware and was misread as a bad flash
# both times.
#
# Bundling it also breaks a bootstrap trap. The fixes that let NOTRIX bring
# up its own Wi-Fi cannot be delivered over Wi-Fi, so they have to arrive in
# the image.
if [ "$STARTUP_LIB" = "/res/lib/libnotrixboot.so" ]; then
    NOTRIX_LIB="${NOTRIX_LIB:-/src/build/device-arm/firmware/libnotrix.so}"
    if [ ! -f "$NOTRIX_LIB" ]; then
        # Refused rather than warned about. An image without this is one that
        # boots to the stock clock, and the person flashing it would have no
        # way to tell that from a NOTRIX that failed to start.
        echo "error: $NOTRIX_LIB is missing" >&2
        echo "       build it first: cmake --build --preset device-arm --target notrix_startup" >&2
        exit 1
    fi

    echo "--- bundling NOTRIX ---"
    cp "$NOTRIX_LIB" "$WORK/tree/lib/libnotrix.so"
    chown --reference="$WORK/tree/lib/libzkgui.so" "$WORK/tree/lib/libnotrix.so"
    chmod --reference="$WORK/tree/lib/libzkgui.so" "$WORK/tree/lib/libnotrix.so"
    touch -r "$WORK/tree/lib/libzkgui.so" "$WORK/tree/lib/libnotrix.so"
    touch -r "$WORK/tree/lib/libzkgui.so" "$WORK/tree/lib"

    # Checked on the way in, because a library built for the wrong
    # architecture fails at dlopen on the device - by which point the only
    # evidence is one line in a log on a machine with no network.
    if ! arm-linux-gnueabihf-readelf -h "$WORK/tree/lib/libnotrix.so" | grep -q "ARM"; then
        echo "error: $NOTRIX_LIB is not an ARM shared library" >&2
        exit 1
    fi
    echo "    $(ls -la "$WORK/tree/lib/libnotrix.so" | awk '{print $5}') bytes"
fi

# Parameters read off the original rather than chosen: squashfs 4.0, xz,
# 128 KiB blocks. The kernel that mounts this was built with a fixed set of
# decompressors, and an image it cannot read is a device with no application.
mksquashfs "$WORK/tree" "$WORK/out.squashfs" \
    -comp xz -b 131072 -noappend -no-progress > /dev/null

cp "$WORK/out.squashfs" "$OUTPUT"

echo "--- built ---"
ls -la "$OUTPUT" | awk '{print "size        " $5 " bytes"}'
echo "wrote       $OUTPUT"
echo
if [ "$STARTUP_LIB" = "/res/lib/libnotrixboot.so" ]; then
    echo "The image carries the shim and NOTRIX itself, so flashing it is the"
    echo "whole install - nothing has to be copied afterwards and no network"
    echo "is needed. The shim prefers /data/notrix/libnotrix.so.override if"
    echo "somebody put one there, falls back to the bundled copy, and falls"
    echo "back again to the stock clock rather than to nothing."
else
    echo "This image has no NOTRIX in it. It changes where the framework looks,"
    echo "and NOTRIX goes to $STARTUP_LIB separately."
    echo
    echo "WARNING: pointing straight at /data means a missing NOTRIX leaves the"
    echo "         device with no application, and therefore no network and no"
    echo "         way in. See docs/adr/0008-installer-helper.md."
fi
