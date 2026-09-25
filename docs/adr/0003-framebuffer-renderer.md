# 0003 — Framebuffer renderer owned by STIPPLE

- **Status:** Accepted
- **Date:** 2026-09-11

## Context

The TC002 exposes a text and layout renderer through Ulanzi's DIY API. Building
on it would be the shortest path to something on screen.

It would also make every §3.1 reliability guarantee unenforceable. Scrolling
that must never clip, two text elements that must always both render,
pixel-deterministic output — none of these can be promised on top of a renderer
whose behaviour is undocumented, untestable off-device, and subject to change in
any firmware revision.

## Decision

STIPPLE owns a complete 52×16 RGB framebuffer and renders everything itself:
pixels, lines, rectangles, sprites, icons, bitmap fonts, scrolling text,
progress bars, graphs, transitions and animations. The platform adapter's only
job is to hand a finished frame to the panel.

```
Application → Scene → Renderer → 52×16 framebuffer → display adapter → sendLedData
```

Implementation constraints, from §9 and §38:

- Storage is a fixed inline array: `52 × 16 × 3 = 2496` bytes. No heap, no
  resize, no double buffer until memory headroom is measured on real hardware.
- Nothing in the render path allocates.
- Every primitive is clipped through a single `intersect()`. Clip rects only
  ever shrink as `ClipScope`s nest, which is what makes "one component cannot
  corrupt another's region" (§9.3) a structural property rather than a
  convention.
- Out-of-bounds writes are asserted in debug and ignored in release. On a clock,
  an assert must not be the thing that takes the display down.

RGB888 is the starting point. RGB565 and palette formats stay open if profiling
on hardware demands them; the `Rgb` type is the only thing that would change.

## Consequences

**Good**

- The renderer is testable on a host with no device, which is what makes
  [0011](0011-simulator-first-development-order.md) possible at all.
- Golden-image tests can assert exact pixel output (§31.2).
- Emulator and device are pixel-identical by construction, not by comparison.
- Firmware-revision changes in Ulanzi's renderer cannot regress STIPPLE.

**Bad**

- Everything must be built: fonts, text measurement, scrolling, transitions. No
  borrowing the stock implementation for a quick win.
- 2496 bytes are spent unconditionally, before any app data.
- Frame pacing is now our problem. The documented ~15 ms minimum interval is a
  hard constraint (§9.4); the emulator paces at 30 FPS so that animations built
  in the simulator remain achievable on the device.
