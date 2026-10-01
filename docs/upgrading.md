# Upgrading Stipple

**Most updates are an upload. Some are not, and this page exists so you find
out before rather than after.**

> **No warranty, no liability.** Upgrading modifies firmware on a device the
> manufacturer did not intend to be modified (GPL-3.0 sections 15–16).
> **Do not run Stipple on a device you are not prepared to lose.**

## The short version

| You have | You want | How |
|---|---|---|
| Any Stipple | A newer Stipple | **Settings → Firmware**, upload `libstipple-X.Y.Z.so` |
| Stipple, loader older than the release needs | That release's safety features | **Reflash** from a USB stick — an upload cannot change the loader |
| Stipple | Stock Ulanzi | `docs/recovery.md` |
| Stock | Stipple | `docs/install.md` |

If **Settings → Firmware** says nothing unusual, upload the file and you are
done. The device checks the file before writing anything, keeps the previous
version, and **Go back** rolls it back.

## Why some updates need a reflash

Stipple is two pieces, and they live in different places:

```
/res/lib/libstippleboot.so    the loader (the "shim")   read-only  reflash only
/res/lib/libstipple.so        the flashed application   read-only  reflash only
/data/stipple/libstipple.so.override   your updates     writable   upload
```

**An upload writes the override and nothing else.** That is the whole of OTA,
and it is deliberate — it is also why a failed update cannot brick anything:
delete one file and the flashed copy runs again.

The consequence is that **the loader can only be changed by reflashing.** A
device flashed a year ago and updated over the air ever since is running the
newest application on the oldest loader, and the version number on the
settings page says nothing about it.

That matters when a release's safety properties live in the loader. The
boot-failure ladder is the first one that does:

```
3 unfinished boots  ->  safe mode: default settings, no stored apps   application
5 unfinished boots  ->  roll back the uploaded update                 loader
8 unfinished boots  ->  hand over to the stock clock                  loader
```

Upload that release onto an old loader and you get the first rung only. The
application works; the net under it is not there.

## How to tell, without guessing

```bash
curl -s http://your-clock/api/v1/system/firmware
```

`loaderFeatures` is the answer:

| Value | Means |
|---|---|
| `-1` | A loader from before it reported anything. No boot ladder. |
| `1` | Attempt counting and the 5/8 ladder. |

**`-1` is not an error and not a fault.** It is the honest state of every
device flashed before this existed, and such a device keeps working exactly
as it did. It simply has no automatic recovery from a Stipple that starts and
then fails — so if that happens, recovery is manual, through
`docs/recovery.md`.

Stipple will not pretend otherwise, and the firmware page says so rather than
letting an upload look sufficient.

## Reflashing to get a newer loader

Same procedure as a first install — `docs/install.md` — with one thing worth
knowing before you start:

**A USB reflash keeps `/data`.** Scripts, settings, stored Wi-Fi and an
uploaded `libstipple.so` all survive it — this page said otherwise before it
was measured on hardware. It is the **reset button** that wipes `/data`, so
only reset first if you actually want a clean device.

Worth exporting anyway before any firmware operation, because cheap:

```bash
curl -s http://your-clock/api/v1/scripts  > scripts-backup.json
curl -s http://your-clock/api/v1/settings > settings-backup.json
```

And the thing that catches people:

**Whatever sits at `/mnt/storage/update.img` is what the recovery button
installs** — and a USB install does not appear to change it, provided you
remove the stick when the Ulanzi logo appears. Measured: after installing
Stipple that way, holding reset still produced **stock**.

That makes "remove the stick at the logo" more than housekeeping. It decides
what your recovery button does for the life of the device:

- **Remove it at the logo** — the staged image is untouched. On a device that
  has only ever had the factory image staged, reset keeps meaning "back to
  stock".
- **Leave it in** — the image can end up staged, and from then on reset
  reinstalls *that* instead. A device here had a Stipple image staged with a
  timestamp matching an earlier install where the stick stayed in.

Check rather than assume — the size tells you which it is:

```bash
curl -s http://your-clock/api/v1/system/recovery
```

It answers in words: `"means": "returns this device to the stock Ulanzi
clock"`. `docs/recovery.md` covers changing it.

## Downgrading

Uploading an older `libstipple.so` works, and **Go back** does it for you if
the version you want is the one you just replaced.

One wrinkle, if you are on loader features `1` or later: an application old
enough not to clear the attempt counter never tells the loader the boot
finished. After five such boots the loader rolls the override back to the
flashed copy, which does clear it — so a deliberate downgrade to a
pre-ladder release will flip back every few boots rather than stay put. It is
not harmful and it never reaches the stock-clock rung, because each rollback
resets the count. If you want to sit on an old version, reflash an old image
rather than uploading one.

## If an update will not start

Nothing to do. The loader tries the override, then the copy flashed beside it,
then the stock clock, so the worst case is a reboot into the version you
started with. On loader features `1` or later that happens by itself after
five unfinished boots.

If you are on `-1` and stuck, `docs/recovery.md` — the short version is almost
always "unplug it and plug it back in".
