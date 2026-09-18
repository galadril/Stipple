# TC002 bring-up

What to do with a device that has just arrived, in order, with the rules that
keep it recoverable.

Read this before plugging anything in. It takes five minutes and the mistakes it
prevents take considerably longer.

---

## The four rules

These are not advice. Each one exists because skipping it is how a device stops
working.

**1. Capture a restore image before you change anything.**
A restore image is Ulanzi's filesystem. We cannot ship one, cannot legally
redistribute one, and cannot make one for you. The only copy that will ever
exist for your device is the one you take off it while it still works. Take it
first, verify it, and put it somewhere that is not this repository.

**2. Never flash something that has not already run in `/tmp` on that exact
device.** Tier 3 writes the `res` partition in place, with no A/B copy. A
binary that has never executed on the hardware in front of you is the scenario
that write punishes.

**3. Record the stock-app and MCU version in everything.** A report without them
cannot be compared with anyone else's, or with your own from last month. The
probe puts them at the top for this reason.

**4. If something is uncertain, the probe answers it — not a guess.**
Nearly everything in `docs/research/tc002-platform-findings.md` is second-hand,
and two of its sources already disagree with each other. Reading the device is
cheap.

---

## Day one, in order

### 0. Before the device arrives

```powershell
.\dev.ps1 ci        # 546 tests, warnings as errors
.\dev.ps1 device    # cross-builds for ARMv7 and runs it under emulation
```

Both should pass with no hardware attached. If `device` fails, fix that first —
it is the same binary the clock will run, and a toolchain problem is much easier
to diagnose without a device in the loop.

You will need `adb`. It ships in the Android platform-tools; nothing else from
the Android SDK is required.

### 1. Use it as Ulanzi shipped it

Genuinely. Set it up through the stock app, put it on Wi-Fi, let it show the
time for a while.

Two reasons, both practical. It proves the hardware works before anything we do
can be blamed for it. And the stock app is currently the **only** documented way
to get the device onto a network — see "the provisioning gap" below.

Write down: the IP address, the stock app version, and what the buttons do.

### 2. Probe it, read-only

```powershell
adb connect 192.168.1.50:5555
python.exe tooling/probe/probe.py 192.168.1.50 --out device-probe.md
```

This runs only commands that read. It cannot write a file, stop a service or set
a property — `probe.py` refuses to start if anyone ever adds a command that
could, and the allowlist naming what may run is right at the top of the file.

The report answers, among other things:

| Question | Why it matters now |
|---|---|
| How much RAM? | Every budget in `test_memory_budget.cpp` is a guess until this |
| Two buttons or three? | Settles the disagreement in ADR 0016 |
| Which glibc? | Confirms whether static linking was necessary |
| Can it run an access point? | Decides whether a flashed device can ever be re-provisioned |
| How big is `res`, and how full? | Whether our 605 KB binary fits alongside what is there |
| What is the launcher? | We intend to replace it |

Read it before doing anything else. Several things we have built are guesses
this report either confirms or overturns, and overturning them costs nothing at
this point and a great deal later.

### 3. Capture the restore image

**This is the gate.** Nothing past here happens until there is a verified image
stored off this machine.

The mechanism is the one ADR 0008 describes and it is not implemented yet —
`dev.ps1 capture` arrives with the rest of the device verbs. Until it does, the
probe report tells you the partition layout, and the capture has to be done by
hand and checksummed.

Store it somewhere that is not this repository and not only this laptop. It is
vendor filesystem: do not publish it.

### 4. Run NOTRIX from `/tmp`

Tier 2 of ADR 0008. Volatile by construction — a power cycle brings the stock
application straight back, which is what makes this the safe loop and the one to
stay in.

Start with the smoke binary, not the full application:

```powershell
adb push build\device-arm\firmware\notrix_device_smoke /tmp/
adb shell chmod +x /tmp/notrix_device_smoke
adb shell /tmp/notrix_device_smoke
```

It prints what it did and exits. It drives the core through the *simulator*
adapter, so it touches no hardware at all — which is exactly what makes it a good
first thing to run. If it prints `OK`, then the CPU, the ABI and the libc are all
fine and every later failure belongs to the device adapter rather than the build.

If it does **not** run, the probe report has the answer: compare the glibc it
reports against what the binary needs (`readelf -d` shows none, because it is
static — so a failure here means something more interesting than a version
mismatch).

### 5. Everything else

The device platform adapter, the display path, the MCU protocol and the real
application loop are Phase 7 proper, and they are written against what the probe
found rather than against what we assumed.

---

## Recovery

Know these before you need them.

- **The knob held while powering on** starts the vendor application, restoring
  the stock UI, updater and ADB.
- **Three consecutive start-up crashes** make the platform fall back to the
  vendor launcher on the fourth boot.
- **There is a recovery/reset option** alongside the USB-C port, reported by a
  device owner. What it actually restores is unverified.

Below that floor nothing is validated. If neither the vendor application nor ADB
comes back, recovery needs the stock bootloader's update path or a serial
connection — which is why rule 1 exists.

NOTRIX's own anti-brick behaviour is separate and already built: three failed
boots put `ApplicationHost` into safe mode, which ignores stored configuration,
loads no apps, starts no MQTT, and draws something legible. That protects
against our own bad configuration. It does not protect against a bad flash,
because by then our code is what failed to start.

---

## The provisioning gap

**A flashed device has no documented way onto a new network.**

The stock app does the Wi-Fi setup. Once NOTRIX replaces the launcher, that app
is gone. If the device then moves house, or the router changes, there is
currently no known route back other than recovery.

This is the most important thing the probe can settle — hence the AP-capability
check. If the platform can bring up an access point, NOTRIX can offer
first-boot provisioning and the gap closes. If it cannot, that is a real
constraint on who should install this, and it belongs in the README rather than
in a footnote.

Until it is settled, the honest position is: **NOTRIX is for a device on a
network you control and do not expect to change.**

---

## What exists today

| Tier | State |
|---|---|
| 1 — Emulator | Done. `.\dev.ps1 serve`, real renderer and API, no device |
| 2 — `/tmp` trial | Binary builds and runs under emulation; `dev.ps1 deploy` not written |
| 3 — Flash | Designed only (ADR 0008). No code, and the gates are not negotiable |

`dev.ps1` gains `deploy`, `capture`, `flash`, `restore` and `logs` in Phase 7.
`doctor` and the probe are the parts that exist now, because they are the parts
that cannot hurt anything.
