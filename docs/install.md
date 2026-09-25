# Installing Stipple on a TC002

Written after doing it, badly, several times in one evening. Every warning
here is something that actually went wrong.

> ### ⚠️ Read this before you start
>
> **You do this entirely at your own risk.** Flashing modifies firmware on a
> device the manufacturer did not intend to be modified. It can leave your
> clock unusable, it will probably void your warranty, and recovering it may
> need a USB stick, hardware access, or help from the vendor.
>
> This exact procedure bricked the development device badly enough to need a
> recovery process obtained from Ulanzi support. It is written down here
> *because* that happened.
>
> Stipple comes with **no warranty and no liability of any kind** (GPL-3.0
> sections 15-16). **Do not install it on a device you are not prepared to
> lose.**

## The shape of it

**You flash once, and update by file copy for ever after.**

The flash puts two things in the device's read-only `res` partition: a small
shim, and Stipple itself. It changes exactly one line of vendor
configuration — `startupLibPath` — so the framework loads the shim instead of
the stock application. The shim then picks what to run:

```
/data/stipple/libstipple.so.override   an update you installed   ->  run it
/res/lib/libstipple.so                flashed with the shim     ->  run it
neither                              the stock Ulanzi clock    ->  run that
```

That third line is the important one. **A Stipple that will not load leaves a
working clock on your network**, not a device nobody can reach. Recovery is
deleting one file.

After the flash, updates are an upload through **Settings → Firmware**. No
USB stick, no reset button, no flash writes.

## Why there is no image to download

`update.img` contains Ulanzi's `res` partition — their application, their
fonts, their assets — with one line changed and Stipple added beside it.
Publishing it would be redistributing their firmware, so the build starts
from **a capture of your own device**.

The part that is ours, `libstipple.so`, *is* published with every release.

## What you need

- The device, on your network, with ADB reachable
- Podman or Docker
- A **plain USB flash drive** — not a card reader. Readers present an
  empty-slot state and sometimes several LUNs, and the device's `vold` would
  not mount one.
- **FAT32 with 4 KB clusters.** Windows defaults a 32 GB volume to 16 KB,
  which is what a failed attempt used:
  `format D: /FS:FAT32 /A:4096 /Q`

## 1. Capture your device's partition

```powershell
.\dev.ps1 capture 192.168.1.238:5555
```

Reads the live `res` partition through the kernel's read-only alias and
verifies what it read. **It writes nothing to the device.** The result lands
in `restore/`, which is gitignored — it is vendor firmware and carries your
device's identifiers, so it must not be shared.

Keep it. It is also your route back to stock.

## 2. Build the image

```powershell
podman run --rm -v "${PWD}:/src" stipple-cross:bullseye bash -c `
  "cd /src && tooling/imgtool/buildres.sh restore/res-raw.bin restore/stipple-res.squashfs"

python.exe tooling/imgtool/imgtool.py pack `
  restore/stipple-res.squashfs restore/stipple-update.img `
  --template restore/shipped-update.img

python.exe tooling/imgtool/imgtool.py info restore/stipple-update.img
```

The last command re-reads what was written and checks both checksums the
device will check. It should end with `header CRC32 ok` and `payload MD5 ok`.

**Use the bullseye container, not bookworm.** Bookworm's output needs
`GLIBC_2.34` and the device has 2.30. The build is otherwise identical and
fails only at `dlopen`, on the device, where nothing can tell you why.

## 3. Put it on the stick

```powershell
.\dev.ps1 usb
```

It lists your removable drives, asks which one, erases it, and writes both
files. It checks the image before touching the drive and verifies the copy
afterwards.

**Only removable drives are ever offered**, there is a size cap so an external
backup disk cannot be chosen by accident, and it asks you to type the drive
letter rather than press "y" — the letter is the thing people get wrong, and a
yes/no prompt is answered by reflex.

<details>
<summary>Doing it by hand instead</summary>

Two files in the root, nothing else:

```
update.img       the image you built in step 2
zkautoupgrade    a single ASCII '0' - one byte, no extension
```

Format FAT32 with **4 KB clusters** — Windows defaults a 32 GB volume to
16 KB, which is what a failed attempt used:

```
format D: /FS:FAT32 /A:4096 /Q
```

`zkautoupgrade` is the piece nobody can guess from the binaries. Without it
the loader ignores external media entirely — an attempt with `update.img`,
`extupdate.img`, `full_update.zk` *and* `zkimg/update.img` but no sentinel did
nothing at all.

</details>

## 4. Flash

1. Power the clock **from the base pins**, not USB — the USB-C port has to
   stay free.
2. Switch it on.
3. **Then** insert the stick.
4. Wait. It restarts and reflashes on its own.
5. **Remove the stick when the Ulanzi logo appears.**

Remove it because whatever sits at `/mnt/storage/update.img` is what the
device installs on its *next* recovery — and one kind of recovery happens
unattended. See the warning below.

## What the first boot looks like

```
Stipple over a travelling wave          5 seconds
version over the IP address            5 seconds
the clock
```

The panel will not show a time until the device has one — this hardware has
no RTC battery, so it boots at 1970 and asks the network. That takes a few
seconds after the address arrives. `__:__` means "I do not know yet", not a
fault.

**If it cannot reach your Wi-Fi**, it hosts an open network called
`Stipple-setup` — after 60 seconds if it has never connected, or 5 minutes if
it lost a network that was working. Join it and open <http://192.168.4.1/>.

You can also ask for that at any time: **hold the knob for five seconds.** The
panel counts down under `SETUP`. It clears nothing and changes nothing.

## Updating, from then on

Download `libstipple-X.Y.Z.so` from a release and upload it in
**Settings → Firmware**. It is checked before anything is written — it has to
be a 32-bit ARM shared library, so a host build is refused rather than
installed — then written beside the running copy and renamed into place.

The previous version is kept. **Go back** rolls it back. If an update will not
load at all, the shim falls through to the copy flashed in step 4, so the
worst case is a reboot into the version you started with.

## Two things that will bite you

**Never leave an image at `/mnt/storage/update.img`.** `/bin/zkdaemon` watches
the property `sys.zkapp.state` and, if the application has not declared itself
`running` in time, performs *auto recovery*: it installs whatever is staged,
with nobody pressing anything. A stock image left there as a "safety net"
reverted a working Stipple within a minute of boot, twice, and looked like a
mysterious flash failure both times.

**Do not hold reset to break a boot loop.** It wipes `/data`. If your only
copy of Stipple is the override there, it goes with it — though since the flash
in step 4 puts a copy in `/res`, this is now survivable rather than fatal.

## Getting back to stock

`docs/recovery.md`, and the short version is: the same USB procedure with your
captured `restore-res.img` instead. That is why step 1 says to keep it.
