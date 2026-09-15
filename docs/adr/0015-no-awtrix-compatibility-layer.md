# 0015 — No AWTRIX compatibility layer

- **Status:** Accepted
- **Date:** 2026-09-11
- **Amends:** [0001](0001-project-scope.md)

## Context

The blueprint planned two API layers (§3.5, §19.2): the native `/api/v1/*`
surface, and an AWTRIX-compatible subset under `/api/*` so existing AWTRIX
integrations could point at a NOTRIX device with little or no change. Blueprint
Stage 7 scoped that work and named the maintainer's own Domoticz AWTRIX NG
plugin as the reference client.

With `/api/v1/*` now built (Phase 5), the compatibility layer is the next piece
of that plan. The question is whether to build it at all.

Arguments for building it: existing AWTRIX users could migrate without touching
their automations, which lowers the barrier to trying NOTRIX and brings early
adopters with working setups.

Arguments against:

- It is permanent surface. Once an integration depends on `/api/notify`, that
  endpoint's exact behaviour — including its quirks — is frozen. The blueprint
  itself warns against letting historical AWTRIX behaviour constrain the
  architecture (§3.5).
- Claiming compatibility honestly is expensive. §3.5 requires contract tests and
  a published compatibility matrix before the claim can be made at all, and
  partial compatibility is arguably worse than none: an integration that works
  for four fields and silently ignores the fifth produces a bug report against
  NOTRIX for behaviour NOTRIX never promised.
- It costs device resources. Every compatibility route is flash and RAM on a
  platform where §38 treats memory as a hard constraint, spent on translating
  someone else's schema.
- It splits the documentation and the mental model. Two ways to do everything,
  forever.

## Decision

**NOTRIX serves `/api/v1/*` only. There is no AWTRIX compatibility layer, and
none is planned.** Blueprint §19.2 and the compatibility half of §3.5 are
withdrawn; blueprint Stage 7 is dropped rather than deferred.

Integrations target the native API.

A request to any `/api/*` path that is not `/api/v1/*` answers `404` with a
message saying so explicitly, rather than a bare not-found. Someone pointing an
AWTRIX client at a NOTRIX device should learn why it will not work from the
response, not from silence.

If AWTRIX compatibility is ever wanted, it belongs **outside the firmware** as a
translating proxy: a small service that accepts AWTRIX-shaped requests and calls
`/api/v1/*`. That placement is strictly better than in-firmware compatibility —
it costs the device nothing, it can be iterated and versioned independently of
firmware releases, and its inevitable gaps are the proxy's problem rather than a
frozen part of the device's public contract. Nothing in this decision prevents
someone building one.

## Consequences

**Good**

- One API, one set of documentation, one mental model.
- No permanent compatibility debt, and no risk of claiming a compatibility we
  cannot prove.
- The native API stays free to be designed well rather than shaped by another
  project's history.
- No contract-test suite or compatibility matrix to build and maintain.
- Flash and RAM stay spent on NOTRIX's own features.

**Bad**

- Existing AWTRIX automations do not work against NOTRIX. Every user migrating
  from AWTRIX has to rewrite their integration, which is real friction and will
  cost some early adopters.
- The maintainer's Domoticz AWTRIX NG plugin needs a NOTRIX mode before it can
  drive a NOTRIX device. That work moves from "maybe unnecessary" to "required",
  and it is now the first real consumer of the native API — which makes it a
  useful proving ground for whether that API is actually pleasant to use.
- NOTRIX loses an easy answer to "why should I switch?". The case now rests on
  the product being better, not on it being a drop-in.

**Neutral**

- Nothing already built is discarded: no compatibility code was ever written,
  and `matchRoute` never accepted anything outside `/api/v1`.
- AWTRIX remains a legitimate product and UX reference under
  [ADR 0001](0001-project-scope.md); this decision is about API surface, not
  about ignoring good ideas.
