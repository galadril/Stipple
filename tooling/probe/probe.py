#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only reconnaissance of a TC002.

Run this first, before anything else touches the device. It answers the
questions in blueprint §46 that have been guesses until now — RAM, partition
layout, which input devices exist, what the launcher actually is, which glibc is
installed — and writes them to a report you can read, keep, and compare against
after a change.

**This script only reads.** It runs no command that writes a file, stops a
service, sets a property or installs anything. That is deliberate and it is the
whole point: the first contact with a new device should not be able to damage
it, so this can be run on a stock device with nothing at stake. Every command it
executes is listed in READ_ONLY_COMMANDS below, and there is a test that asserts
none of them can mutate.

Usage:
    python.exe tooling/probe/probe.py                 # uses the only attached device
    python.exe tooling/probe/probe.py 192.168.1.50    # connect over Wi-Fi ADB first
    python.exe tooling/probe/probe.py --out report.md
"""

from __future__ import annotations

import argparse
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# An allowlist, not a denylist.
#
# The first attempt banned dangerous words and immediately rejected
# `cat /proc/mounts` for containing "mount" - which is the shape of that mistake
# in both directions: it refuses safe things, and it would happily pass anything
# whose name nobody thought to ban. Naming what may run is checkable; guessing
# what may not is not.
READ_ONLY_COMMANDS = frozenset({
    "cat", "ls", "df", "ps", "getprop", "ip", "ifconfig", "iw", "netstat",
    "find", "head", "tail", "grep", "echo", "true", "uname", "stat", "wc",
    "sort", "readlink", "dirname", "basename",
    # Running libc prints its own version banner and does nothing else. It is
    # the only binary here invoked by path, and the only reason to allow one.
    "/lib/libc.so.6",
})

# Where a new command can begin inside a shell line.
_COMMAND_SEPARATORS = ("|", "&&", "||", ";")


@dataclass
class Section:
    title: str
    why: str
    # (label, command) or (label, command, keywords).
    #
    # Keywords filter the output *here*, not on the device. The device shell is
    # busybox with a reduced applet set - it has no grep, head, du or `df -h` -
    # and a pipeline through a missing applet silently blanks a whole section
    # rather than failing loudly. Keeping device commands trivial removes that
    # class of hole entirely.
    commands: list[tuple] = field(default_factory=list)


# Ordered so the most decision-relevant answers come first: a report that gets
# truncated or skimmed should still carry the things that change what we build.
SECTIONS: list[Section] = [
    Section(
        "Identity and versions",
        "Every later report is worthless without these. A finding that does not "
        "name the stock-app and MCU version cannot be compared with anyone "
        "else's, including our own from last week.",
        [
            ("Stock app version", "getprop", "version build ulanzi zk mcu"),
            ("Kernel", "cat /proc/version"),
            ("Model / board", "cat /proc/device-tree/model 2>/dev/null || echo unknown"),
            ("All properties", "getprop"),
        ],
    ),
    Section(
        "Memory",
        "§46 and every budget in test_memory_budget.cpp are guesses until this "
        "runs. The 8 MiB figure we have is flash, not RAM.",
        [
            ("Memory", "cat /proc/meminfo"),
            ("Processes", "ps"),
        ],
    ),
    Section(
        "Storage and partitions",
        "The res partition is what an install writes. Its real size, and how "
        "full it already is, decide whether our binary fits before anyone "
        "tries it.",
        [
            ("Mounts", "cat /proc/mounts"),
            ("Free space", "df"),
            ("Partitions", "cat /proc/partitions"),
            ("MTD devices", "cat /proc/mtd 2>/dev/null || echo 'no /proc/mtd'"),
            ("By-name mapping", "ls -la /dev/block/by-name/ 2>/dev/null || echo 'none'"),
        ],
    ),
    Section(
        "CPU",
        "Confirms the Cortex-A7 assumption the cross-toolchain is built on, and "
        "settles whether 'Z21' and 'SSD21x' are the same thing.",
        [
            ("CPU", "cat /proc/cpuinfo"),
        ],
    ),
    Section(
        "Input devices",
        "This is what settles ADR 0016. Two sources disagree about whether there "
        "are two buttons or three; the kernel knows.",
        [
            ("Input devices", "cat /proc/bus/input/devices"),
            ("Event handlers", "ls -la /dev/input/ 2>/dev/null || echo 'none'"),
        ],
    ),
    Section(
        "The launcher and what is running",
        "We intend to replace the launcher. Worth knowing exactly what it is "
        "before planning to stop it.",
        [
            ("Services", "getprop", "init.svc"),
            ("Process list", "ps -A 2>/dev/null || ps"),
            ("Init scripts", "ls -la /etc/init.d/ 2>/dev/null || echo 'none'"),
        ],
    ),
    Section(
        "Userspace ABI",
        "Decides whether our statically linked binary was the right call, and "
        "what a dynamically linked one would need.",
        [
            ("libc", "ls -la /lib/libc* /lib/ld-* 2>/dev/null || echo 'none in /lib'"),
            ("libc version", "ls -la /lib/libc-*.so /lib/ld-*.so 2>/dev/null"),
            ("C++ runtime", "ls -la /lib/libstdc++* /usr/lib/libstdc++* 2>/dev/null"),
        ],
    ),
    Section(
        "Network",
        "First-time Wi-Fi provisioning is still unsolved. Whether this platform "
        "can bring up an access point at all is the single most useful unknown "
        "left, because it decides whether a flashed device that moves house has "
        "any way back.",
        [
            ("Interfaces", "ip addr 2>/dev/null || ifconfig -a"),
            ("Wi-Fi driver", "cat /proc/net/wireless 2>/dev/null || echo 'none'"),
            ("AP and DHCP tooling", "ls /bin /sbin /usr/sbin 2>/dev/null",
             "hostapd dnsmasq wpa_supplicant udhcpd iw"),
            ("wpa_supplicant", "ls -la /etc/wpa_supplicant* /data/misc/wifi 2>/dev/null "
                               "|| echo 'not found'"),
            ("Listening sockets", "cat /proc/net/tcp"),
        ],
    ),
    Section(
        "Audio and sensors",
        "One source reports a speaker and a microphone that reports volume, and "
        "no light sensor. Confirms what IAudioOutput has to bind to.",
        [
            ("Sound devices", "ls -la /dev/snd/ 2>/dev/null || echo 'none'"),
            ("ALSA cards", "cat /proc/asound/cards 2>/dev/null || echo 'none'"),
            ("I2C buses", "ls -la /dev/i2c* 2>/dev/null || echo 'none'"),
        ],
    ),
    Section(
        "Display",
        "How the panel is actually reached. The blueprint names "
        "PageBase::sendLedData; this is where we find out what that sits on.",
        [
            ("Framebuffers", "ls -la /dev/fb* 2>/dev/null || echo 'none'"),
            ("LED class", "ls -la /sys/class/leds/ 2>/dev/null || echo 'none'"),
            ("SPI devices", "ls -la /dev/spidev* 2>/dev/null || echo 'none'"),
            ("Serial ports", "ls -la /dev/ttyS* /dev/ttyUSB* 2>/dev/null || echo 'none'"),
        ],
    ),
    Section(
        "Writable space",
        "Tier 2 of ADR 0008 pushes to /tmp. Confirms it exists, is writable, and "
        "how much room it has - and that it really is volatile.",
        [
            ("tmp", "ls -ld /tmp /data /mnt 2>/dev/null"),
            ("tmpfs size", "df /tmp 2>/dev/null || echo 'no /tmp'"),
        ],
    ),
]


def invoked_binaries(command: str) -> list[str]:
    """Every binary a shell line would actually execute.

    Tokenised rather than split on punctuation: `grep -E 'version|build'` has a
    pipe inside a quoted pattern, and treating that as a command separator
    reports "build" as a binary. shlex knows the difference.
    """
    tokens = shlex.split(command, posix=True)

    found = []
    expecting_command = True
    for token in tokens:
        if token in _COMMAND_SEPARATORS:
            expecting_command = True
            continue
        if expecting_command:
            found.append(token)
            expecting_command = False
    return found


def check_read_only() -> None:
    """Refuse to run if anything here could modify the device.

    Called before the first command is sent, so a mutating command added in
    future fails on a developer's machine rather than on someone's clock.
    """
    for section in SECTIONS:
        for label, command, *_ in section.commands:
            # Redirection writes files even when the binary is harmless.
            # `2>/dev/null` is the exception: it discards stderr so a missing
            # path reports as absent rather than as an error, and it creates
            # nothing.
            redirects = command.replace("2>/dev/null", "")
            if ">" in redirects:
                raise SystemExit(
                    f"probe.py must stay read-only, but {section.title!r} / "
                    f"{label!r} redirects output:\n    {command}"
                )
            for binary in invoked_binaries(command):
                if binary not in READ_ONLY_COMMANDS:
                    raise SystemExit(
                        f"probe.py must stay read-only, but {section.title!r} / "
                        f"{label!r} runs {binary!r}, which is not in "
                        f"READ_ONLY_COMMANDS:\n    {command}\n\n"
                        "If it genuinely only reads, add it to the allowlist and "
                        "say why. If it does not, it belongs in the runbook "
                        "behind a gate."
                    )


def keep_matching(output: str, keywords: list[str]) -> str:
    """Lines mentioning any keyword, or a note that none did.

    Says so explicitly when nothing matched: an empty block is ambiguous between
    "the device has none of these" and "the command failed", and those need very
    different responses.
    """
    lowered = [k.lower() for k in keywords]
    kept = [line for line in output.splitlines()
            if any(k in line.lower() for k in lowered)]
    if not kept:
        return f"(nothing matching: {' '.join(keywords)})"
    return "\n".join(kept)


def adb(args: list[str], serial: str | None) -> subprocess.CompletedProcess:
    command = ["adb"]
    if serial:
        command += ["-s", serial]
    command += args
    return subprocess.run(command, capture_output=True, text=True, timeout=30)


def run_on_device(command: str, serial: str | None) -> str:
    try:
        result = adb(["shell", command], serial)
    except subprocess.TimeoutExpired:
        return "(timed out)"
    output = (result.stdout or "") + (result.stderr or "")
    return output.strip() or "(no output)"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", nargs="?",
                        help="IP or host:port to adb connect to first")
    parser.add_argument("--out", default="device-probe.md",
                        help="where to write the report (default: device-probe.md)")
    args = parser.parse_args()

    check_read_only()

    if shutil.which("adb") is None:
        print("adb not found on PATH. See docs/bring-up.md for how to get it.",
              file=sys.stderr)
        return 2

    if args.address:
        address = args.address if ":" in args.address else f"{args.address}:5555"
        print(f"connecting to {address} ...")
        connect = adb(["connect", address], None)
        print(connect.stdout.strip() or connect.stderr.strip())

    devices = adb(["devices"], None).stdout
    attached = [line.split()[0] for line in devices.splitlines()[1:]
                if line.strip() and line.split()[-1] == "device"]
    if not attached:
        print("No device. Check `adb devices`, and see docs/bring-up.md.",
              file=sys.stderr)
        return 1
    serial = attached[0]
    print(f"probing {serial} ({len(attached)} attached)\n")

    lines: list[str] = [
        "# TC002 probe report",
        "",
        f"- **Device:** `{serial}`",
        "- **Method:** read-only ADB shell commands; nothing was written, started "
        "or stopped.",
        "- **Produced by:** `tooling/probe/probe.py`",
        "",
        "> Record the stock-app and MCU version from the first section in any "
        "issue or note that refers to this device. A finding without them cannot "
        "be compared against anything.",
        "",
    ]

    for section in SECTIONS:
        print(f"  {section.title} ...")
        lines += [f"## {section.title}", "", f"_{section.why}_", ""]
        for label, command, *rest in section.commands:
            output = run_on_device(command, serial)
            if rest:
                output = keep_matching(output, rest[0].split())
            lines += [
                f"### {label}", "",
                f"```console", f"$ {command}", output, "```", "",
            ]

    report = Path(args.out)
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\nWrote {report} ({report.stat().st_size} bytes).")
    print("Nothing on the device was modified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
