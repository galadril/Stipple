# 0002 — GPL-3.0-or-later licence

- **Status:** Accepted
- **Date:** 2026-09-11

## Context

The official Ulanzi TC002 source repository is GPL-3.0-or-later. NOTRIX runs
inside the FlyThings application model and will, at minimum, be built against
headers and interfaces from that ecosystem; it may end up incorporating or
deriving from GPL-covered Ulanzi code.

If a distributed work derives from GPL-covered source, the combined work must
comply with the GPL. Choosing a permissive licence would create ambiguity that
contributors cannot reasonably resolve themselves.

Blueprint §5 requires this decision before substantial implementation.

## Decision

NOTRIX is licensed **GPL-3.0-or-later**.

- `LICENSE` holds the verbatim GPL-3.0 text.
- Every source file carries `SPDX-License-Identifier: GPL-3.0-or-later`.
- `THIRD_PARTY_NOTICES.md` records every dependency with source, version,
  licence, reason for inclusion and redistribution status. It is currently empty
  because the core has no dependencies (see [0012](0012-dependency-free-core.md)).
- AWTRIX NG source is **not** copied into this project. Any proposal to do so
  requires separate licence review and an explicit decision recorded here.

## Consequences

- Maximum compatibility with the official Ulanzi source base; no ambiguity for
  contributors.
- Improvements to distributed derivatives stay available.
- Commercial closed-source redistribution of a derived work is not possible.
  Acceptable: NOTRIX is a community project, not a licensing play.
- Open question, carried from §46: which FlyThings SDK components may legally be
  redistributed in build containers and release artefacts. This affects
  packaging and CI caching, not the project licence, and is resolved in Phase 7.
