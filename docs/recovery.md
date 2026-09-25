# Getting a TC002 back

Written for the moment you need it, so it starts with the answer.

> **No warranty, no liability.** These procedures are what worked on the
> development device. They are not guaranteed to work on yours, and following
> them is at your own risk (GPL-3.0 sections 15-16).

## The short version

**Unplug it and plug it back in.**

STIPPLE runs from `/tmp`, which is tmpfs. A power cycle wipes it and the stock
Ulanzi firmware comes straight back. This works for every problem STIPPLE can
currently cause, including the ones that look alarming: a frozen panel, a
device that has vanished from the network, two copies fighting over the
display, a hotspot that will not go away.

Nothing STIPPLE does today touches flash. That is deliberate and it is the
reason the rest of this document is short.

---

## By symptom

### The panel is frozen, or showing nonsense

Power cycle. If the stock clock comes back, STIPPLE was the problem and nothing
is damaged.

### The device has disappeared from the network

Usually the hotspot: one radio cannot be an access point and a station at the
same time, so while STIPPLE hosts `STIPPLE-setup` it is not on your Wi-Fi at
all. That is normal and it reverts on its own.

1. Look for a Wi-Fi network called **`STIPPLE-setup`**. If it is there, join it
   and open <http://192.168.4.1/>.
2. If it is not, wait two minutes — the hotspot reverts by itself and the
   device re-joins your network.
3. If it is still gone, power cycle.

### It is on the network but you cannot log in

Hold **− and + together for five seconds**. The panel counts down, and at zero
the access password is cleared and the device starts its hotspot.

It clears **only** the password. Apps, settings and arrangement are kept —
somebody locked out of a clock wants their configuration to still be there
when they get back in.

### You want to put it on a different Wi-Fi network

**Hold the knob in for five seconds.** The panel counts down under `SETUP`,
and at zero the device starts its hotspot: join `STIPPLE-setup` and open
<http://192.168.4.1/>.

This works whether or not the device is already online, which is the point -
moving house or changing routers is not a fault, and it should not require
being locked out first.

It **changes nothing**. No password is cleared, no stored network is
forgotten, nothing is written to flash, and the request does not survive a
reboot. If you let go early, or reboot without configuring anything, the
device carries on exactly as it was.

A *short* hold of the same knob still opens the settings menu as it always
did. The two do not collide, because the menu opens when you let go and this
fires while you are still holding.

Compare with the two-button gesture below, which is the one that clears your
access password. If you can reach the web UI and just want to change
networks, this is the gentler one to reach for.

### The clock shows the wrong time and will not change

Check the timezone under **System → Time**. The device has no timezone
database, so zones are stored as POSIX rules; picking your city from the list
sets the right one including daylight saving.

### Two copies of STIPPLE are running

Symptom: the panel flickers between two things, or looks doubled. This only
happens during development, when a new build is started without stopping the
old one.

```powershell
adb shell ps                       # find the /tmp/stipple_device entries
adb shell "kill -9 <pid>"          # stop each one
```

Or just power cycle.

---

## USB stick recovery — the one that works

**Proven on hardware.** A device stuck at the Ulanzi logo with no
application, no network and no USB gadget was brought back to stock with
this. It is the procedure Ulanzi support provides, and the part nobody can
guess from the binaries is the sentinel file.

### What you need

- A **plain USB flash drive** — not a card reader. Readers present as
  removable media with an empty-slot state and sometimes several LUNs, and
  the device's `vold` did not mount one.
- **FAT32 with 4 KB clusters.** Windows defaults a 32 GB volume to 16 KB,
  which is what a failed attempt used. Force it:
  `format D: /FS:FAT32 /A:4096 /Q`
- Two files in the root, and nothing else:

```
update.img       the firmware image  (dev.ps1 capture makes one for your unit)
zkautoupgrade    a single ASCII '0'  (one byte, no extension)
```

`zkautoupgrade` is the piece that matters. Without it the loader ignores
external media entirely — an attempt with `update.img`, `extupdate.img`,
`full_update.zk` and `zkimg/update.img` but no sentinel did nothing at all.
Its content is one byte, `0x30`.

### The procedure

1. Power the clock **from the base pins**, not USB — the USB-C port has to
   stay free.
2. Switch it on.
3. **Then** insert the stick into the left USB-C port.
4. Wait. It restarts and reflashes on its own.
5. **Remove the stick** when the Ulanzi logo appears.

Remove it because **whatever sits at `/mnt/storage/update.img` is what the
reset button installs**. Leave the wrong image there and the recovery button
is armed with it, silently, until somebody needs it.

(It is not, as this guide previously claimed, what causes a reflash loop.
That is driven by a pending-upgrade flag in `/data`; a device with an unused
`update.img` sitting on its storage runs quite happily.)

### What it does

A full `res` reflash **and** a `/data` wipe. Stock application, stock
configuration, and anything in `/data` — including STIPPLE and its settings —
is gone. That is a restore, not a repair.

### Honest note on what was tested

Two variables changed between the attempt that failed and the one that
worked: a card reader became a plain stick, and 16 KB clusters became 4 KB.
Either could have been the cause; both were fixed at once. If you only have a
card reader, reformatting at 4 KB is worth trying before buying anything.

## If the device boots but shows no application

The panel lights up, maybe shows the vendor logo, and nothing else happens.
Not reachable on the network, and nothing appears when plugged into a
computer.

**This is the state to take seriously**, because every software route out of
it depends on the application that is not running:

- No application means **no DHCP**. Nothing else on this device obtains an
  address, so there is no IP and no ADB over the network.
- No application means **no USB gadget**. The application is what sets
  `otg_role` to `usb_device`; until then the port is a host and nothing
  enumerates on a computer, whatever cable is used.
- Holding **reset** only helps if an image is sitting in `/mnt/storage` for
  the loader to install. If the last flash consumed it, reset does nothing -
  and reset also wipes `/data`, which is where STIPPLE lives.

### What still works

**A USB flash drive plugged into the clock.** The port is a host, so it can
read one. The boot-time update check runs inside `libeasyui` rather than in
the application, so it still happens. Use a **small, plain USB stick, 8 GB or
less, formatted FAT32** - not a card reader, and not a large volume with
16 KB clusters. Put `update.img` at the root, and for good measure
`extupdate.img`, `full_update.zk` and `zkimg/update.img`, which are the other
names the loader looks for.

**A serial console.** `ttyS0` at 115200 8N1, enabled in the kernel. Pads are
not documented for this board, so it means opening the case and finding
TX/RX/GND. With a shell the fix is three lines and moves no files:

```sh
mkdir -p /data/stipple
ln -s /res/lib/libzkgui.so /data/stipple/libstipple.so
reboot
```

That points the configured startup path at the vendor's own application,
which is still sitting untouched in `/res/lib/`. `dlopen` follows symlinks,
so the stock app loads, the network comes back, and the device is ordinary
again.

**The vendor.** The bootloader, kernel and rootfs are untouched in this
state, and their firmware update exists to recover it.

### How to not end up here

- **Never leave a staged image where the loader will find it twice.** It will
  reflash on every boot.
- **Do not hold reset to break a boot loop** if `/data` holds the only copy
  of the application. It wipes it.

## When Wi-Fi is not available at all

The USB-C port defaults to host mode, but it can be switched:

```powershell
adb shell "echo usb_device > /sys/bus/platform/devices/soc:usbotg/otg_role"
```

That brings up the ADB gadget over the cable, giving a root shell with no
network involved. Useful precisely when the network is the thing that broke —
though note it needs a shell to run, so it is something to set up *before* you
need it rather than after.

---

## The factory reset button, and why it may not do what you want

Holding the reset button during power-up makes the device wipe `/data` and
reflash the `res` partition from `/mnt/storage/update.img` — the file on its
own USB volume.

**That image is not necessarily the firmware your device is running.**

On the unit STIPPLE was developed against, the shipped image is *older* than
the installed partition. Holding reset there is a downgrade, not a restore.
The third-party TC002 project reports the same on their unit. Devices differ.

So before relying on that button:

```powershell
.\dev.ps1 capture 192.168.1.238:5555
```

This reads your device's live `res` partition through the kernel's read-only
alias, wraps it in an image the loader accepts, and verifies the result
against what it read. It writes nothing to the device.

Keep `restore/restore-res.img`. It is specific to your unit, it is Ulanzi's
firmware, and it must not be shared — which is why it is gitignored.

Putting that file on the device's USB volume as `update.img` makes the reset
button a *correct* recovery. That is worth doing whether or not you ever
intend to flash anything.

---

## What STIPPLE will not do to you

- **It does not write flash.** No release does, and none will until
  [ADR 0008](adr/0008-installer-helper.md)'s gates are met: a verified restore
  image *and* a restore path somebody has actually demonstrated.
- **It does not change your Wi-Fi settings without being asked.** Joining a
  network appends a block and never replaces one, so a wrong password falls
  back to the network that was already working.
- **It does not keep an open access point running forever.** The hotspot
  reverts on a timer, because a clock quietly hosting an open network is a
  worse problem than the one it was solving.
- **It does not phone anywhere.** No cloud, no telemetry, no account.

## If none of this helps

Open an issue with:

- what the panel was showing,
- whether the device answers `ping`,
- `adb shell getprop ro.product.model` if you can reach a shell,
- and the output of `/api/v1/logs` if you can reach the web UI.

Do **not** attach a restore image or a partition capture. They are vendor
firmware and they carry details specific to your device.
