# 0016 — TC002 input layout: a knob and two buttons

- **Status:** Accepted, pending hardware confirmation
- **Date:** 2026-09-16

## Context

`platform::RawInput` has modelled the TC002's controls as `KeyLeft`, `KeyMiddle`,
`KeyRight` and a rotary encoder since Phase 3. That shape came from the blueprint
(§4, §15) and from the Ulanzi TC001, which does have three buttons. No one on
this project has ever held a TC002.

The default mapping that followed from it bound left and right to previous/next
app, middle to pause, and the knob to previous/next app as well — so two
different controls did the same job, and `Action::VolumeUp` and
`Action::VolumeDown` existed in the enum but were bound to nothing and
implemented nowhere.

Documentation for a third-party TC002 port describes the controls concretely:
"turn the knob to move between apps", "tap −/+ to lower/raise speaker volume by 5
percentage points on release", "hold −/+ for 0.7 seconds to lower/raise
brightness by 10". That is a rotary encoder and **two buttons labelled − and +**,
not three unlabelled ones.

This is second-hand — one person's device, one stock-app version — and it is
recorded as such in `docs/research/tc002-platform-findings.md`. But it is far
more specific than the assumption it replaces, and it is internally consistent:
labelled −/+ buttons only make sense next to a knob that does the navigating.

## Decision

Model the hardware as a knob that also presses, plus two labelled buttons:

```
RawInput::KeyMinus
RawInput::KeyPlus
RawInput::RotaryPress
RawInput::RotaryLeft
RawInput::RotaryRight
```

Name them for the labels on the case rather than for positions. `KeyLeft` invites
a binding to "previous", which is a sensible thing to put on a left-hand button
and a nonsensical thing to put on a button marked −.

Default bindings:

| Control | Short | Long |
|---|---|---|
| Knob turn | previous / next app | — |
| Knob press | pause | dismiss notification |
| − | volume down | brightness down |
| + | volume up | brightness up |

Tap for volume and hold for brightness puts the commoner adjustment on the
shorter gesture, and gives the only two labelled controls something to label.

Implement `Action::VolumeUp` / `VolumeDown` in `ApplicationHost` at the same
time, backed by a new `config.audio.volumePercent`. A default binding that
resolves to an unimplemented action is a dead button, which is the same class of
problem as the `autoBrightness` switch removed in the same change: a control that
looks real and does nothing.

## Consequences

**Good.**

- The model describes controls the device plausibly has, instead of controls it
  plausibly does not.
- No two controls duplicate each other, so the knob and the buttons each have a
  job.
- Volume works, through the API and the buttons, and persists across reboots.
- `ApplicationHost`'s action switch now enumerates every `Action` rather than
  ending in `default:`, so adding an action fails the build until it is handled.
  That is what would have caught the unimplemented volume case years earlier.

**Bad, or at least owed.**

- This is a breaking change to a public enum, and to the emulator's `data-source`
  ordinals which are positional. Both are updated; anything added later that
  hard-codes an ordinal will need the same care.
- It is a decision made on documentation about someone else's device. If that
  documentation is wrong, this is wrong.

## If the hardware disagrees

The tests under "the default layout" in `firmware/tests/test_input_mapper.cpp`
exist to be the thing that fails. In particular
`InputMapper.EveryPhysicalControlIsReachable` asserts that every value of
`RawInput` produces some action, which is the check that catches a control the
mapper has forgotten about.

If a real TC002 turns out to have three buttons after all, the change is small
and local: add the enum value, add a `ButtonBinding` for it, extend
`buttonIndex` / `bindingFor`, raise `kButtonCount`. Nothing above the §53
boundary knows how many buttons exist — apps and scenes only ever see `Action`,
which is the reason this is a cheap thing to get wrong.

Blueprint §15 puts mappings in configuration, so these remain defaults rather
than assumptions. What is *not* yet configurable is the binding set itself; that
belongs with the Phase 6 settings UI, and this ADR does not decide it.

## Related

- `docs/research/tc002-platform-findings.md` — the evidence, and what else it
  settles
- [0013](0013-platform-capability-model.md) — why a missing speaker reports
  absence rather than silently swallowing volume changes
