#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read, verify and build TC002 ``update.img`` containers.

**Why this exists, and why it is not an MTD writer.**

The obvious way to make STIPPLE persist is to write flash directly: the raw
character devices are there, the part is NOR so there are no bad blocks, and
it is about a hundred and fifty lines. It is also the wrong answer, because
the failure mode is a clock nobody can recover without opening it.

The device already has an update mechanism, and it is better than anything we
would write:

* it validates a header CRC32 and a payload MD5 before touching flash;
* it writes only the ``res`` partition directly, which is the one that can be
  broken without stopping the device from booting to a shell;
* recovery is a **physical button** - hold the reset key for five seconds and
  it reflashes from ``/mnt/storage``, needing no network and no computer;
* ``/mnt/storage`` is the USB mass-storage volume, so putting an image there
  is drag-and-drop from any machine.

So STIPPLE ships an ``update.img`` and uses the vendor's own path. That is the
difference between a restore procedure we hope works and one the device has
been doing since it left the factory.

**Everything here was verified against a real image pulled from a real
device**, not taken from a description of the format. See
docs/research/tc002-platform-findings.md.

**Nothing in this file writes to a device.** It reads and writes ordinary
files. Putting an image on a clock is a separate, deliberate step.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import zlib
from pathlib import Path

# --- the format --------------------------------------------------------------
#
# Offsets confirmed byte for byte against the factory update.img on a TC002
# running Zkswe_SSD21X_SPINOR.

HEADER_BYTES = 0x23C  # 572

OFF_MAGIC = 0x00  # b"ZKSWEV1.0-YYMMDD"; only the first nine bytes are checked
OFF_PREFIX_LEN = 0x10  # 0x30
OFF_ENTRY_COUNT = 0x11  # 1
OFF_PARTITION = 0x14  # MTD index; 3 is "res"
OFF_PAYLOAD_AT = 0x18  # where the payload starts, == HEADER_BYTES
OFF_PAYLOAD_LEN = 0x1C
OFF_RELOCATED = 0x20  # the real first 16 bytes of the image
OFF_DEVICE_CODE = 0x35  # 0xaa550606, little-endian, and yes it is unaligned
OFF_HEADER_CRC = 0x238  # CRC32 over header[0:0x238]

MAGIC = b"ZKSWEV1.0"
DEVICE_CODE = 0xAA550606
RES_PARTITION = 3

# The squashfs's first sixteen bytes live in the header, and the MD5 of the
# whole image sits where they used to be. Both are sixteen bytes, which is
# presumably why it was done this way.
RELOCATED_BYTES = 16

# The res partition is 8 MiB. An image larger than that cannot be written and
# should be refused here rather than half-way through flashing a clock.
RES_PARTITION_BYTES = 0x800000


class Damaged(Exception):
    """The file is not a usable update.img."""


def _u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def parse(data: bytes) -> dict:
    """Pull the header apart, checking everything that can be checked."""
    if len(data) < HEADER_BYTES:
        raise Damaged(f"too short to be an update.img ({len(data)} bytes)")
    head = data[:HEADER_BYTES]

    if head[:len(MAGIC)] != MAGIC:
        raise Damaged(f"magic is {head[:16]!r}, expected {MAGIC!r}...")

    device = _u32(head, OFF_DEVICE_CODE)
    if device != DEVICE_CODE:
        # A different device code means a different board. The loader checks
        # this too, but finding out here beats finding out from a clock.
        raise Damaged(f"device code is 0x{device:08x}, expected 0x{DEVICE_CODE:08x}")

    payload_at = _u32(head, OFF_PAYLOAD_AT)
    payload_len = _u32(head, OFF_PAYLOAD_LEN)
    if payload_at != HEADER_BYTES:
        raise Damaged(f"payload starts at 0x{payload_at:x}, expected 0x{HEADER_BYTES:x}")
    if payload_at + payload_len != len(data):
        raise Damaged(
            f"header says {payload_at + payload_len} bytes, file is {len(data)}")

    stored_crc = _u32(head, OFF_HEADER_CRC)
    actual_crc = zlib.crc32(head[:OFF_HEADER_CRC]) & 0xFFFFFFFF
    if stored_crc != actual_crc:
        raise Damaged(f"header CRC32 is 0x{stored_crc:08x}, computed 0x{actual_crc:08x}")

    payload = data[payload_at:]
    relocated = head[OFF_RELOCATED:OFF_RELOCATED + RELOCATED_BYTES]

    # The MD5 covers the image as it will be written - that is, with the real
    # first sixteen bytes back in place, not as the bytes sit in this file.
    restored = relocated + payload[RELOCATED_BYTES:]
    stored_md5 = payload[:RELOCATED_BYTES]
    actual_md5 = hashlib.md5(restored).digest()
    if stored_md5 != actual_md5:
        raise Damaged(
            f"payload MD5 is {stored_md5.hex()}, computed {actual_md5.hex()}")

    return {
        "magic": head[:16].decode("ascii", "replace"),
        "partition": head[OFF_PARTITION],
        "entries": head[OFF_ENTRY_COUNT],
        "payload_len": payload_len,
        "header_crc": stored_crc,
        "md5": stored_md5.hex(),
        "image": restored,
        "header": head,
    }


def build(image: bytes, template: bytes, partition: int = RES_PARTITION) -> bytes:
    """Wrap `image` in a container, reusing `template`'s header.

    **The vendor header is kept and edited rather than written from scratch**,
    and that is deliberate. Large parts of it are not understood - there is a
    table of some sort from 0x60 onwards that this tool has no business
    inventing. Changing only the five fields that must change keeps everything
    else exactly as the loader has always seen it.
    """
    if len(image) < RELOCATED_BYTES:
        raise Damaged("image is too small to be a filesystem")
    if len(image) > RES_PARTITION_BYTES:
        raise Damaged(
            f"image is {len(image)} bytes; the res partition holds "
            f"{RES_PARTITION_BYTES}")
    if image[:4] != b"hsqs":
        # Every res image seen so far is squashfs. This is a warning wearing a
        # refusal's clothes: if a future one is not, this line should be the
        # thing that gets changed, deliberately, by somebody who knows why.
        raise Damaged(f"image does not start with squashfs magic (got {image[:4]!r})")

    # Padded to 4 KiB, as the vendor's own images are.
    padded = image + b"\x00" * (-len(image) % 4096)

    head = bytearray(template[:HEADER_BYTES])
    head[OFF_PARTITION] = partition
    struct.pack_into("<I", head, OFF_PAYLOAD_AT, HEADER_BYTES)
    struct.pack_into("<I", head, OFF_PAYLOAD_LEN, len(padded))
    head[OFF_RELOCATED:OFF_RELOCATED + RELOCATED_BYTES] = padded[:RELOCATED_BYTES]

    payload = bytearray(padded)
    payload[:RELOCATED_BYTES] = hashlib.md5(padded).digest()

    # Last, because it covers everything above it.
    struct.pack_into("<I", head, OFF_HEADER_CRC,
                     zlib.crc32(bytes(head[:OFF_HEADER_CRC])) & 0xFFFFFFFF)

    return bytes(head) + bytes(payload)


# --- commands ----------------------------------------------------------------

def cmd_info(args) -> int:
    data = Path(args.image).read_bytes()
    try:
        info = parse(data)
    except Damaged as problem:
        print(f"not usable: {problem}", file=sys.stderr)
        return 1

    print(f"file          {args.image}")
    print(f"magic         {info['magic']}")
    print(f"partition     {info['partition']}"
          f"{'  (res)' if info['partition'] == RES_PARTITION else ''}")
    print(f"entries       {info['entries']}")
    print(f"payload       {info['payload_len']} bytes")
    print(f"header CRC32  0x{info['header_crc']:08x}  ok")
    print(f"payload MD5   {info['md5']}  ok")
    print(f"filesystem    {info['image'][:4]!r}"
          f"{'  (squashfs)' if info['image'][:4] == b'hsqs' else ''}")
    return 0


def cmd_extract(args) -> int:
    data = Path(args.image).read_bytes()
    try:
        info = parse(data)
    except Damaged as problem:
        print(f"not usable: {problem}", file=sys.stderr)
        return 1
    Path(args.out).write_bytes(info["image"])
    print(f"wrote {len(info['image'])} bytes to {args.out}")
    print("this is the partition image exactly as the loader would write it")
    return 0


def cmd_pack(args) -> int:
    image = Path(args.filesystem).read_bytes()
    template = Path(args.template).read_bytes()
    try:
        parse(template)  # refuse to copy a header out of a broken file
        packed = build(image, template, args.partition)
        parse(packed)  # and refuse to emit one we cannot read back
    except Damaged as problem:
        print(f"refused: {problem}", file=sys.stderr)
        return 1

    Path(args.out).write_bytes(packed)
    print(f"wrote {len(packed)} bytes to {args.out}")
    print(f"  partition   {args.partition}")
    print(f"  payload     {len(packed) - HEADER_BYTES} bytes")
    print("verified by re-reading it; the device will check the same two sums")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Read, verify and build TC002 update.img containers. "
                    "Touches files only - never a device.")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("info", help="check an image and describe it")
    p.add_argument("image")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("extract", help="write out the partition image inside")
    p.add_argument("image")
    p.add_argument("out")
    p.set_defaults(func=cmd_extract)

    p = sub.add_parser("pack", help="wrap a filesystem image in a container")
    p.add_argument("filesystem", help="squashfs image to wrap")
    p.add_argument("out")
    p.add_argument("--template", required=True,
                   help="a known-good update.img to take the header from")
    p.add_argument("--partition", type=int, default=RES_PARTITION)
    p.set_defaults(func=cmd_pack)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
