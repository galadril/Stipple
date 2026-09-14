# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository state

Phase 1 of 7. What exists: `notrix_core` (framebuffer, Canvas, test pattern), `notrix_imageio` (dependency-free PNG encoder), a host test suite with golden-image comparison, and a WebAssembly browser emulator. Directories for `sdk/`, `installer/`, `web/`, `integrations/` and `tooling/` do not exist yet — they appear as their phases begin.

**No code has ever run on a TC002.** There is no hardware available, and none is needed until Phase 7.

**Read `NOTRIX-PROJECT-BLUEPRINT.md` before any architectural work.** It is the single source of truth for scope, staging and naming. Sections worth re-reading per task: §6 (repo layout), §7–§15 (runtime architecture), §19–§21 (API/MQTT/config), §37 (stage roadmap), §46 (open research questions), §53 (the core architectural boundary).

**The phase order is deliberately not the blueprint's stage order.** Because there is no device, the simulator moved to the front and device bring-up to the back. See `docs/adr/0011-simulator-first-development-order.md`. Blueprint Stages 0 and 1 are this project's Phase 7.

| Phase | Content | Needs TC002 |
|---|---|:--:|
| 0 | Repo skeleton, CMake, CI, ADRs | no |
| 1 / 1b | Framebuffer + Canvas + golden tests; WASM emulator | no |
| 2 | Font and text engine | no |
| 3 | `IPlatformServices` + simulator adapter | no |
| 4 | Scene model, app engine, configuration | no |
| 5 | HTTP API and notifications | no |
| 6 | MQTT and device web UI | no |
| 7 | TC002 bring-up, device adapter | **yes** |

## What NOTRIX is

Open-source replacement *user application* for the Ulanzi TC002 pixel clock (52×16 RGB matrix, 832 pixels). The device is a SigmaStar SSD21x / ARMv7 Cortex-A7 running a FlyThings / EasyUI runtime; the NOTRIX application loads as `libzkgui.so` inside that host. Stage 1 replaces the app experience only — **not** the bootloader, kernel or Linux platform services.

It is not an ESP32 firmware, not a port of AWTRIX 3 or AWTRIX NG, and must not incorporate AWTRIX source. AWTRIX may be studied as a product/API/UX reference only; concepts (custom apps, notifications, rotation, indicators, MQTT) get reimplemented independently.

## Binding rules (from blueprint §44)

- Reliability before feature count. A feature that works 100% of the time beats five half-working ones.
- No AWTRIX source copying.
- All hardware behind interfaces (`IFrameBufferDisplay`, `IInputDevice`, `IAudioOutput`, `IMicrophone`, `INetworkManager`, `IStorage`, `ISystemClock`, `IRebooter`, `IUpgradeManager`). Core code must not know whether it runs on hardware or in the simulator.
- The simulator must remain supported and must run the *same* scene parser, layout engine, font engine, animation engine and app scheduler as the device. Only the platform adapter differs.
- No unbounded allocations or queues. Treat RAM as a hard constraint — bounded queues, bounded HTTP payloads, bounded notification count, bounded asset size, no duplicated framebuffers, no heap allocation during render.
- No persistent flashing logic without an explicit task saying so.
- Tests required for core behavior.
- Do not hand-edit generated FlyThings files.
- Document reversed/reverse-engineered platform behavior in `docs/`.
- Create an ADR (`docs/adr/`) for significant architectural changes.
- Keep GPL/third-party notices accurate. Project license is GPL-3.0-or-later (the official Ulanzi repo is GPL-3.0-or-later). Every imported dependency needs source, version, license, reason for inclusion, redistribution status.

## The architectural boundary that matters most

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

Render path: `Application → Scene → Renderer → 52×16 RGB framebuffer → TC002 display adapter → PageBase::sendLedData(...)`. NOTRIX owns the full framebuffer; do not depend on Ulanzi's DIY text/layout renderer for the core experience.

The public/native API is declarative **scenes** (JSON elements: pixel, line, rectangle, text, icon, bitmap, sprite, progress, graph, animation, group), not low-level internals.

**One API surface: `/api/v1/*`.** There is no AWTRIX compatibility layer and none is planned — blueprint §19.2 and the compatibility half of §3.5 are withdrawn, and blueprint Stage 7 is dropped. See `docs/adr/0015-no-awtrix-compatibility-layer.md`. Any `/api/*` path outside `/api/v1/*` answers 404 saying so explicitly. If compatibility is ever wanted it belongs outside the firmware as a translating proxy, never as device routes.

MQTT namespace is `notrix/{deviceId}/...`. MQTT is optional — HTTP-only and MQTT-only operation must both work.

## Non-obvious constraints

- The TC002 display API has an internal throttle; Ulanzi warns against frame intervals below ~15 ms. Do **not** target 60 FPS. Use dirty rendering, a frame budget, and 20–30 FPS for animation.
- The MCU must be initialized before normal LED-board operation.
- USB-C on the TC002 is mass-storage, not a serial/USB flashing path. A browser WebSerial/WebUSB flasher (ESP32-style) is **not** available. Development deployment is Wi-Fi ADB into `/tmp`, which is volatile — a power cycle restores the stock application. This is the default and safest dev mode.
- App ordering must never be inferred from filesystem enumeration or associative-container iteration; the app manager owns explicit ordering.
- Configuration is versioned (`schemaVersion`) with transactional writes, checksum, backup copy and migration code. Malformed JSON must never brick the device or cause a boot loop.
- Logging is a ring buffer — avoid flash writes. Never expose Wi-Fi passwords or secrets via diagnostics.
- Naming uses `notrix`, never the GitHub owner (`galadril`). Keep the owner out of firmware identifiers, MQTT topics, API names, package names, update manifests and persistent device config so a future transfer to an org is infrastructure-only. CI derives ownership from `${GITHUB_REPOSITORY_OWNER}`.

## Commands

```powershell
.\dev.ps1 doctor       # report toolchain status
.\dev.ps1 build        # configure + build core and tests
.\dev.ps1 test         # build and run the suite
.\dev.ps1 test Canvas  # only tests whose "Suite.Name" contains "Canvas"
.\dev.ps1 golden       # rewrite golden fixtures after an intentional change
.\dev.ps1 emulator     # build the WASM emulator (needs EMSDK)
.\dev.ps1 serve        # build it and serve on http://localhost:8080/
```

Or directly: `cmake --preset host-debug`, `cmake --build --preset host-debug`, `ctest --preset host-debug`. Presets: `host-debug`, `host-release`, `ci` (warnings as errors), `sanitize` (ASan/UBSan, non-MSVC), `emulator`.

CMake from a Visual Studio install is **not on PATH**; `dev.ps1` locates it via `vswhere`.

The blueprint's device verbs (`deploy`, `logs`, `restore`) arrive in Phase 7.

## Build and test architecture

**Targets.** `notrix_core` is portable C++17 above the §53 boundary — it must compile unchanged for host, WASM and ARM, and may not include a platform header. `notrix_imageio` (PNG) is deliberately a separate target so it can never be linked into the memory-constrained device build.

**No external dependencies**, by decision — see `docs/adr/0012-dependency-free-core.md`. The test harness (`firmware/tests/support/`) and PNG encoder are in-tree for this reason. Do not add a dependency to the core without an ADR. JSON in Phase 4 is the one open case where a library may be the right answer, since it parses untrusted network input.

**Golden-image tests.** `NOTRIX_CHECK_GOLDEN(name, framebuffer)` compares against `firmware/tests/testdata/<name>.rgb`. A *missing* fixture is auto-created locally with a reviewable PNG; a *mismatch* always fails. `NOTRIX_STRICT_GOLDEN=1` (set in CI) makes missing fixtures fail too. Failures write 8× actual/expected PNGs to `testdata/_failed/`. Never regenerate a fixture without looking at the PNG — a blindly updated golden records the bug instead of catching it.

**Warnings.** `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast` (or `/W4 /permissive-`), errors in CI. Sanitizers are host-only.

Blueprint's longer-term build layering still applies for Phase 7: CMake + a pinned Docker cross-toolchain (`arm-linux-gnueabihf`), headless, with compiler/SDK/image-digest all pinned — CI must never pull "latest" FlyThings dependencies.

## Working on tasks

Work stage by stage (blueprint §37). Never take on "build the complete firmware" — scope to the current stage and explicitly refuse adjacent stages (e.g. while in Stage 1, do not implement HTTP, MQTT, installer or OTA).

Per task: inspect existing code, state assumptions, make the smallest coherent change, add/update tests, run them, summarize changed files, list risks/TODOs, and do not silently widen scope.

The open research questions in §46 are deliberately unresolved (hardware revisions, `/tmp` override reliability, `update.img` packaging, FlyThings SDK redistribution rights, OTA safety). Do not paper over them with guesses — each answer becomes documentation or an ADR.
