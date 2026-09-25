# 0008 — Installer helper: three tiers, and a restore image you made yourself

- **Status:** Accepted (design). Implementation pending hardware — Phase 7.
- **Date:** 2026-09-17

## Context

Blueprint §27 sketches an installer but leaves the ordering and the safety model
open, and §44 forbids writing persistent flashing logic without an explicit task.
There is now one: a TC002 is being acquired and needs a deployment path that can
be trusted on the first attempt.

Three facts constrain the design, all from documentation of a third-party TC002
port and recorded in `docs/research/tc002-platform-findings.md`:

- **Installing writes the `res` partition in place. There is no A/B copy.** A
  power cut during the write leaves a partition that needs recovery. Rollback
  therefore cannot be a partition swap — it has to be a restore image.
- **Recovery is a hardware gesture with a floor.** Holding the knob while
  powering on starts the vendor application, which restores the stock UI,
  updater and ADB. Separately, three consecutive start-up crashes make the
  platform fall back to the vendor launcher on the fourth boot. Below that,
  nothing is validated: if neither the vendor app nor ADB returns, recovery
  needs the stock bootloader's update path or a serial connection.
- **A restore image is vendor filesystem.** It is Ulanzi's, not ours. We can
  neither publish it nor assume the user has one.

We also have something that port did not: a WebAssembly emulator running the
real renderer, scheduler and `/api/v1` router. That changes what the lowest tier
of "try it" should even be.

And we do not yet have the thing an installer exists to deliver. There is no
`arm-linux-gnueabihf` preset, no TC002 platform adapter and no device binary. So
this ADR decides the *shape* and the *gates*; the code lands in Phase 7 behind
them.

## Decision

### Three tiers, and you may not skip one

| Tier | Persistence | Needs a device | Gate to reach it |
|---|---|---|---|
| 1. Emulator | none | no | none — it is a URL or a zip |
| 2. Trial | until power cycle | yes | device reachable, version recognised |
| 3. Flash | permanent | yes | tier 2 succeeded **and** a restore image exists |

Tier 1 is not a mock. It is the same core with a different platform adapter
(ADR 0011), so "does my scene render correctly" is answered without hardware at
all. Every question it can answer must be answered there, because it is the only
tier with no failure mode.

Tier 2 is the default development loop: push to `/tmp`, stop the launcher, run,
restart the stock service afterwards. Volatile by construction — a power cycle
restores the stock application. Blueprint §44's "no persistent flashing without
an explicit task" means this tier, not tier 3, is what `dev.ps1 deploy` does.

Tier 3 is for a device that has already run STIPPLE successfully in tier 2 on
that same hardware and stock-app version. Flashing something that has never
executed on the device in front of you is the scenario the 8 MiB partition write
punishes.

### The flash refuses to run without a restore image it captured itself

This is the decision that matters most, and it departs from the port we studied.
They offer `restore-stock.img` as a downloadable release artifact. We will not:

- **Capture, don't download.** The installer reads the device's own live `res`
  filesystem and writes a restore image to the operator's machine *before* it
  will write anything. No restore image, no flash — not a warning, a refusal.
- **Verify it before trusting it.** The captured image is validated with the
  same checks as an outgoing one, and its digest is recorded alongside the
  device's stock-app and MCU versions. A restore image is only known to fit the
  device it came from.
- **Never publish one.** No vendor-derived blob becomes a release asset, ever.
  This is §46 Q5's redistribution half answered in the negative, and it is a
  property of our release process, not of the installer.

A downloaded restore image is a stranger's filesystem, possibly from a different
hardware revision, and it is the artifact you reach for at the worst possible
moment. One taken from your own device ten minutes earlier is not.

### Preflight runs on the device, and it is not advisory

The checks, adopted as a list from what that port documents because the list is
the insight:

1. Platform header, CRC and payload MD5 of the image.
2. Filesystem bounds, and the **8 MiB `res` partition ceiling**.
3. Partition geometry read from the device, not assumed.
4. Vendor files against recorded fingerprints.
5. Stock-app and MCU version against a known-good allowlist.
6. A restore image exists, validates, and matches this device.

Checks 1–4 and 6 are structural: there is no override. Check 5 is a
compatibility allowlist and may be overridden, because a new stock-app release
is a normal event and refusing forever would strand users — but the override is
typed, not a flag, so it cannot hide in a shell history or a copied one-liner.

The write itself requires the operator to type `flash`. The `curl | sh`
one-liner shape is not adopted for tier 3; piping a remote script into a shell
that then writes flash removes the one pause where a human notices the device
model is wrong. Tiers 1 and 2 may be as frictionless as we can make them.

### One implementation, two front ends

Blueprint §27.3's CLI is the reference implementation:

```
stipple doctor  --host 192.168.1.42    # reachability, versions, capabilities
stipple deploy  --host 192.168.1.42    # tier 2, volatile
stipple capture --host 192.168.1.42    # write a restore image here
stipple flash   --host 192.168.1.42    # tier 3, gated on the above
stipple restore --host 192.168.1.42    # back to stock from a captured image
stipple logs    --host 192.168.1.42
```

`dev.ps1`'s device verbs and any future GUI helper wrap this, they do not
reimplement it. `capture` is additive to §27.3's verb list and follows from the
refusal above; `flash` is named for what it does rather than `install`, because
the two words carry very different amounts of warning.

### Crash-loop state is reported, not inferred

The platform abandons an application that crashes three times at start-up. A
STIPPLE that has been fallen back from is indistinguishable, to the user, from
one that never installed. `stipple doctor` must therefore be able to say which of
the two it is looking at, and the firmware must record start-up progress
somewhere that survives a crash. Otherwise the first real field report is "it
didn't work" with nothing attached.

### Tier 3 stays off until §27.4's gates pass

Blueprint §27.4's list — reproducible image, two devices, factory reset,
interrupted update, invalid image rejection, upgrade, downgrade, checksum, boot
health flag, rollback — is unchanged and unrelaxed. Until all of it passes,
`stipple flash` is not built into released binaries, and no public
"flash permanently" button exists (§27.4, Stage 10's warning). Tier 2 ships
first and carries real usage.

## Consequences

**Good.**

- The dangerous tier is unreachable without having already run the exact build
  on the exact device, and without a rollback artifact in hand.
- Rollback does not depend on an A/B partition the hardware does not have.
- No licensing or redistribution exposure from vendor blobs, because none are
  distributed.
- The emulator absorbs most of the iteration that would otherwise have to
  happen against hardware, which is what makes a slow, gated tier 3 acceptable
  rather than painful.
- The captured-restore rule turns §46 Q5's open redistribution question from a
  blocker into a design constraint we have already satisfied.

**Bad, or at least owed.**

- Tier 3 is meaningfully more work for the user than a `curl | sh` one-liner,
  and some will read the refusal as the tool being obstructive. Worth it once;
  the failure it prevents is a bricked clock.
- `capture` requires read access to the live `res` filesystem. If that turns out
  not to be readable over ADB, the precondition is unsatisfiable and this ADR
  needs revisiting rather than relaxing — a flash with no rollback is not a
  tier we offer.
- Every check above is specified against a format we have not seen. The
  `update.img` layout is still §46 Q5, and nothing here should be mistaken for
  knowing it.
- Recovery's documented floor is not a guarantee. We inherit "untested below
  the vendor launcher" and cannot improve on it without hardware.

## What this does not decide

- **The image format.** §46 Q5 stays open. Implementing a validator for a layout
  we have inferred would be the guessing §46 exists to prevent.
- **Signing and update security.** ADR 0009, still pending. This ADR assumes
  digests, not signatures.
- **OTA.** Blueprint Stage 13. A device-initiated update is a different risk
  model and does not inherit this one's gates.
- **First-time Wi-Fi provisioning.** Still unsettled, and still the most useful
  thing to test when hardware arrives: every tier here assumes a device already
  on the network, so a flashed device moved to a new network has no documented
  way back.

## Related

- `docs/research/tc002-platform-findings.md` — the evidence, its provenance, and
  why that project's code is not reused
- [0011](0011-simulator-first-development-order.md) — why the emulator is tier 1
  rather than a testing aid
- [0013](0013-platform-capability-model.md) — capabilities report presence
  rather than being assumed, which is how `doctor` answers honestly
- Blueprint §27 (installer), §27.4 (the gate list), §46 Q5 (image packaging)


---

## Amendment, 2026-09-19: a restore image is not a restore capability

Probing a real device turned up something this ADR assumed away: **the TC002
cannot write its own flash.**

`/bin/busybox` is a 66 KB build with neither `dd` nor `awk`, and there is no
`flash_erase`, `flashcp`, `nandwrite` or `mtd_debug` anywhere on the
filesystem. Capturing an image worked only because `adb pull` reads a character
device; no tool on the device can write one back.

The original tier-3 gate — "tier 2 succeeded **and** a restore image exists" —
is therefore necessary but not sufficient. An image nobody can write back is a
souvenir.

**The gate is amended to require all three:**

1. tier 2 has succeeded on that exact device and stock-app version, **and**
2. a verified restore image exists, **and**
3. the restore *path* has been demonstrated end to end on that device — written,
   run, and the result verified against the captured hashes.

Requirement 3 is new and it is the one with teeth, because it cannot be
satisfied by paperwork.

### Proving a writer without risking the device

A restore tool is small — open `/dev/mtd/mtdN`, `MEMERASE`, `write()`, verify —
and we control the cross-toolchain, so building one is not the hard part.
Trusting it is.

The honest way to earn that trust is to exercise it on a partition whose loss
would not matter, and `UDISK` (mtd7, mounted `/mnt/storage`) is exactly that: it
is user storage rather than anything the device boots from, it is already
captured and hashed, and a failed write there costs a re-flash of user files
rather than a brick.

Only once a write-and-verify round trip has succeeded on UDISK does the tool
become something to point at `res`.

### What this does not change

Nothing below tier 3. `/tmp` is tmpfs — RAM, not flash — so the trial path
remains free of any risk this amendment is about, and it is where all near-term
work belongs.


### How the third-party port does it (from their documentation)

Read from their README, not their source — ADR 0001's line holds, and their
tooling is still their code.

- They **built their own helper** (`tc002-update`), pushed it to `/tmp` and ran
  it on the device. There was no existing tool to reuse, which matches what the
  probe found here.
- It **writes only the `res` partition**: "Neither tool writes other flash
  partitions." The blast radius of the whole exercise is one partition.
- The ordering is careful and worth copying as a *shape*: stop the GUI and its
  Bluetooth helper, unmount `/res`, and **if the unmount fails, abort before
  erasing** and restart the GUI. Destroy nothing until every precondition has
  actually held.
- Their restore is applying a **stock `update.img`** with the same helper.

### Where that leaves us, concretely

The last point is the real difference. Their restore unit is a packaged
`update.img` — a vendor artifact they keep a copy of. Ours is **raw partition
dumps of all eight partitions**, which is both more complete and lower-level: we
do not need their packaging format to put `res` back, because we have the bytes
that were in it.

So the missing piece is only the writer, and its requirements are now clear:

1. **Only ever write `res`.** Never BOOT0, KERNEL or rootfs. Nothing STIPPLE does
   needs them, and refusing to write them is a property of the tool rather than
   a promise in a runbook.
2. **Abort before erasing** if anything is not as expected — unmount failure,
   size mismatch, hash mismatch on the source image.
3. **Verify by reading back** and comparing against the captured hash. A write
   that is not verified has not happened.
4. **Prove it on UDISK first**, per the gate above.

### One caveat the recovery story does not cover

Both documented recovery routes — knob held at power-on, and the fourth-boot
fallback after three crashes — are described as *starting the vendor
application*. The vendor application is `/res/lib/libzkgui.so`. If `res` itself
is the thing that is broken, there is nothing for either route to fall back to.

That is an argument for requirement 3 above rather than a reason to despair, but
it should be said plainly: the recovery gestures choose which launcher runs.
They do not repair files.

## Update, 2026-09-22: one gate met, one still standing

**A verified restore image now exists**, captured from a real unit and
checked against the partition it came from, and `tooling/imgtool/capture.py`
makes one for any device. The first precondition is satisfied.

**The second is not.** This ADR asks for a restore path that has been
*demonstrated*, and nobody has yet held the reset button on a device that
needed it. Until somebody has, nothing gets flashed.

What changed is how the flashing would happen. It will not be a tool of ours
writing MTD directly — the device has its own update path, it validates a
header CRC32 and a payload MD5, it writes only the partition that can be
broken safely, and its recovery is a physical button. See
[ADR 0020](0020-persistence-through-the-vendor-update-path.md).

One finding sharpens this ADR's own argument. The `update.img` that ships on
a unit's USB volume is **not necessarily the firmware that unit is running** -
measured on the device here, and reported by others on theirs. So the reset
button is not automatically a restore; it can be a downgrade. Capturing from
the device is not belt-and-braces, it is the only thing that makes the
physical recovery correct.

## Update, 2026-09-22 evening: the gate was right and I argued past it

A device was flashed and is now unreachable. The flash itself worked - the
`res` partition took the image, and STIPPLE started as the device's
application, which is the thing the whole day was aiming at. What followed is
the part worth recording.

**What actually happened, in order:**

1. `update.img` was staged on `/mnt/storage` so the reset button would be a
   correct recovery. Good idea.
2. The image was flashed. It applied, and STIPPLE ran.
3. **Nothing cleared the staged image**, so the loader found it again on the
   next boot and reflashed. And again. A boot loop.
4. The reset button was held to break the loop. Reset wipes `/data` - which
   is where `libstipple.so` lived, because
   [ADR 0021](0021-stipple-as-the-startup-library.md) deliberately put it
   there.
5. With no library, no application loads. And **nothing on this device
   obtains an IP address except the application** - the finding recorded in
   the research notes that same morning.

No application means no DHCP, no network, no ADB. It also means no USB
gadget, because the application is what sets `otg_role` to `usb_device`;
three cable types were tried and the device never enumerates. Every software
route depends on the thing that is missing.

**Each of those five steps was known in advance.** The staging, the reset
behaviour, and the DHCP gap were all written down before any of this was
done. They were not composed.

### What this changes

**The "demonstrated restore" gate stands, and the argument for skipping it
was wrong.** The reasoning at the time was that demonstrating a restore means
deliberately flashing something broken, which is riskier than flashing
something good - so a null flash would do instead. That reasoning is not
unsound, and it is also not what the gate is for. The gate is not about the
image being bad. It is about *having exercised the way back before needing
it*, and the way back here turned out to depend on a file that the recovery
procedure itself deletes.

**A restore path has to be demonstrated from the state that will actually
need it** - a device with no application - not from a healthy one.

### Rules that follow, and they are not optional

**Staging is one-shot.** An image placed where a loader will find it must be
removed as soon as it has been applied, by the same tooling that put it
there. A recovery image and a pending update are not the same thing and must
not live at the same path.

**Never wipe `/data` while it is the only copy of anything.** ADR 0021 puts
STIPPLE in `/data` precisely so it can be updated without flashing. That makes
`/data` load-bearing, and it makes factory reset destructive in a way it was
not before. Either keep a copy in `res`, or accept that reset is not a
recovery.

**A device that cannot obtain an address cannot be recovered over the
network.** Until STIPPLE's DHCP client runs from somewhere that survives a
missing application - or the config falls back to the vendor library when the
STIPPLE one is absent - flashing this device is not safe.

That last one has a cheap fix worth building before anything is flashed
again: **if `startupLibPath` points at a file that does not exist, the device
should fall back to `/res/lib/libzkgui.so`.** The framework already logs the
`dlerror` and carries on; it simply carries on with nothing. A `res` image
that pointed at a small shim which loads STIPPLE if present and the vendor
application otherwise would have made all of this a non-event.

## Update: the second gate is met

A restore has now been **demonstrated**, not designed. The device that this
ADR's post-mortem describes was brought back to stock from the exact state
that needed it — no application, no network, no USB gadget — using a USB
stick and the vendor's own loader. Confirmed afterwards over ADB:
`startupLibPath` back to `/res/lib/libzkgui.so`, `/bin/zkgui` running,
`/data` wiped.

The procedure is in [`docs/recovery.md`](../recovery.md). The part worth
repeating here is the one nothing in the binaries revealed: the loader
ignores external media unless a **`zkautoupgrade`** file sits beside the
image. One byte, ASCII `0`. An attempt with four plausible image filenames
and no sentinel did nothing whatsoever.

**So both gates are now satisfied** — a verified restore image captured from
the device, and a restore path somebody has actually walked. Flashing is no
longer blocked by this ADR.

It should still not happen without the fallback shim in
[ADR 0021](0021-stipple-as-the-startup-library.md). The gates were about
being able to recover; the shim is about not needing to. Both matter, and
this ADR is the wrong place to relax the second one having just spent a day
proving the first.

## Correction: the loop was a flag, not a file

The post-mortem above says the loader "found it again on the next boot and
reflashed", i.e. that leaving `update.img` on `/mnt/storage` is itself the
loop. That is **wrong**, and the recovered device disproved it.

After recovery, `/mnt/storage/update.img` was still the STIPPLE image - the
same file, in the same place - and the device ran for sixteen minutes
without touching it. Identified by MD5, not assumed.

What differs is `/data`. The recovery wipes it, and the OTA trigger had
evidently left a **pending-upgrade flag** there. So the loop was:

```
flag set in /data  ->  boot  ->  flash  ->  flag still set  ->  boot  ->  ...
```

and the factory wipe ended it by removing the flag, not by removing the
image. The same mechanism explains `zkautoupgrade` on external media: the
loader wants to be *told* there is an update pending, it does not simply
scan for files.

### What this changes

**"Staging is one-shot" is still right, for a different reason.** An image
left at `/mnt/storage/update.img` is not a loop - but it *is* what the reset
button installs. Leaving the STIPPLE image there meant the recovery button was
armed with the thing that broke the device. That is worse than a loop,
because it is silent until somebody reaches for it.

So the rule stands and the wording sharpens: **whatever sits at
`/mnt/storage/update.img` is the device's recovery image.** It should be
stock, or it should not be there. A pending update belongs somewhere the
reset button does not read.

## Second correction, 2026-09-24: it was neither a flag nor a file. It was a daemon.

Both explanations above were guesses at a mechanism nobody had read. The
mechanism has now been read, and it is `/bin/zkdaemon`. Full strings and
reasoning are in `docs/research/tc002-platform-findings.md`; the operative
part:

```
'sys.zkapp.state'  'running'  'ZK_APPCHECK_DELAY'
'[D][zkdaemon] app state: %s'
'[D][zkdaemon] Auto recovery triggered'
'setprop ctl.stop zkswe'   'rm -rf /data/*'
'/mnt/storage'  '%s/update.img'  '/bin/zkupgradebin'
```

`zkdaemon` polls the property `sys.zkapp.state`. If the application has not
set it to `running` within `ZK_APPCHECK_DELAY`, it wipes `/data` and
reinstalls whatever is staged. **No flag, no button, no file needs to be
involved.** The stock application sets that property - `libzkgui.so` carries
the string - and STIPPLE, which replaces the application, never did.

So the sequence was: STIPPLE booted fine, failed to announce itself, and was
deleted along with the Wi-Fi credentials in `/data/misc/wifi`, and stock was
reinstalled from the safety-net image. The progress bar was auto recovery.

**This is the third explanation for the same event.** The first two were
constructed from what was visible from outside - a file that was present, a
flag that must have been set - and each was consistent with the evidence to
hand and wrong. The difference this time is not that the story is neater: it
is that the mechanism was read out of the binary that implements it, and it
predicts the one detail neither earlier story could account for, namely why
*stock* came back with no Wi-Fi.

Worth keeping as a caution. Two plausible mechanisms were written down as
findings before anyone had looked at the thing doing the work.

### What this changes

**`Tc002Platform::announceRunning()` is now a precondition for flashing
anything**, and `stippleMain` calls it before opening the panel, the MCU or
the network.

**The rule about `/mnt/storage/update.img` gets stronger, not weaker.** The
previous correction called it "what the reset button installs". It is also
what an *unattended* recovery installs, triggered by a daemon on a timer. A
staged image is an armed revert, not a passive one.
