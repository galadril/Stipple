# 0011 — Simulator-first development order

- **Status:** Accepted
- **Date:** 2026-09-11

## Context

The blueprint's stage roadmap (§37) runs Stage 0 (research and reproducibility
on real hardware) → Stage 1 (minimal display runtime on the device) → Stage 2
(simulator and rendering tests). That order assumes a TC002 is available from
the first day.

It is not. The project is starting without the hardware, and buying one does not
change the underlying question: how much of STIPPLE actually needs a device?

Reading §53, the answer is "very little". The architecture already places
`IPlatformServices` as the boundary between STIPPLE core and the world. Above it
sit the framebuffer, canvas, font engine, scene model, layout, app scheduler,
notification queue, configuration, HTTP API and MQTT client. All of that is
portable logic with no hardware dependency. Below it sit the LED panel, MCU,
buttons, rotary encoder, Wi-Fi, storage and clock.

Stage 2 also already requires that the simulator run *the same* scene parser,
layout engine, font engine and app scheduler as the device, with only the
adapter differing (§24). A simulator that shares the engine is not a mock — it
is the real system with a different output device.

## Decision

Reorder the roadmap so the simulator comes first and device bring-up comes last:

| Phase | Content | Hardware |
|---|---|---|
| 0 | Repo skeleton, CMake, CI, ADRs | no |
| 1 | Framebuffer + Canvas + golden-image tests | no |
| 1b | WebAssembly browser emulator | no |
| 2 | Font and text engine | no |
| 3 | `IPlatformServices` + simulator adapter | no |
| 4 | Scene model, app engine, configuration | no |
| 5 | HTTP API and notifications | no |
| 6 | MQTT and device web UI | no |
| 7 | TC002 bring-up: probe, cross-compile, sideload, device adapter | **yes** |

Blueprint Stage 0 and Stage 1 become Phase 7. Their content is unchanged; only
their position moves.

The browser emulator is built in Phase 1b rather than Phase 3 so that there is a
visible, interactive artefact from the very beginning. It is compiled from the
core with Emscripten, which also makes it the demo the public website needs
(§24, §26) at no extra cost.

## Consequences

**Good**

- Work starts immediately and roughly 85% of the roadmap is reachable without a
  device.
- The §53 boundary gets exercised by six phases of real use before the first
  adapter is written, rather than being validated once at the end.
- Every phase ships host tests and golden images, so the device bring-up in
  Phase 7 starts from a renderer that is already known-correct. When the first
  frame on real hardware looks wrong, the renderer is not a suspect.
- The emulator is a first-class deliverable rather than a tool retrofitted after
  the fact — which is what §24 wanted anyway.

**Bad**

- Hardware unknowns stay unresolved for longer. The §46 research questions —
  update packaging, `/tmp` override reliability, memory headroom, FlyThings
  redistribution rights — are all still open when Phase 5 finishes. None of them
  affect core code, but several affect whether the project can ship at all.
- Memory budgets (§38) cannot be validated against the real device. The core is
  written to the constraint (fixed buffers, no render-path allocation, bounded
  queues) but "fits in TC002 RAM" remains an assumption until Phase 7.
- There is a real risk of building polish on top of an unverified platform
  assumption. Mitigation: Phase 7 is scoped as an *adapter implementation*, and
  no consumer-facing release happens before it passes.

**Neutral**

- The device adapter is stubbed until Phase 7. Anything that cannot be honestly
  simulated returns a clearly-marked "not implemented" rather than a plausible
  fake, so absent functionality fails loudly instead of silently passing tests.
