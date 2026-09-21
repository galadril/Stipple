# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository state

Phases 0–7 are done. NOTRIX runs on real TC002 hardware: it renders through the real `Canvas` and `Framebuffer` onto the panel, reads the buttons and knob, serves its web UI and API over HTTP, and talks to a broker over MQTT.

What exists: `notrix_core` (framebuffer, Canvas, font/text, scenes, icon store, app carousel, notifications, config, frame scheduler, ring log, `ApplicationHost`, the `/api/v1/*` server, the embedded device web UI and the MQTT bridge), `notrix_imageio` (dependency-free PNG encoder), **both** platform adapters — `simulator` and `tc002` (`Tc002Display`, `Tc002Input`, `Tc002Mcu`, `Tc002HttpServer`, `Tc002MqttClient`, `Tc002Platform`) — a host test suite with golden-image comparison, and a WebAssembly browser emulator that serves the real config page through the real router.

`tooling/` exists: `probe/` (read-only device reconnaissance, restore-image capture, ABI checking) and `cross/` (two pinned container toolchains — bookworm for libraries and static executables, bullseye for dynamic ones, because bookworm's executables demand `GLIBC_2.34` and the device has 2.30). Directories for `sdk/`, `installer/` and `integrations/` do not exist yet. The device UI lives in `firmware/web/` and is compiled into the binary by `cmake/EmbedWebAssets.cmake`; there is no top-level `web/`.

**The hardware is real and the findings are first-hand.** `docs/research/tc002-platform-findings.md` is the measured record: SSD21x dual-core Cortex-A7, 36 MB RAM, glibc 2.30, an 8 MiB `res` partition, and the display, input and MCU protocols decoded off the wire. Prefer it over the blueprint wherever the two disagree — the blueprint was written before anyone had a device.

**The panel needs GPIO 35 strobed.** Writing 3072 bytes to `/dev/spidev0.0` only fills the driver chips' shift registers; GPIO 35 low-before/high-after latches them onto the panel. Without the strobe every write succeeds, returns 3072 and lights nothing. That one fact explains most of the bring-up's confusing days.

**Read `NOTRIX-PROJECT-BLUEPRINT.md` before any architectural work.** It is the single source of truth for scope, staging and naming. Sections worth re-reading per task: §6 (repo layout), §7–§15 (runtime architecture), §19–§21 (API/MQTT/config), §37 (stage roadmap), §46 (open research questions), §53 (the core architectural boundary).

**The phase order was deliberately not the blueprint's stage order.** With no device to hand, the simulator moved to the front and bring-up to the back — see `docs/adr/0011-simulator-first-development-order.md`. That paid off: when hardware arrived, every layer above `IPlatformServices` already worked and had tests, so bring-up was writing one adapter rather than debugging a whole system through a 52×16 window.

| Phase | Content | Needs TC002 |
|---|---|:--:|
| 0 | Repo skeleton, CMake, CI, ADRs | no |
| 1 / 1b | Framebuffer + Canvas + golden tests; WASM emulator | no |
| 2 | Font and text engine | no |
| 3 | `IPlatformServices` + simulator adapter | no |
| 4 | Scene model, app engine, configuration | no |
| 5 | HTTP API and notifications | no |
| 6 | MQTT and device web UI | no |
| 7 | TC002 bring-up, device adapter | **yes** — done |

## What NOTRIX is

Open-source replacement *user application* for the Ulanzi TC002 pixel clock (52×16 RGB matrix, 832 pixels). The device is a SigmaStar SSD21x / dual-core ARMv7 Cortex-A7 with 36 MB RAM and glibc 2.30, running a FlyThings / EasyUI runtime. **Confirmed on hardware:** `/bin/zkgui` (9.5 KB) is the EasyUI host and loads the application from `/res/lib/libzkgui.so` (7.14 MB) at runtime — so the NOTRIX application really does load as `libzkgui.so` inside that host, exactly as blueprint §7.1 says. The LED panel is reached through the vendor HAL (`ledc_set_led` / `ledc_set_group` in `libzkhw.so`), not by driving SPI directly. See `docs/research/tc002-platform-findings.md`. Stage 1 replaces the app experience only — **not** the bootloader, kernel or Linux platform services.

It is not an ESP32 firmware, not a port of AWTRIX 3 or AWTRIX NG, and must not incorporate AWTRIX source. AWTRIX may be studied as a product/API/UX reference only; concepts (custom apps, notifications, rotation, indicators, MQTT) get reimplemented independently.

## Binding rules (from blueprint §44)

- Reliability before feature count. A feature that works 100% of the time beats five half-working ones.
- No AWTRIX source copying.
- All hardware behind interfaces (`IFrameBufferDisplay`, `IInputDevice`, `IAudioOutput`, `IMicrophone`, `INetworkManager`, `IStorage`, `ISystemClock`, `IRebooter`, `IUpgradeManager`). Core code must not know whether it runs on hardware or in the simulator.
- The simulator must remain supported and must run the *same* scene parser, layout engine, font engine, animation engine and app scheduler as the device. Only the platform adapter differs.
- No unbounded allocations or queues. Treat RAM as a hard constraint — bounded queues, bounded HTTP payloads, bounded notification count, bounded asset size, no duplicated framebuffers, no heap allocation during render.
- No persistent flashing logic without an explicit task saying so. One now
  exists and its design is [ADR 0008](docs/adr/0008-installer-helper.md): three
  tiers (emulator → volatile `/tmp` trial → gated flash), a restore image
  captured from the user's own device as a hard precondition, and no
  vendor-derived blob in any release. The code is Phase 7; the gates are not
  negotiable in it.
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

MQTT namespace is `notrix/{deviceId}/...`, off by default. Commands are translated into `api::Request` objects and answered by the same `ApiServer` as HTTP, so the two surfaces cannot drift — see `docs/mqtt.md`. `Tc002MqttClient` implements the transport on hardware; the simulator has an in-memory broker for tests.

## Non-obvious constraints

- The TC002 display API has an internal throttle; Ulanzi warns against frame intervals below ~15 ms. Do **not** target 60 FPS. Use dirty rendering, a frame budget, and 20–30 FPS for animation.
- The MCU must be initialized before normal LED-board operation.
- USB-C on the TC002 is mass-storage, not a serial/USB flashing path. A browser WebSerial/WebUSB flasher (ESP32-style) is **not** available. Development deployment is Wi-Fi ADB into `/tmp`, which is volatile — a power cycle restores the stock application. This is the default and safest dev mode.
- App ordering must never be inferred from filesystem enumeration or associative-container iteration; the app manager owns explicit ordering.
- Configuration is versioned (`schemaVersion`) with transactional writes, checksum, backup copy and migration code. Malformed JSON must never brick the device or cause a boot loop.
- Logging is a ring buffer — avoid flash writes. Never expose Wi-Fi passwords or secrets via diagnostics.
- Naming uses `notrix`, never the GitHub owner (`galadril`). Keep the owner out of firmware identifiers, MQTT topics, API names, package names, update manifests and persistent device config so a future transfer to an org is infrastructure-only. CI derives ownership from `${GITHUB_REPOSITORY_OWNER}`.

## Running on the device

Tier 2 of ADR 0008, and the only tier with code behind it. `/tmp` is tmpfs, so a
power cycle restores the stock application and nothing touches flash.

```powershell
# cross-build (podman/docker + the pinned toolchain in tooling/cross/)
podman run --rm -v "${PWD}:/src" notrix-cross:bookworm bash -c `
  "cmake --preset device-arm && cmake --build --preset device-arm --target notrix_device"

adb connect 192.168.1.238:5555
adb push build/device-arm/firmware/notrix_device /tmp/
adb shell chmod +x /tmp/notrix_device
adb shell setprop ctl.stop zkswe      # release the panel from the vendor app
adb shell /tmp/notrix_device          # hold this session open; it runs in the foreground
```

`setprop ctl.start zkswe` puts the stock application back, and so does a reboot.
The binary is static (~680 KB stripped) so nothing on the device has to satisfy
it. There is no `dev.ps1 deploy` yet; `device` cross-builds and runs under qemu,
`panel` pushes the channel-order test.

**Tier 3 (flashing) has no code and its gates are not negotiable** — ADR 0008
requires a verified restore image *and* a demonstrated restore path, and the
second does not exist: the device has no `dd`, `flashcp` or `nandwrite`, so
writing flash needs a tool we have not written.

## Commands

```powershell
.\dev.ps1 doctor       # report toolchain status
.\dev.ps1 build        # configure + build core and tests
.\dev.ps1 test         # build and run the suite
.\dev.ps1 test Canvas  # only tests whose "Suite.Name" contains "Canvas"
.\dev.ps1 ci           # what CI runs: warnings as errors + strict goldens
.\dev.ps1 golden       # rewrite golden fixtures after an intentional change
.\dev.ps1 emulator     # build the WASM emulator (needs EMSDK)
.\dev.ps1 verify       # drive the built WASM module under node
.\dev.ps1 device       # cross-build for ARMv7 and run it under emulation
.\dev.ps1 serve        # build it and serve on http://localhost:8080/
```

Or directly: `cmake --preset host-debug`, `cmake --build --preset host-debug`, `ctest --preset host-debug`. Presets: `host-debug`, `host-release`, `ci` (warnings as errors), `sanitize` (ASan/UBSan, non-MSVC), `emulator`.

CMake from a Visual Studio install is **not on PATH**; `dev.ps1` locates it via `vswhere`.

`docs/bring-up.md` is the day-one runbook for a new device: probe read-only first, capture a restore image before anything else, never flash what has not run from `/tmp` on that exact unit. `tooling/probe/probe.py` refuses to start if a mutating command is ever added to it.

The remaining device verbs arrive in Phase 7. ADR 0008 fixes the set as `doctor`,
`deploy`, `capture`, `flash`, `restore`, `logs` — `capture` and `flash` are
additions to blueprint §27.3, and `dev.ps1` wraps the CLI rather than
reimplementing it.

CI is `.github/workflows/ci.yml`; `release.yml` calls it via `workflow_call` on
a `v*` tag so a release cannot pass weaker gates than main. A release packages
the emulator only, states in its notes that no installable firmware exists, and
is always a prerelease while on 0.x. The tag, `project(VERSION)` in
`CMakeLists.txt` and `kVersion` in `firmware/include/notrix/core/Version.h` must
agree or the workflow fails before building — bump all three together. The Pages
job is opt-in behind the `NOTRIX_PAGES` repository variable and stays skipped
until someone sets it.

## Build and test architecture

**Targets.** `notrix_core` is portable C++17 above the §53 boundary — it must compile unchanged for host, WASM and ARM, and may not include a platform header. `notrix_imageio` (PNG) is deliberately a separate target so it can never be linked into the memory-constrained device build.

**The device UI is compiled in.** `firmware/web/*.html|css|js` become a C++ asset table via `cmake/EmbedWebAssets.cmake`, served by `web::StaticFiles` for any path outside `/api/`. A device whose storage has failed is exactly when its config page is needed, so the page must not live on that storage. Assets must be text — the generator emits raw string literals, deliberately using no tool beyond CMake so the Phase 7 cross-toolchain stays dependency-free. Editing a file under `firmware/web/` triggers a reconfigure.

**No external dependencies**, by decision — see `docs/adr/0012-dependency-free-core.md`. The test harness (`firmware/tests/support/`) and PNG encoder are in-tree for this reason. Do not add a dependency to the core without an ADR. JSON in Phase 4 is the one open case where a library may be the right answer, since it parses untrusted network input.

**Golden-image tests.** `NOTRIX_CHECK_GOLDEN(name, framebuffer)` compares against `firmware/tests/testdata/<name>.rgb`. A *missing* fixture is auto-created locally with a reviewable PNG; a *mismatch* always fails. `NOTRIX_STRICT_GOLDEN=1` (set in CI) makes missing fixtures fail too. Failures write 8× actual/expected PNGs to `testdata/_failed/`. Never regenerate a fixture without looking at the PNG — a blindly updated golden records the bug instead of catching it.

**Warnings.** `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast` (or `/W4 /permissive-`), errors in CI. Sanitizers are host-only.

Blueprint's longer-term build layering still applies for Phase 7: CMake + a pinned Docker cross-toolchain (`arm-linux-gnueabihf`), headless, with compiler/SDK/image-digest all pinned — CI must never pull "latest" FlyThings dependencies.

## Working on tasks

Work stage by stage (blueprint §37). Never take on "build the complete firmware" — scope to the current stage and explicitly refuse adjacent stages (e.g. while in Stage 1, do not implement HTTP, MQTT, installer or OTA).

Per task: inspect existing code, state assumptions, make the smallest coherent change, add/update tests, run them, summarize changed files, list risks/TODOs, and do not silently widen scope.

The open research questions in §46 are deliberately unresolved (hardware revisions, `/tmp` override reliability, `update.img` packaging, FlyThings SDK redistribution rights, OTA safety). Do not paper over them with guesses — each answer becomes documentation or an ADR.

`docs/research/tc002-platform-findings.md` records what a third-party TC002 port demonstrates about the hardware — packaging format and the 8 MiB res ceiling, the `zkswe` launcher, ~42 FPS, an Android-style property service, no ambient light sensor, a knob plus two buttons. It is second-hand and flagged as such; confirm on real hardware before depending on any of it. When gathering more: **read their documentation, never their source** (ADR 0001).
