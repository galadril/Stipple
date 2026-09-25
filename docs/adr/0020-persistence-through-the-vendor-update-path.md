# 0020 — Persist through the vendor's update path, not a flasher of our own

- **Status:** Accepted; the image tooling is built and verified, nothing has been flashed
- **Date:** 2026-09-22

## Context

STIPPLE runs from `/tmp`. A power cycle restores the stock application, which
has been exactly the right property while everything was being built — today
alone it recovered the device three times. It is also why STIPPLE is a program
you run rather than a firmware the device runs.

Making it persist means writing flash, because nothing else will do:

- `/data` is the only writable persistent filesystem, 8 MiB of jffs2.
- `/`, `/res` and `/config` are read-only squashfs.
- **Nothing in `init.rc` runs anything from `/data`.** Every service it
  declares points at `/bin` or `/res`, and `init.rc` itself lives on the
  read-only rootfs. There is no "drop a file in `/data` and it starts at
  boot" escape hatch.

The obvious answer was to write an MTD flasher. `/dev/mtd/mtd0`–`mtd7` are
there as character devices, root-owned, and the part is **NOR** — `writesize`
1, `oobsize` 0, no bad blocks — so it is erase-then-write and about a hundred
and fifty lines.

That answer is wrong, and the reason is worth stating: the failure mode of a
flasher we wrote is a clock nobody can recover without opening it.

## Decision

### Use the update mechanism the device already has

Verified first-hand on hardware, not taken from a description:

- `/mnt/storage/update.img` exists on the device's own USB volume — the vfat
  partition (mtd7) that appears as mass storage when plugged into a computer.
- The loader validates a **header CRC32** and a **payload MD5** before
  writing anything.
- Only the **`res` partition (index 3)** is written directly. Other
  partitions go through a u-boot handoff, which is not a path this project
  will touch.
- Recovery is a **physical button**: hold reset during power-up and it
  reflashes from `/mnt/storage`. No network, no computer, no shell.

Every one of those is better than what we would have built. Two checksums
validated by the loader beat one verified by us; a physical recovery button
beats a procedure in a README; and writing only `res` means the worst case is
a device that still boots, still runs `init`, and still starts `adbd`.

**`res` is the partition that can be broken safely.** Break the rootfs or the
kernel and the device does not boot far enough to be talked to. Break `res`
and the kernel, init, ADB and the network all still come up — which is
exactly the difference between a recoverable mistake and a brick.

### The container format, confirmed byte for byte

Read off a factory image pulled from a real device, then proven by rebuilding
that image and getting the same bytes back:

```
0x000  magic  "ZKSWEV1.0-180127"    (first 9 bytes are what is checked)
0x010  prefix length 0x30, entry count 1
0x014  partition index               3 = res
0x018  payload offset                0x23c (572) — the header size
0x01c  payload length
0x020  the real first 16 bytes of the filesystem image, relocated
0x035  device code 0xaa550606        Zkswe_SSD21X_SPINOR, unaligned
0x238  CRC32 over header[0:0x238]
payload[0:16]                        MD5 of the image with its first 16 bytes restored
```

The two sixteen-byte swaps are the only clever part: the filesystem's opening
bytes move into the header, and the MD5 takes their place in the payload.

**The header is copied from a known-good image and edited, never written from
scratch.** Large parts of it are not understood — there is a table from 0x60
onwards that this project has no business inventing. Changing only the five
fields that must change leaves everything else exactly as the loader has
always seen it.

### A restore image comes from the device it will restore

ADR 0008 already required this. What is new is evidence for *why*, measured
on the unit this was written against:

> The `update.img` on its own USB volume is **not** what it is running. The
> shipped payload is 2 781 184 bytes; the installed filesystem is 2 787 758.
> **Holding the reset button on this device installs an older image than the
> one it has.**

The third-party TC002 documentation reports the same on their unit. Devices
differ; the only image known to match a device is the one read out of it.

So `tooling/imgtool/capture.py` reads the live `res` through the kernel's
**read-only** alias `/dev/mtd/mtd3ro`, trims it to the filesystem it
contains, wraps it in a container, and verifies the result against the
capture before saying it worked. It writes nothing to the device.

## Consequences

**ADR 0008's first gate is met, and its second is not.** There is now a
verified restore image captured from a real device, and a tool to make one
for any device. There is still no *demonstrated* restore path: nobody has
held the reset button on a device that needed it. Until somebody has, nothing
gets flashed. That has not changed and is not negotiable.

**The unproven part is no longer the flashing.** It is whether STIPPLE can be
built into a `res` image the vendor host will load. Blueprint §7.1 says the
application lives at `/res/lib/libzkgui.so` and `/bin/zkgui` loads it, which
means STIPPLE has to become a shared library with whatever entry points that
host calls. That is real work and nobody has tried it.

**A safety net exists that did not before.** Keeping a captured, verified
image on `/mnt/storage` means the reset button becomes a *correct* recovery
rather than a downgrade — worth doing before any flashing regardless of how
it goes.

**The USB cable is a recovery path after all.** `otg_role` reads `usb_host`
and can be set to `usb_device`, giving root ADB over USB with no network at
all. CLAUDE.md says USB-C on this device is mass storage only; that is true
of its default role and not of the port. Recorded in the findings.

## Alternatives considered

**Write an MTD flasher.** Smaller than expected on NOR, and still wrong: it
replaces a validated path the device has used since the factory with one we
would be testing for the first time on somebody's clock. If a partition ever
needs writing that the update path will not write, this is the fallback — and
it should arrive with its own ADR and its own reasons.

**Replace `/bin/zkgui` or an init service on the rootfs.** Simpler than
building a `res` image, and it puts the blast radius on the partition whose
failure means the device does not boot. Rejected for that alone.

**Stay volatile forever.** Defensible for a development tool and not for a
product: a clock that forgets what it is every time the power blinks is not a
replacement firmware, it is a demo.

## Update: the stock firmware will do the flashing

Probed on hardware after this ADR was written, and it simplifies the
remaining work considerably.

The stock application serves an OTA endpoint on port 80 that takes a URL and
downloads the image itself:

```json
POST /update
{"app": {"version": "0.1.0", "downloadUrl": "http://host/stipple.img"}}
```

Unauthenticated. So the eventual install becomes: boot stock, serve the image
from any machine on the LAN, hand the device its URL. No writer of ours, no
MTD ioctls, and the same validation path Ulanzi's own updates take.

This does not move ADR 0008's gates and does not change this ADR's decision -
it confirms it. The reasoning was that the device's own update path is better
than anything we would write. It turns out to be better still than the
`/mnt/storage` route this ADR described, because it needs no physical access
at all.

**What this project must not copy is its security posture.** An
unauthenticated remote firmware write available to anything on the LAN is
exactly what STIPPLE should not offer. `/api/v1/system/restore-image` stages a
file to the USB volume, behind the access password, and cannot flash. That
asymmetry is deliberate and should survive whatever comes next.
