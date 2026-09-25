#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture a restore image from a TC002, reading only.

This is the gate in `docs/bring-up.md` and in ADR 0008: nothing gets deployed,
and certainly nothing gets flashed, until a verified image of the device's own
flash exists somewhere safe.

It has to be your device's image. A restore image is Ulanzi's filesystem — we
cannot publish one, cannot legally redistribute one, and cannot make one for
you. The only copy that will ever exist for this unit is the one taken while it
still works.

**Reads only.** Every partition is read through its `/dev/mtdNro` node, which
the kernel exposes specifically as a read-only view of the same flash. Nothing
is written to the device, no service is stopped, and no property is set — the
capture can be taken on a healthy stock device with nothing at stake, which is
exactly when it should be taken.

Usage:
    python.exe tooling/probe/capture.py 192.168.1.238
    python.exe tooling/probe/capture.py 192.168.1.238 --out D:/stipple-backups
    python.exe tooling/probe/capture.py --verify D:/stipple-backups/tc002-...
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Anything that could write. `push` is absent on purpose, and so is `dd`: a
# read-only node read by `pull` cannot be pointed at the wrong destination by a
# typo, and `dd` can.
ADB_READ_ONLY = ("shell", "pull", "connect", "devices")

CHUNK = 1024 * 1024


def adb(args: list[str], serial: str | None = None, binary: bool = False):
    if args[0] not in ADB_READ_ONLY:
        raise SystemExit(f"capture.py refuses to run `adb {args[0]}`: it only reads.")
    command = ["adb"] + (["-s", serial] if serial else []) + args
    return subprocess.run(command, capture_output=True, text=not binary)


def partitions(serial: str) -> list[dict]:
    """Parse /proc/mtd into name, index and expected size."""
    out = adb(["shell", "cat /proc/mtd"], serial).stdout
    found = []
    for line in out.splitlines():
        # mtd3: 00800000 00010000 "res"
        match = re.match(r'mtd(\d+):\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"([^"]+)"', line.strip())
        if match:
            found.append({
                "index": int(match.group(1)),
                "bytes": int(match.group(2), 16),
                "erase": int(match.group(3), 16),
                "name": match.group(4),
            })
    return found


def capture_one(serial: str, part: dict, target: Path) -> dict:
    """Pull one partition to a local file and hash it.

    `adb pull` is the only transport on this device that moves bytes intact, and
    establishing that was most of the work:

    - `adb exec-out` is not supported by this adbd at all. It answers
      "error: closed" and produces nothing, which at least fails loudly.
    - `adb shell cat` *appears* to work and silently corrupts. Reading the
      262144-byte MISC partition returned 262402 bytes, because line feeds in
      the binary were translated to CRLF on the way out. An image damaged this
      way looks fine until the day it is needed, which is the worst possible
      time to discover it.
    - `adb pull` reads the character device directly, byte for byte.

    The size check below is not belt-and-braces. It is what caught that.
    """
    # /dev/mtd is a directory and the nodes live inside it. Reading the `ro`
    # node means the kernel refuses writes, rather than this script promising
    # not to make any.
    node = f"/dev/mtd/mtd{part['index']}ro"

    print(f"    {part['name']:<10} {part['bytes'] / 1024:>8.0f} KB ", end="", flush=True)
    result = adb(["pull", node, str(target)], serial)

    if not target.exists():
        print("FAILED")
        print(f"      {(result.stderr or '').strip()}")
        return {
            "name": part["name"], "node": node,
            "expectedBytes": part["bytes"], "actualBytes": 0,
            "sha256": "", "complete": False,
        }

    digest = hashlib.sha256()
    written = 0
    with target.open("rb") as handle:
        for block in iter(lambda: handle.read(CHUNK), b""):
            digest.update(block)
            written += len(block)

    print("ok" if written == part["bytes"] else f"SHORT ({written})")

    return {
        "name": part["name"],
        "node": node,
        "expectedBytes": part["bytes"],
        "actualBytes": written,
        "sha256": digest.hexdigest(),
        "complete": written == part["bytes"],
    }


def do_capture(address: str | None, out_root: Path) -> int:
    if shutil.which("adb") is None:
        print("adb not found on PATH.", file=sys.stderr)
        return 2

    if address:
        target = address if ":" in address else f"{address}:5555"
        print(adb(["connect", target]).stdout.strip())

    devices = adb(["devices"]).stdout
    attached = [line.split()[0] for line in devices.splitlines()[1:]
                if line.strip() and line.split()[-1] == "device"]
    if not attached:
        print("No device attached.", file=sys.stderr)
        return 1
    serial = attached[0]

    base = json.loads(adb(["shell", "cat /getBase 2>/dev/null"], serial).stdout or "{}")
    info = adb(["shell", "getprop ro.build.date"], serial).stdout.strip()

    parts = partitions(serial)
    if not parts:
        print("Could not read /proc/mtd — refusing to capture a partial image.",
              file=sys.stderr)
        return 1

    total = sum(p["bytes"] for p in parts)
    print(f"\n{len(parts)} partitions, {total / 1024 / 1024:.1f} MB total\n")

    stamp = info or "unknown"
    folder = out_root / f"tc002-restore-{stamp}"
    folder.mkdir(parents=True, exist_ok=True)

    results = []
    for part in parts:
        results.append(capture_one(serial, part, folder / f"{part['index']:02d}-{part['name']}.bin"))

    manifest = {
        "device": "TC002",
        "buildDate": info,
        "partitions": results,
        # Identifiers are deliberately not recorded: a manifest gets shared when
        # something goes wrong, and it should not carry a serial or an SSID.
        "note": "Restore image of vendor firmware. Do not publish or redistribute.",
    }
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    incomplete = [r for r in results if not r["complete"]]
    print()
    if incomplete:
        print("INCOMPLETE — this image must not be trusted:", file=sys.stderr)
        for r in incomplete:
            print(f"  {r['name']}: got {r['actualBytes']} of {r['expectedBytes']}",
                  file=sys.stderr)
        print("\nRe-run. A short read here means a restore that bricks the device.",
              file=sys.stderr)
        return 1

    print(f"Captured to {folder}")
    print(f"  {len(results)} partitions, all sizes match /proc/mtd")
    print("\nNow move this off this machine. It is the only copy that will ever exist.")
    print("It is vendor firmware: do not publish it.")
    return 0


def do_verify(folder: Path) -> int:
    """Re-hash a stored capture against its manifest."""
    manifest_path = folder / "manifest.json"
    if not manifest_path.exists():
        print(f"No manifest at {manifest_path}", file=sys.stderr)
        return 1

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    bad = 0
    for entry in manifest["partitions"]:
        path = folder / f"{entry['name']}.bin"
        candidates = list(folder.glob(f"*{entry['name']}.bin"))
        if candidates:
            path = candidates[0]
        if not path.exists():
            print(f"  MISSING  {entry['name']}")
            bad += 1
            continue

        digest = hashlib.sha256()
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(CHUNK), b""):
                digest.update(block)

        if digest.hexdigest() == entry["sha256"]:
            print(f"  ok       {entry['name']}  ({entry['actualBytes']} bytes)")
        else:
            print(f"  CORRUPT  {entry['name']}")
            bad += 1

    print()
    if bad:
        print(f"{bad} partition(s) do not match. This image cannot be trusted.",
              file=sys.stderr)
        return 1
    print("Every partition matches its recorded hash.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", nargs="?", help="device IP, or host:port")
    parser.add_argument("--out", default="../stipple-backups",
                        help="where to write (default: ../stipple-backups, outside the repo)")
    parser.add_argument("--verify", metavar="FOLDER",
                        help="re-hash an existing capture instead of taking one")
    args = parser.parse_args()

    if args.verify:
        return do_verify(Path(args.verify))
    return do_capture(args.address, Path(args.out))


if __name__ == "__main__":
    sys.exit(main())
