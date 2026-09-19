#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check that a built artifact can actually load on the device.

Cross-compiling successfully proves nothing about whether the result will run.
A binary linked against a newer C or C++ runtime than the device carries will
build cleanly, copy across happily, and then fail at load time with a message
naming a symbol version — which sends everyone looking at the wrong thing.

This answers the question directly: take every versioned symbol the artifact
imports, take every versioned symbol the device's own libraries export, and
report anything in the first set that is missing from the second.

That is deliberately not the same as comparing version numbers. "The device has
glibc 2.30 and we need 2.28, so we are fine" is an inference. This is a lookup.

Reads only, on both sides. Device libraries are pulled, never pushed.

Usage:
    python.exe tooling/probe/abi-check.py build/device-arm/firmware/libnotrix.so
    python.exe tooling/probe/abi-check.py <artifact> --address 192.168.1.238
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# The runtime libraries an artifact of ours could plausibly import from. Pulled
# from the device so the comparison is against what is really installed.
DEVICE_LIBRARIES = (
    "/lib/libc.so.6",
    "/lib/libstdc++.so.6",
    "/lib/libgcc_s.so.1",
    "/lib/libm.so.6",
    "/lib/libpthread.so.0",
    "/lib/libdl.so.2",
)

CONTAINER = "notrix-cross:bookworm"


def container_engine() -> str:
    for candidate in ("podman", "docker"):
        try:
            subprocess.run([candidate, "--version"], capture_output=True, check=True)
            return candidate
        except (OSError, subprocess.CalledProcessError):
            continue
    raise SystemExit("podman or docker is needed to run readelf for ARM. See tooling/cross/.")


def readelf(engine: str, repo: Path, relative: str, dynamic_only: bool) -> set[str]:
    """Versioned symbols an ELF file imports (UND) or exports."""
    script = (
        f'arm-linux-gnueabihf-readelf --dyn-syms -W "/src/{relative}" 2>/dev/null'
    )
    result = subprocess.run(
        [engine, "run", "--rm", "-v", f"{repo}:/src", CONTAINER, "bash", "-c", script],
        capture_output=True, text=True)

    symbols = set()
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) < 8 or "@" not in parts[7]:
            continue
        undefined = parts[6] == "UND"
        if undefined == dynamic_only:
            symbols.add(parts[7].replace("@@", "@"))
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", help="the .so or executable to check")
    parser.add_argument("--address", help="device IP, to refresh the pulled libraries")
    parser.add_argument("--libs", default="build/vendor-analysis",
                        help="where device libraries are kept")
    args = parser.parse_args()

    repo = Path.cwd()
    artifact = Path(args.artifact)
    if not artifact.exists():
        print(f"No such artifact: {artifact}", file=sys.stderr)
        return 2

    lib_dir = Path(args.libs)
    lib_dir.mkdir(parents=True, exist_ok=True)

    if args.address:
        target = args.address if ":" in args.address else f"{args.address}:5555"
        subprocess.run(["adb", "connect", target], capture_output=True)
        for path in DEVICE_LIBRARIES:
            # Resolve the symlink device-side; pulling a symlink gets a symlink.
            real = subprocess.run(
                ["adb", "-s", target, "shell", f"readlink -f {path}"],
                capture_output=True, text=True).stdout.strip() or path
            name = Path(real).name
            subprocess.run(["adb", "-s", target, "pull", real, str(lib_dir / name)],
                           capture_output=True)

    present = sorted(p for p in lib_dir.iterdir()
                     if re.search(r"lib.*\.so", p.name) and p.is_file())
    if not present:
        print(f"No device libraries in {lib_dir}. Re-run with --address to fetch them.",
              file=sys.stderr)
        return 2

    engine = container_engine()

    def inside_repo(path: Path) -> str:
        # Paths are handed to a container that mounts the repo at /src, so they
        # have to be repo-relative. resolve() first: a relative argument and an
        # absolute one must reduce to the same thing.
        return path.resolve().relative_to(repo.resolve()).as_posix()

    provided: set[str] = set()
    for lib in present:
        provided |= readelf(engine, repo, inside_repo(lib), dynamic_only=False)

    needed = readelf(engine, repo, inside_repo(artifact), dynamic_only=True)

    print(f"artifact : {artifact}")
    print(f"needs    : {len(needed)} versioned symbols")
    print(f"device   : {len(provided)} exported, from {len(present)} libraries")
    print()

    missing = sorted(needed - provided)
    if missing:
        print(f"{len(missing)} symbol(s) the device cannot satisfy:")
        for symbol in missing:
            print(f"  {symbol}")
        print("\nThis artifact would fail to load. It needs an older toolchain, or")
        print("the offending dependency removed.")
        return 1

    print("Every imported symbol resolves against the device's own libraries.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
