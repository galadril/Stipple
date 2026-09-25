# Contributing to Stipple

**You do not need the hardware.** Everything above the platform boundary is
portable C++17 with a host test suite and a browser emulator, which is most
of the project. That was a deliberate design choice, and it is why the
simulator was built before bring-up rather than after.

## Getting started

```powershell
.\dev.ps1 doctor     # what your toolchain is missing
.\dev.ps1 test       # build and run the suite
.\dev.ps1 test Canvas   # only tests whose "Suite.Name" contains "Canvas"
.\dev.ps1 ci         # what CI runs: warnings as errors, strict goldens
```

Or directly: `cmake --preset host-debug`, `cmake --build --preset host-debug`,
`ctest --preset host-debug`.

Presets: `host-debug`, `host-release`, `ci`, `sanitize` (ASan/UBSan,
non-MSVC), `emulator`. See [docs/development/toolchain.md](docs/development/toolchain.md).

## The one rule that shapes everything

```
                    portable C++17, host-testable
┌─────────────────────────────────────────────────────────┐
│ Apps / Notifications / Scheduler / API / MQTT           │
│ Scene Model → Layout → Renderer → Framebuffer           │
└─────────────────────────┬───────────────────────────────┘
                   IPlatformServices
             ┌────────────┴────────────┐
      TC002 adapter               Simulator adapter
```

**Core code must not know whether it is running on hardware.** It may not
include a platform header. If you find yourself wanting a `#ifdef` for the
device, the thing you want belongs behind `IPlatformServices`.

The simulator is not a toy — it runs the *same* scene parser, layout engine,
font engine and app scheduler as the device. Only the adapter differs, and it
must stay that way.

## House rules

- **Reliability before feature count.** A feature that works every time beats
  five that half-work.
- **No AWTRIX source copying.** Stipple is an independent implementation.
  Other projects may be studied as product or UX references; concepts get
  reimplemented, not copied.
- **Nothing unbounded.** No unbounded queues, allocations or payloads. Treat
  RAM as a hard constraint — this device has 36 MB, and nothing may allocate
  during render.
- **No new dependencies without discussion.** The JSON parser, PNG encoder,
  test harness and MQTT client are all in-tree on purpose. Open an issue
  before adding one.
- **Capability absence must be visible.** A device that cannot measure a
  battery reports `known: false`, never `0%`. A plausible wrong answer is
  worse than an honest missing one.
- **Tests for core behaviour.** Not for everything; for anything somebody
  could break without noticing.
- **Document reverse-engineered platform behaviour** in `docs/research/`,
  with how you measured it.

## Tests

The harness is in-tree (`firmware/tests/support/`). A test is a function:

```cpp
NOTRIX_TEST(Stopwatch, HoldsTheTimeWhenStopped) {
    Stopwatch watch;
    watch.press(1000);
    watch.press(9000);
    STIPPLE_CHECK_EQ(watch.elapsedMillis(90000), 8000u);
}
```

### Golden images

`STIPPLE_CHECK_GOLDEN(name, framebuffer)` compares a rendered frame against
`firmware/tests/testdata/<name>.rgb`.

- A **missing** fixture is created locally, with a PNG beside it for review.
- A **mismatch** always fails, and writes 8× actual/expected PNGs to
  `testdata/_failed/`.
- `.\dev.ps1 golden` regenerates fixtures after an intentional change.

**Look at the PNG before you commit a regenerated fixture.** A blindly
updated golden records the bug instead of catching it. This is the one rule
most worth taking literally.

## Style

Warnings are errors in CI: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
-Wsign-conversion -Wold-style-cast`, or `/W4 /permissive-` on MSVC.

**Comments should say why, not what.** The code says what it does. A comment
earns its place by explaining a constraint, a measurement, or a decision that
looks wrong until you know the reason — especially anything learned from the
hardware the hard way.

## Changing the API

`docs/openapi.yaml` is checked against the router in CI. If you add or rename
a route, update the specification in the same commit or the build fails:

```bash
python3 tooling/api/check-openapi.py
```

Same for the web UI: `node tooling/web/check-app.mjs` catches calls to
functions that no longer exist, which is how a config page once shipped
broken.

## Working on the device

You do not need to flash anything to try a change on real hardware. Stipple
runs from `/tmp`, which is tmpfs — a power cycle restores the stock firmware
every time.

```powershell
.\dev.ps1 capture 192.168.1.42:5555   # a restore image, first, always
.\dev.ps1 deploy  192.168.1.42:5555   # cross-build, push to /tmp, run
```

Read [docs/bring-up.md](docs/bring-up.md) before the first time, and
[docs/recovery.md](docs/recovery.md) before you need it.

**Do not add anything that writes flash** without discussing it first. That
boundary exists because crossing it carelessly once cost a device.

## Pull requests

- One coherent change. Several unrelated fixes are several pull requests.
- Say what you changed and why. If it came from a measurement on hardware,
  include it — that is the most valuable thing in the repository.
- Tests for anything a future change could break silently.
- `.\dev.ps1 ci` should pass before you open it.

Significant architectural changes are worth an issue first, so the discussion
happens before the work rather than after.

## Licence

By contributing you agree your work is licensed GPL-3.0-or-later, like the
rest of the project.
