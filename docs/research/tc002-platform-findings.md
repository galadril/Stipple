# TC002 platform findings from a third-party port

- **Date:** 2026-09-16
- **Source:** public README of `github.com/sanderdw/awtrix-ng-tc002`, a TC002 port
  of AWTRIX NG
- **Status:** second-hand. Every item here is someone else's observation of their
  hardware and must be confirmed on our own device before anything depends on it.

## Why this document exists

Blueprint §46 lists hardware questions we deliberately refused to guess at, and
Stage 0 asks that reversed platform behaviour be written down rather than left in
someone's head. A working third-party port is the strongest evidence available
short of owning the device, so it is worth recording what it demonstrates —
and, just as importantly, what it does not.

**Nothing here is copied from that project.** ADR 0001 permits studying other
products as a reference and forbids taking their source; these are facts about
Ulanzi's hardware, not anyone's implementation. No code, markup or assets have
been read into this repository.

The line held while gathering this: **their documentation was read, their source
was not.** A README describing which buttons the hardware has is a fact about a
Ulanzi product. Their implementation of how to read those buttons is their work,
and reading it would compromise the independence this project is built on — so it
was left alone, and should stay that way when someone revisits this at Phase 7.

## What it settles

### The build path works (§46 Q13)

Cross-compiled for ARMv7 with the Arm GNU toolchain (they pin 9.2-2019.12),
driven by CMake, producing stripped binaries. Headless, no FlyThings IDE.

That is the approach ADR 0011 assumed and `docs/development/toolchain.md`
describes. It is no longer an assumption.

### An HTTP server runs on the device

Their build "serves the web UI on port 80". So a listening socket inside the
replaced application is possible.

This matters directly for `platform::IHttpServer`, which today returns null
everywhere with a comment saying the transport is unknown. It is not unknown any
more — only unwritten.

### Frame rate has more headroom than assumed

They report "about 42 FPS" for full 52×16 output — roughly a 24 ms frame.

Blueprint §9.4 warns against intervals below ~15 ms, and our `FrameScheduler`
takes 15 ms as the floor with a 30 FPS target. Both numbers look comfortable
rather than optimistic. Worth re-measuring ourselves, since 42 FPS may be their
achieved rate rather than a ceiling.

### Persistent install is a real, documented mechanism (§46 Q5)

An `update.img` is built against the stock image and validated on a "platform
header, CRC, payload MD5, filesystem bounds and 8 MiB res partition limit",
copied to `/tmp` alongside a helper binary, and installed by running the helper,
which "writes flash and reboots". A separate `restore-stock.img` returns the
device to the original filesystem.

So the format is structured and checkable, there is a real size ceiling
(**8 MiB** for the res partition), and a restore path exists. Blueprint §27.4's
list of safety gates before offering persistent flashing to users still applies
in full.

### The temporary path is even more temporary than we assumed

Their trial mode stops the `zkswe` launcher service, runs from an isolated `/tmp`
data directory, restarts the stock service afterwards — and is time-limited to
about 180 seconds.

Our `docs/development/toolchain.md` describes `/tmp` sideloading as the default
development mode. A three-minute window changes what that loop feels like, and
is worth confirming before Phase 7 plans around it.

### The replacement target is the launcher, not a library

They stop and replace the **`zkswe` launcher service**.

Blueprint §7.1 describes NOTRIX loading "as `libzkgui.so` inside that host". The
evidence points at replacing the launcher process rather than injecting a library
into it. If that holds, the §53 boundary is unaffected — our platform adapter
still sits underneath everything — but the Phase 7 entry point is a `main()`
rather than a library export. That is a smaller change than it sounds, and
`ApplicationHost::tick()` was already shaped for a caller-owned loop.

### Input is a knob plus two buttons

Described behaviour: "Turn the knob to move between apps", "Tap −/+ to lower/raise
speaker volume by 5 percentage points on release", "Hold −/+ for 0.7 seconds to
lower/raise brightness by 10."

So the physical layout is a **rotary encoder and two labelled −/+ buttons**, and
the natural mapping puts navigation on the knob, volume on a tap and brightness
on a hold.

`InputMapper` already models a rotary with acceleration, which is the part that
would have been painful to retrofit. Its defaults assumed `left / middle / right
/ rotary-press` — three buttons plus the knob — and bound left and right to
previous/next app, duplicating what the knob does.

**Acted on.** `RawInput` is now `KeyMinus / KeyPlus / RotaryPress / RotaryLeft /
RotaryRight`, with navigation on the knob, volume on a tap and brightness on a
hold. See [ADR 0016](../adr/0016-tc002-input-layout.md), which records the
reasoning, what it costs if this is wrong, and exactly which test should fail
first. Volume was implemented at the same time, because a default binding to an
unimplemented action is just a dead button.

This is the one finding in this document that has been built on rather than
merely recorded, so it carries the most risk if the source is wrong.

### The platform is Android-flavoured, not plain Linux init

Services are controlled with `setprop ctl.start zkswe` — Android's property
service, not sysvinit or systemd. Anything Phase 7 writes to start, stop or
supervise the application should expect that model.

### TLS is available, via a bundled OpenSSL

They ship "certificate-verified HTTPS with the bundled OpenSSL 3.5.8". So the
device can do real TLS, at the cost of carrying the library.

This does not threaten ADR 0012. TLS belongs to the platform adapter, below the
§53 boundary; `notrix_core` stays dependency-free either way. It does mean the
8 MiB res ceiling has to be budgeted against a bundled crypto library if we ever
want HTTPS.

### Audio, mDNS and NTP are all real

Speaker playback (including MP3 and HTTP streaming), volume control, mDNS
discovery and NTP sync are all listed as working. `IAudioOutput` has something to
bind to, and the splash's "show the IP address" fallback could eventually be
joined by an mDNS name.

### There is a concrete compatibility baseline

"Confirmed on TC002 stock app 1.1.1 / MCU V1.0.17."

The first real data point for §46 Q1/Q2. It does not tell us how much variation
exists across units, but it does mean stock-app and MCU versions are worth
recording whenever we test — a report without them is not reproducible.

## What it settles unhappily

### The TC002 has no ambient light sensor

Quoted: "the TC002 has fixed wiring and no light, temperature or humidity
sensor; those GPIO controls are hidden."

**This invalidates a setting we already shipped.** `config.display.autoBrightness`
came from surveying what a pixel-clock settings page usually offers. On this
hardware nothing could ever honour it, so it is a switch that does nothing —
precisely the kind of quietly-lying control this project keeps refusing to build
elsewhere. It has been removed.

If a future device does have a sensor, the honest shape is an optional platform
capability that reports its presence, exactly as ADR 0013 handles audio and
network — not a config flag that hopes.

Battery percentage *is* available from the MCU, and the microphone is documented
by Ulanzi, so a sensor interface is still worth having eventually. It should
report what exists rather than assume a fixed set.

### There is a display quirk nobody has explained

They report flicker on specific dim greens — `#004200` and `#004B00` — at any
brightness, cause unknown, worked around by nudging the colour.

Recorded because it would otherwise cost days: a renderer producing those exact
values would look broken through no fault of its own. If we see it, this is the
first thing to check rather than the last.

## What it does not settle

- **First-time Wi-Fi provisioning.** No AP mode, hotspot or captive portal is
  documented. Their instructions are explicit that you "connect the TC002 to
  Wi-Fi using its stock app and find its IP address" *before* installing
  anything — so the stock application does the provisioning and the replacement
  inherits a configured network.

  That is a real gap rather than a solved problem: it means a device that is
  flashed and then moved to a new network has no documented way back. Our splash
  showing the IP address helps only once the device is already on a network.
  Whether the platform can bring up an access point at all is still unknown, and
  it is the single most useful thing to test when hardware arrives.
- **RAM.** No figure given. The 8 MiB res limit is flash, not memory. Every
  budget in `test_memory_budget.cpp` — the 12 KB icon store, the 128 KB
  worst-case app storage — is still unvalidated.
- **The MCU protocol.** Its version matters (V1.0.17 above) and the blueprint
  says it must be initialised before normal LED operation, but nothing here
  describes the link itself.
- **Which hardware revisions this applies to** (§46 Q1, Q2). One device, one
  stock-app version, one MCU version.

## Update: their installer tooling (2026-09-17)

The same project has since published an end-to-end install path — a one-line
`curl | sh` installer, a RAM-only trial runner, and a documented recovery
procedure. Same provenance rule as above: **their README was read, their scripts
were not.** What follows is the workflow they describe, because the *sequence* is
the reusable insight; their implementation of it is theirs.

Worth saying plainly, because it is easy to assume otherwise: this tooling is
not AWTRIX NG's. It is TC002-specific work by that project's author, so ADR 0001
does not speak to it. What does speak to it is licensing — GitHub reports the
repository's licence as `NOASSERTION`, meaning no recognised licence could be
identified. Compatibility with our GPL-3.0-or-later cannot be established from
that, so their code stays out regardless of ADR 0001, and §42's dependency
register would have nothing valid to record. Reimplementing a documented
workflow is unaffected.

### The shape of their install path

Three tiers, escalating in permanence:

1. **Trial** — ADB-push a binary, stop the launcher, run from an isolated `/tmp`
   data directory, web UI on port **18081**, killed after ~180 s. Confirms the
   180-second figure already recorded above, and that the trial deliberately
   uses a *different* port from the installed app's 80.
2. **Install** — read the device's application partition, verify it against
   supported stock versions, build the image locally, run a preflight **on the
   clock**, require the operator to type `flash`, then write in place (~3 min).
3. **Restore** — the same helper run against a `restore-stock.img`.

Their preflight checks, which is the part worth copying as a *list*: platform
header, CRC, payload MD5, filesystem bounds, the 8 MiB res limit, vendor files
against recorded fingerprints, and partition geometry. Unrecognised stock
firmware is refused unless explicitly forced.

### Three facts that change our plans

**There is no A/B partition.** Quoted: installing "writes the `res` flash
partition in place. There is no A/B copy on the clock." A power cut mid-write
leaves a partition needing recovery. Blueprint §27.4 already demands rollback be
verified before persistent flashing ships — this says rollback cannot be an
A/B swap, so it has to be the restore image plus the recovery path below.

**Recovery is a hardware gesture, and it has a floor.** Holding the knob while
powering on launches the vendor application, which brings back the stock UI,
updater and ADB. Independently, three crashes in a row at start-up triggers a
launcher fallback on the fourth boot. Both matter to us directly: the second one
means *our* application must not crash-loop silently, because the platform will
quietly stop running it — and a NOTRIX that has been fallen back from looks
identical to one that was never installed. Phase 7 should expect to surface that
state rather than let the user guess.

They are explicit that below this there is nothing validated: if neither the
vendor application nor ADB returns, recovery needs the stock bootloader's update
path or a serial connection, untested. So the recovery story has a documented
floor, not a guaranteed one.

**The images are built from the user's own device, not downloaded.** Their
generator takes a stock image and a live res dump from a `device-private/`
directory that is kept out of the repository, and the install path reads the
clock's own partition. Nothing vendor-derived is redistributed.

That is the answer to §46 Q5's redistribution half, and it is a constraint on our
release process rather than an implementation detail: **NOTRIX must never publish
a `restore-stock.img` or any vendor-derived blob as a release asset.** The
restore image is something the installer *produces locally* from the device in
front of it. A release can ship our payload and the tool; it cannot ship
Ulanzi's filesystem.

### What this does not give us

A payload. Their installer's hard part is validating and writing the `res`
partition; the binary it writes is their application. We have no ARM preset, no
TC002 platform adapter and no device build, so there is nothing for an
equivalent installer to carry yet. The ordering stands: Phase 7 produces a
binary, and only then is an installer meaningful.

It also still does not settle first-time Wi-Fi provisioning — the gap recorded
above is untouched by any of this. Their install path assumes a device already
on the network, and a flashed device moved to a new network remains without a
documented way back.

## Consequences taken

1. `display.autoBrightness` removed from configuration and the API.
2. `platform::IHttpServer` keeps its shape; only the implementation is missing,
   and it is now known to be possible.
3. Phase 7 should expect to provide a `main()` replacing the launcher rather than
   a `libzkgui.so` export. No change to the §53 boundary.
4. The 180-second trial limit, the 8 MiB res ceiling and the green-flicker quirk
   are recorded here so Phase 7 does not rediscover them.
5. `RawInput` and the default bindings were rewritten for a knob plus two
   labelled buttons (ADR 0016), and `Action::VolumeUp` / `VolumeDown` were
   implemented so those bindings do something.
6. Phase 7 test reports should record the stock-app and MCU versions, since a
   report without them cannot be compared against anything.
7. Releases publish only what exists. `.github/workflows/release.yml` packages
   the emulator, states in the release notes that no installable firmware
   exists, and marks every 0.x tag a prerelease. Device artifacts join that
   workflow in Phase 7.
8. No vendor-derived blob ever becomes a release asset. The restore image is
   generated locally from the device being installed onto.
9. Crash-loop visibility is now a Phase 7 requirement, not a nicety: the
   platform falls back to the vendor launcher after three failed start-ups, and
   that state must be reported rather than left looking like a failed install.
