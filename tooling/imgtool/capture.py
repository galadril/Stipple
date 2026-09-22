#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture a restore image from one device, and verify it before believing it.

ADR 0008 will not allow anything to be flashed until there is a verified
restore image **taken from the unit in front of you**. This is that step, and
it is worth being precise about why it cannot be skipped.

Every TC002 ships with an ``update.img`` on its own USB volume, and holding
the reset button installs it. That looks like a recovery path and is not a
reliable one: on the unit this was written against, **the shipped image is
older than the ``res`` partition actually running**, so the reset button is a
downgrade rather than a restore. The third-party TC002 documentation reports
the same thing on their unit. Devices differ, which is the point - the only
image known to match a device is the one read out of it.

So this reads the live ``res`` partition through the kernel's **read-only**
alias, trims it to the filesystem it contains, wraps it in a container the
loader will accept, and checks the result end to end before saying it worked.

**Nothing here writes to the device.** It reads `/dev/mtd/mtd3ro`, and the
one file it creates on the device is a copy in `/tmp`, which it removes.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import imgtool  # noqa: E402

RES_INDEX = 3
# The kernel exposes each partition twice; the "ro" alias cannot write even if
# something tried. Reading through it is free insurance.
RES_NODE = f"/dev/mtd/mtd{RES_INDEX}ro"
SQUASHFS_BYTES_USED_AT = 0x28


def adb(args: list[str], target: str | None, binary: bool = False):
    command = ["adb"]
    if target:
        command += ["-s", target]
    command += args
    return subprocess.run(command, check=True,
                          stdout=subprocess.PIPE if binary else None)


def adb_text(args: list[str], target: str | None) -> str:
    command = ["adb"]
    if target:
        command += ["-s", target]
    command += args
    done = subprocess.run(command, check=True, stdout=subprocess.PIPE)
    return done.stdout.decode("utf-8", "replace").replace("\r", "")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Capture a verified restore image from a TC002. "
                    "Reads the device; never writes to it.")
    parser.add_argument("--target", help="adb device, e.g. 192.168.1.238:5555")
    parser.add_argument("--out", default="restore",
                        help="directory to write into (default: restore/)")
    args = parser.parse_args(argv)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    # Identify the unit. Not decoration: a restore image belongs to exactly
    # one device, and a folder full of unlabelled .img files is how somebody
    # eventually flashes the wrong one.
    model = adb_text(["shell", "getprop ro.product.model"], args.target).strip()
    print(f"model            {model}")
    if "SSD21X" not in model:
        print("this does not look like a TC002; refusing", file=sys.stderr)
        return 1

    print(f"reading          {RES_NODE} (read-only alias)")
    # Copied on the device first and then pulled as an ordinary file: this
    # adbd predates `exec-out`, and a binary stream through `adb shell` comes
    # back with its line endings helpfully corrupted.
    adb(["shell", f"cat {RES_NODE} > /tmp/notrix-res-capture.bin"], args.target)
    adb(["pull", "/tmp/notrix-res-capture.bin", str(out / "res-raw.bin")], args.target)
    adb(["shell", "rm -f /tmp/notrix-res-capture.bin"], args.target)

    raw = (out / "res-raw.bin").read_bytes()
    if raw[:4] != b"hsqs":
        print(f"the partition does not start with squashfs magic ({raw[:4]!r})",
              file=sys.stderr)
        return 1

    used = struct.unpack_from("<Q", raw, SQUASHFS_BYTES_USED_AT)[0]
    if not (0 < used <= len(raw)):
        print(f"squashfs claims {used} bytes inside a {len(raw)} byte partition",
              file=sys.stderr)
        return 1

    filesystem = raw[:used]
    print(f"partition        {len(raw)} bytes")
    print(f"filesystem       {used} bytes")
    print(f"sha256           {hashlib.sha256(filesystem).hexdigest()}")

    # The header is taken from the device's own shipped image. Large parts of
    # it are not understood, and copying them is safer than inventing them.
    shipped = out / "shipped-update.img"
    adb(["pull", "/mnt/storage/update.img", str(shipped)], args.target)
    template = shipped.read_bytes()
    try:
        imgtool.parse(template)
    except imgtool.Damaged as problem:
        print(f"the shipped update.img is unusable as a template: {problem}",
              file=sys.stderr)
        return 1

    packed = imgtool.build(filesystem, template, RES_INDEX)
    restore = out / "restore-res.img"
    restore.write_bytes(packed)

    # Read it back rather than trusting what was just written. The device will
    # check the same two sums, and finding out here is free.
    info = imgtool.parse(packed)
    if info["image"][:used] != filesystem:
        print("the packed image does not contain what was captured", file=sys.stderr)
        return 1

    print()
    print(f"wrote            {restore}  ({len(packed)} bytes)")
    print(f"  header CRC32   0x{info['header_crc']:08x}  ok")
    print(f"  payload MD5    {info['md5']}  ok")
    print("  contents       verified against the capture")

    # And the finding that makes this whole exercise necessary.
    try:
        shipped_info = imgtool.parse(template)
        if shipped_info["image"][:used] == filesystem:
            print()
            print("the shipped udisk image matches what is running on this unit")
        else:
            print()
            print("NOTE: the update.img on this unit's own USB volume is NOT what")
            print("      it is running. Holding the reset button would install that")
            print("      one instead - a different version, most likely older.")
            print("      Use the image captured here, not the shipped one.")
    except imgtool.Damaged:
        pass

    print()
    print("Keep this file. It is specific to this device and is Ulanzi's")
    print("firmware, so it is gitignored and must not be redistributed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
