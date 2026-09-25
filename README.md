# STIPPLE

**Open pixel firmware for the Ulanzi TC002.**

STIPPLE replaces the stock pixel-app experience on the Ulanzi TC002 with a
renderer it owns end to end: a 52×16 framebuffer, deterministic custom apps,
notifications, HTTP and MQTT APIs, sound, and a browser-based emulator.
Local-first — no cloud, no account, no vendor app.

> **Status: runs on real hardware, and now survives a power cycle.**
>
> STIPPLE installs into the device's `res` partition alongside a small shim
> that chooses what to run. A STIPPLE that will not load falls back to the
> stock Ulanzi clock rather than to nothing, so the device stays reachable.
> Updates after the first install are a file upload in the web UI — no
> flashing. See [docs/install.md](docs/install.md).

> ### ⚠️ Use entirely at your own risk
>
> **This software modifies firmware on a device it was not designed for.** It
> can leave your clock unusable, and getting it back may need hardware access,
> a USB recovery stick, or a device you are prepared to lose.
>
> That is not theoretical. During development this project bricked a device
> badly enough to need a recovery procedure obtained from Ulanzi support.
>
> STIPPLE comes with **absolutely no warranty of any kind** — see sections 15,
> 16 and 17 of the [GPL-3.0](LICENSE). Nobody involved is liable for damage to
> your hardware, lost data, voided warranty, or anything else that follows
> from using it. **If you are not willing to lose the device, do not install
> this.**

---

## What works, on hardware

**The panel.** 52×16 RGB through the vendor HAL, dirty rendering, a frame
budget, overlays and configurable transitions between apps.

**Apps and scenes.** A declarative scene model — pixel, line, rectangle, text,
icon, bitmap, sprite, progress, graph, animation, group — with a carousel you
can reorder, pin and configure per app.

**Controls.** One meaning per control: knob turns to move, press to act, −/+
to adjust, middle to go back, hold the knob for settings
([ADR 0017](docs/adr/0017-device-navigation-model.md)).

**Sound.** Tones and notification chimes through the SigmaStar audio path, with
volume on −/+. Plus a microphone-driven visualiser.

**Networking.** Its own DHCP client, Wi-Fi scanning and joining, and a setup
hotspot — the device hosts `STIPPLE-setup`, serves its configuration page on
`192.168.4.1`, and hands the radio back when you are done
([ADR 0018](docs/adr/0018-first-run-provisioning-and-access.md)).

**A web UI and an API.** One surface, `/api/v1/*`, served by the device itself
and reachable over HTTP or MQTT. The configuration page is compiled into the
binary, because a device whose storage has failed is exactly when its
configuration page is needed.

**Access control.** HTTP Basic over the page and the API alike, off by default,
with a physical way back: hold − and + for five seconds.

**A browser emulator.** The real renderer compiled to WebAssembly, serving the
real configuration page through the real router. Not a mock.

Roughly 840 tests, including golden-image comparison of rendered frames.

## Try it without a device

```powershell
.\dev.ps1 doctor     # check your toolchain
.\dev.ps1 test       # run the host test suite
.\dev.ps1 preview    # render frames to PNG and open them - needs only a compiler
.\dev.ps1 serve      # build the emulator and open http://localhost:8080/
```

`serve` needs the Emscripten SDK; see
[`docs/development/toolchain.md`](docs/development/toolchain.md).

## Try it on a device

**This does not modify your clock.** STIPPLE is pushed to `/tmp`, which is
tmpfs. A power cycle restores the stock firmware, every time. That is the whole
design of tier 2 in [ADR 0008](docs/adr/0008-installer-helper.md).

```powershell
.\dev.ps1 capture 192.168.1.238:5555   # read a restore image off your device first
.\dev.ps1 deploy  192.168.1.238:5555   # cross-build, push to /tmp, run
```

`capture` comes first for a reason worth reading:
[every TC002 ships a recovery image on its own USB volume, and it is not
necessarily the firmware that unit is running](docs/research/tc002-platform-findings.md).
On the unit this was developed against, holding the reset button installs an
*older* image than the device has. Capture from your own device, and the
physical recovery button becomes correct.

Full runbook: [`docs/bring-up.md`](docs/bring-up.md).

## What is not done

Stated plainly, because a status section that only lists wins is not a status
section.

- **It does not persist.** Making it survive a reboot means writing the `res`
  partition. The tooling to build and verify a flashable image exists and is
  verified against a factory image
  ([ADR 0020](docs/adr/0020-persistence-through-the-vendor-update-path.md)), and
  **nothing has been flashed**. ADR 0008 requires a restore path that has been
  *demonstrated*, not one that ought to work.
- **STIPPLE has never been built as `libzkgui.so`.** Persisting means becoming
  the shared library the vendor host loads, and nobody has tried it. That, not
  the flashing, is the unproven part.
- **No OTA updates.**
- **No installable release.** Releases package the emulator and say so.
- **Renewal of a DHCP lease is untested end to end.** The timing is tested on a
  host, the wire format against a real server; twelve hours apart, they have
  not yet met.

## How it is built

```
                        STIPPLE CORE
┌─────────────────────────────────────────────────────────┐
│ Apps / Notifications / Scheduler / API / MQTT           │
│                         ↓                               │
│ Scene Model → Layout → Renderer → Framebuffer           │
└─────────────────────────┬───────────────────────────────┘
                   IPlatformServices
             ┌────────────┴────────────┐
      TC002 adapter               Simulator adapter
```

Everything above that line is portable C++17 and host-testable. Only the
adapter below it needs a device, and the simulator runs the *same* scene
parser, layout engine, font engine and app scheduler as the hardware.

That boundary is why the simulator was built first and bring-up came last
([ADR 0011](docs/adr/0011-simulator-first-development-order.md)). It paid off:
when hardware arrived, every layer above the boundary already worked and had
tests, so bring-up was writing one adapter rather than debugging a whole system
through a 52×16 window.

**No external dependencies**, by decision
([ADR 0012](docs/adr/0012-dependency-free-core.md)). The JSON parser, the PNG
encoder, the test harness and the MQTT client are all in-tree.

## Repository layout

```
firmware/      core renderer, platform adapters, host tests   (C++17)
simulator/     browser emulator (Emscripten)
tooling/       device probe, cross-toolchains, image tooling  (Python)
docs/          architecture, ADRs, research, runbooks
```

The device configuration page lives in `firmware/web/` and is compiled into the
binary.

## Documentation

- [`STIPPLE-PROJECT-BLUEPRINT.md`](STIPPLE-PROJECT-BLUEPRINT.md) — the full design
- [`docs/adr/`](docs/adr/) — every significant decision, and why
- [`docs/research/tc002-platform-findings.md`](docs/research/tc002-platform-findings.md)
  — what the hardware actually does, measured rather than assumed
- [`docs/bring-up.md`](docs/bring-up.md) — day one with a new device
- [`docs/mqtt.md`](docs/mqtt.md) — the MQTT surface

## Contributing

Read the blueprint and the [ADRs](docs/adr/) before architectural work.
Significant changes need an ADR of their own.

House rules, from blueprint §44:

- Reliability before feature count. A feature that works every time beats five
  that half-work.
- **No AWTRIX source copying.** STIPPLE is an independent implementation.
- All hardware behind interfaces; the simulator must stay supported.
- No unbounded allocations or queues; nothing allocates in the render path.
- Tests required for core behaviour.
- Document reverse-engineered platform behaviour in `docs/`.

## Warranty, liability and risk

**There is none. You carry all of it.**

STIPPLE is licensed under the GPL-3.0-or-later, whose sections 15 and 16 say
this in legal terms. In plain ones:

- **No warranty.** The software is provided "as is". Nobody promises it works,
  is fit for any purpose, or will not damage your device.
- **No liability.** No contributor is responsible for a bricked clock, lost
  configuration, a voided manufacturer warranty, time spent on recovery, or
  any other loss — direct or indirect.
- **No support obligation.** Issues and questions are welcome and answered
  when someone has time. Nothing is owed to anyone.
- **Installing this will probably void your manufacturer warranty**, and it
  replaces the application your device shipped with.

This is a hobby project that modifies consumer hardware by methods the
manufacturer did not document or intend. It has damaged a device before and
it can damage yours. **Make the decision on the assumption that the device
might not survive it.**

Nothing here is legal advice, and a disclaimer does not override rights you
may have under local consumer law.

## Licence

[GPL-3.0-or-later](LICENSE). See
[ADR 0002](docs/adr/0002-gpl-license.md) for the reasoning and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the dependency register.

Restore images and partition captures are **not** redistributable — they are
Ulanzi's firmware, and they are specific to one unit. They are gitignored for
both reasons.

---

STIPPLE is an independent open-source community project and is not affiliated
with or endorsed by Ulanzi or AWTRIX. Ulanzi, U-Clock and other product names
are trademarks of their respective owners.
