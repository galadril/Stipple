# 0012 — Dependency-free core and test harness

- **Status:** Accepted
- **Date:** 2026-09-11

## Context

`stipple_core` must compile for three targets: the host test runner (MSVC, GCC,
Clang), the Emscripten emulator, and eventually `arm-linux-gnueabihf` inside the
FlyThings environment. Blueprint §32 demands reproducible builds with pinned
dependencies and no silent downloads; §38 treats memory as a hard constraint;
§5 requires a licence, source, version and redistribution status recorded for
every dependency taken on.

Each third-party library therefore costs more than its download: a pin, a
licence entry, a CI network hop, and a cross-compilation risk on a toolchain we
have not yet reproduced.

## Decision

The core takes no external dependencies. Three places where a library would be
the reflex are handled in-tree instead:

1. **Test framework** — a ~120-line registry, assertion macros and `main()` in
   `firmware/tests/support/`, rather than Catch2 or GoogleTest.
2. **PNG encoding** — `firmware/imageio/` writes valid PNGs using stored
   (uncompressed) deflate blocks, rather than linking zlib or libpng. For a
   52×16 panel the size penalty is irrelevant, and it is kept out of
   `stipple_core` so it can never reach the device build.
3. **JSON** (Phase 4, ahead) — to be decided in its own ADR. The scene and
   configuration parsers face untrusted input over the network, so this one is
   not automatically in-house; it is the case where a hardened library may well
   beat hand-rolled parsing.

## Consequences

**Good**

- `cmake --preset host-debug && cmake --build` works offline, on a clean
  machine, with no package manager.
- Nothing to pin, audit, or record in `THIRD_PARTY_NOTICES.md` yet.
- No risk of a dependency failing to cross-compile for ARMv7 on a toolchain that
  has not been reproduced.
- The device build links only code written for the memory budget.

**Bad**

- The test harness lacks parameterised tests, mocking, tags and sharding. If the
  suite outgrows it, replacement is confined behind the `STIPPLE_TEST` and
  `STIPPLE_CHECK_*` macros.
- The PNG encoder produces files several times larger than a compressed encoder
  would. They are debug artefacts, so this does not matter.
- Hand-written infrastructure is code we own and must maintain. Accepted only
  because each piece is small, self-contained, and exercised on every run.

**Revisit when**

- The test suite needs fixtures or parameterisation badly enough that the macros
  become the obstacle.
- Anything needs real image *decoding* — reading PNGs is a different problem to
  writing them, and is not worth hand-rolling against untrusted input.
