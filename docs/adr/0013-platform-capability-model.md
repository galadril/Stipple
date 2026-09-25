# 0013 — Required and optional platform capabilities

- **Status:** Accepted
- **Date:** 2026-09-11

## Context

`IPlatformServices` (blueprint §53) is the single seam between STIPPLE core and
the world. Some of what it exposes is universal — every platform has a panel, a
clock, somewhere to put bytes, and some way for a person to poke it. Other parts
are not: the simulator has no speaker, a device may ship without audio, and a
unit test harness has no network.

The question is what `audio()` should return on a platform that has no audio.

The tempting answer is a null-object: an `IAudioOutput` whose methods accept
every call and quietly do nothing. Calling code stays simple, no null checks,
no branches.

That answer is wrong here, for a reason specific to this project. STIPPLE is
being developed with no TC002 available (ADR 0011), so for six of the seven
phases the *only* implementation of these interfaces is the simulator. A null
object would let the entire notification-with-sound path be written, tested, and
declared working against a platform that has never made a sound. The tests would
pass. The feature would be a fiction discovered in Phase 7.

## Decision

Split the services in two, and make the distinction visible in the signature.

**Required** — returned by reference, never null:

```
IFrameBufferDisplay& display()
IInputDevice&       input()
ISystemClock&       clock()
IStorage&           storage()
```

Without any of these there is no product. Every adapter must supply them, and no
caller needs a null check.

**Optional** — returned by pointer, `nullptr` meaning "this platform genuinely
cannot do this":

```
IAudioOutput*    audio()
INetworkManager* network()
IRebooter*       rebooter()
```

Callers must handle absence. The type system makes forgetting hard.

Two supporting rules:

- `SimulatorCapabilities` lets a test switch any optional capability off, so the
  `nullptr` branch is exercised off-device. A branch that only ever runs on
  hardware we do not have is not a branch we have tested.
- Where the simulator *can* honestly stand in, it does, and says what it is
  doing. `SimulatorAudio` records playback requests and makes no sound: it
  verifies that STIPPLE asked for the right sound at the right moment, which is
  our logic, while claiming nothing about the speaker.
  `SimulatorRebooter` counts reboot requests and reboots nothing.

`IRebooter` is deliberately its own interface rather than a method on
`IPlatformServices`. Rebooting is the most destructive thing STIPPLE can do to a
clock on someone's desk, and code that needs it should have to be handed it.

## Consequences

**Good**

- A missing capability is a compile-time-visible pointer, not a silent no-op.
- Absent hardware cannot be faked into passing a test.
- Adapters stay honest: the Phase 7 TC002 adapter reports what the device can
  actually do, and core code already handles every combination.
- `name()` exists for diagnostics only. Core code branching on it would
  reintroduce exactly the platform coupling this boundary removes — anything
  that must differ belongs behind an interface instead.

**Bad**

- Every optional-capability call site needs a null check. Accepted: that check
  is the feature.
- Two ways of reaching a service is slightly more surface to learn than one.

**Revisit when**

- A third category appears — a capability that is present but temporarily
  unavailable (network while disconnected, audio while muted by a quiet-hours
  rule). That is a *state* on a present service, not absence, and should not be
  modelled by returning `nullptr` intermittently.
