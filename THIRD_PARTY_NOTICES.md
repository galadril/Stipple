# Third-party notices

STIPPLE is licensed GPL-3.0-or-later. See [`LICENSE`](LICENSE).

Blueprint §5 requires that every imported dependency record its source, version,
licence, reason for inclusion and redistribution status. This file is the
register.

## Runtime dependencies

**None.**

`stipple_core` — the framebuffer, renderer, and everything that will sit above
the platform boundary — has no external dependencies. This is a deliberate
decision, recorded in [ADR 0012](docs/adr/0012-dependency-free-core.md).

## Build and test dependencies

| Component | Version | Licence | Why | Redistributed |
|---|---|---|---|---|
| CMake | ≥ 3.21 | BSD-3-Clause | Build system | No |
| Emscripten SDK | 3.1.64 | MIT / NCSA | Browser emulator target | No |

Neither is linked into any artefact; both are developer tooling.

## Pending

The FlyThings SDK components required for the ARM device build are **not yet
assessed**. Blueprint §46 questions 11 and 12 — which components may legally be
redistributed in build containers and release artefacts, and whether CI may
cache them — must be answered before Phase 7 produces a distributable build.
Until then no FlyThings component is vendored into this repository.
