# 0001 — Project scope

- **Status:** Accepted
- **Date:** 2026-09-11

## Context

The Ulanzi TC002 ships with a closed pixel-app experience. AWTRIX demonstrated
what an open 52×16 clock platform can be, but AWTRIX targets ESP32 hardware and
its source cannot simply be moved onto a SigmaStar SSD21x running FlyThings.

Without a written boundary, a project like this drifts: first a few patches over
the stock DIY API, then a partial AWTRIX reimplementation, then an attempt to
replace the Linux platform itself.

## Decision

NOTRIX replaces the **user application** on the TC002 and nothing below it.

In scope: a renderer NOTRIX owns end to end, a font and text engine, a scene
model, an app carousel, notifications, HTTP and MQTT APIs, a device web UI, a
simulator, an installer, and documentation.

Out of scope, per blueprint §2: a binary port of AWTRIX 3 or AWTRIX NG, ESP32
firmware, any dependency on AWTRIX source, the Ulanzi cloud, Ulanzi Studio after
installation, hacks layered over the stock DIY API, and any requirement that end
users install the FlyThings IDE.

Explicitly deferred, not rejected: replacing the bootloader, kernel or platform
services. That is separate research, not Stage 1 work.

AWTRIX may be studied as a product, API and UX reference. Custom apps,
notifications, app rotation, indicators and MQTT integration are generic ideas
and are reimplemented independently from public behaviour and documented
protocols.

## Consequences

- The FlyThings runtime is a constraint to live within, not an obstacle to
  remove. `libzkgui.so` is the delivery format.
- ~~Compatibility with AWTRIX integrations is a deliberate, tested subset under
  `/api/*`~~ — **withdrawn by [ADR 0015](0015-no-awtrix-compatibility-layer.md)**.
  NOTRIX serves `/api/v1/*` only; integrations target the native API.
- Contributors who arrive wanting to port AWTRIX have a document to be pointed
  at.
- "Reliability before feature count" (§3.1) is the tie-breaker whenever scope is
  contested.
