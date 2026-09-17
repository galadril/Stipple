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

Tier 3 is for a device that has already run NOTRIX successfully in tier 2 on
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
notrix doctor  --host 192.168.1.42    # reachability, versions, capabilities
notrix deploy  --host 192.168.1.42    # tier 2, volatile
notrix capture --host 192.168.1.42    # write a restore image here
notrix flash   --host 192.168.1.42    # tier 3, gated on the above
notrix restore --host 192.168.1.42    # back to stock from a captured image
notrix logs    --host 192.168.1.42
```

`dev.ps1`'s device verbs and any future GUI helper wrap this, they do not
reimplement it. `capture` is additive to §27.3's verb list and follows from the
refusal above; `flash` is named for what it does rather than `install`, because
the two words carry very different amounts of warning.

### Crash-loop state is reported, not inferred

The platform abandons an application that crashes three times at start-up. A
NOTRIX that has been fallen back from is indistinguishable, to the user, from
one that never installed. `notrix doctor` must therefore be able to say which of
the two it is looking at, and the firmware must record start-up progress
somewhere that survives a crash. Otherwise the first real field report is "it
didn't work" with nothing attached.

### Tier 3 stays off until §27.4's gates pass

Blueprint §27.4's list — reproducible image, two devices, factory reset,
interrupted update, invalid image rejection, upgrade, downgrade, checksum, boot
health flag, rollback — is unchanged and unrelaxed. Until all of it passes,
`notrix flash` is not built into released binaries, and no public
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
