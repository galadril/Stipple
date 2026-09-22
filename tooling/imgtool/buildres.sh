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
STARTUP_LIB="${3:-/data/notrix/libnotrix.so}"

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
echo "This image has no NOTRIX in it. It changes where the framework looks,"
echo "and NOTRIX goes to $STARTUP_LIB separately."
