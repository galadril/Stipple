# NOTRIX

**Open pixel firmware for the Ulanzi TC002.**

NOTRIX replaces the stock pixel-app experience on the Ulanzi TC002 with a
renderer it owns end to end: a 52×16 framebuffer, deterministic custom apps,
notifications, HTTP and MQTT APIs, and a browser-based emulator. Local-first —
no cloud required.

> **Status: early development.** Phase 1 of 7. There is no installable firmware
> yet, and nothing here has run on real hardware. See [Roadmap](#roadmap).

---

## What works today

- A complete, tested 52×16 RGB framebuffer and `Canvas` with strict clipping
- A 5×7 proportional font engine with UTF-8, the degree sign, measurement and
  alignment
- The `IPlatformServices` hardware boundary, with a full simulator adapter
- Golden-image rendering tests with PNG diffs on failure
- A browser emulator running the real renderer compiled to WebAssembly

```powershell
.\dev.ps1 doctor     # check your toolchain
.\dev.ps1 test       # run the host test suite
.\dev.ps1 preview    # render frames to PNG and open them — no extra tooling
.\dev.ps1 serve      # build the emulator and open http://localhost:8080/
```

`preview` needs only a C++ compiler. `serve` additionally needs the Emscripten
SDK — see [`docs/development/toolchain.md`](docs/development/toolchain.md).

Setup instructions: [`docs/development/toolchain.md`](docs/development/toolchain.md).

## Why the emulator comes first

The project is being built without a TC002 on hand. That turns out to cost very
little, because the architecture already separates NOTRIX core from the hardware
at a single boundary:

```
                        NOTRIX CORE
┌─────────────────────────────────────────────────────────┐
│ Apps / Notifications / Scheduler / API / MQTT           │
│                         ↓                               │
│ Scene Model → Layout → Renderer → Framebuffer           │
└─────────────────────────┬───────────────────────────────┘
                   IPlatformServices
             ┌────────────┴────────────┐
      TC002 FlyThings             Simulator
      adapter                     adapter
```

Everything above that line is portable and host-testable. Only the adapter below
it needs a device. So the simulator is built first and device bring-up comes
last — and the emulator is not a mock, it is the real renderer with a different
output device.

Recorded in [ADR 0011](docs/adr/0011-simulator-first-development-order.md).

## Roadmap

| Phase | Content | Needs a TC002 |
|---|---|:--:|
| 0 | Repo skeleton, CMake, CI, ADRs | no |
| **1** | **Framebuffer, Canvas, golden-image tests** | no |
| **1b** | **WebAssembly browser emulator** | no |
| 2 | Font and text engine | no |
| 3 | `IPlatformServices` + simulator adapter | no |
| 4 | Scene model, app engine, configuration | no |
| 5 | HTTP API and notifications | no |
| 6 | MQTT and device web UI | no |
| 7 | TC002 bring-up: probe, cross-compile, sideload, device adapter | **yes** |

The full design is in
[`NOTRIX-PROJECT-BLUEPRINT.md`](NOTRIX-PROJECT-BLUEPRINT.md) — vision,
architecture, API design, security, update strategy and the open research
questions that are deliberately still unanswered.

## Repository layout

```
firmware/      core renderer, headers, host tests   (portable C++17)
simulator/     browser emulator (Emscripten)
docs/          architecture, ADRs, development guides
cmake/         shared build configuration
```

Directories for `sdk/`, `installer/`, `web/`, `integrations/` and `tooling/`
appear as their phases begin. NOTRIX is a monorepo by design — one issue
tracker, one release process, and a single PR can change firmware, API,
simulator and docs together (blueprint §41.1).

## Contributing

Work proceeds one phase at a time. Before architectural work, read the blueprint
and the [ADRs](docs/adr/). Significant architectural changes need an ADR of
their own.

House rules, from blueprint §44:

- Reliability before feature count
- No AWTRIX source copying
- All hardware behind interfaces; the simulator must stay supported
- No unbounded allocations or queues; nothing allocates in the render path
- Tests required for core behaviour

## Licence

[GPL-3.0-or-later](LICENSE). See
[ADR 0002](docs/adr/0002-gpl-license.md) for the reasoning and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the dependency register
(currently empty — the core has no dependencies).

---

NOTRIX is an independent open-source community project and is not affiliated
with or endorsed by Ulanzi or AWTRIX. Ulanzi, U-Clock and other product names
are trademarks of their respective owners.
