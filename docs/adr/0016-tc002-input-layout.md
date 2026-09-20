# 0016 — TC002 input layout: a knob and three buttons

- **Status:** Accepted. Amended 2026-09-18, **confirmed against hardware 2026-09-19**
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

---

## Amendment, 2026-09-18: the third button is back

A second and independent source — a TC002 owner describing the stock firmware —
lists "1 knob with press/rotate" **and** "3 separate buttons". The port's
documentation this ADR was built on describes a knob and two buttons marked
− and +.

Both cannot be right about the count, and the most likely explanation is that
neither is wrong about what it was describing: a README explaining what a
firmware *does with* the controls is not an inventory of them, and a firmware
that binds two of three buttons reads exactly like the one quoted above.

### What changes

`RawInput` regains a third button:

```
KeyMinus, KeyPlus, KeyExtra, RotaryPress, RotaryLeft, RotaryRight
```

`KeyExtra` is named for what is actually known about it, which is nothing beyond
its existence. Calling it `KeyMiddle` or `KeyBack` would assert a position or a
purpose that no source supports, and a wrong name outlives the uncertainty that
produced it.

> **Amended 2026-09-20 — confirmed on hardware, and renamed to `KeyMiddle`.**
>
> `firmware/tools/input_probe` read the raw evdev stream while each control was
> pressed on a real TC002. All four keys `/proc/bus/input/devices` declares are
> wired: − is `KEY_DOWN` (108), the third button is `KEY_LEFT` (105), + is
> `KEY_RIGHT` (106), and the knob press is `KEY_UP` (103). The button is real,
> it sits between − and +, so the uncertainty this paragraph was protecting is
> gone and the honest name is now available. See
> `docs/research/tc002-platform-findings.md`.
>
> The decision below — model three rather than two — was right, and for the
> reason given: had we modelled two, a physical button on the case would have
> done nothing.
>
> The rotary is settled too. `knob_key` reports `ABS_X` but is not an axis: the
> value pair carries the direction — `{1, 8}` clockwise, `{11, 13}`
> counter-clockwise — and the alternation within a pair only exists because
> evdev drops unchanged `EV_ABS` values. `Tc002Input` converts each event into
> one `RotaryLeft` or `RotaryRight` tick, which is exactly the detent stream
> `InputMapper` was written against, so its acceleration logic needed no change.
>
> All six `RawInput` values now fire on real hardware.

Its default binding is `AppNext` on a short press and `NotificationDismiss` on a
long one — useful if the button is there, harmless if it is not.

### Why this way round

The asymmetry decides it:

- Model three, hardware has two → one enum value never fires. Invisible.
- Model two, hardware has three → a physical button on a shipped device does
  nothing, and its owner reasonably concludes the firmware is broken.

The first costs a few bytes of unreachable table. The second is the kind of
defect that gets reported as "NOTRIX doesn't work on my clock".

### What has not changed

The naming argument in the original decision stands: controls are named for
their labels, not their positions, and `KeyLeft` still invites bindings that
make no sense on a button marked −. Navigation stays on the knob; − and + keep
volume on a tap and brightness on a hold.

`InputMapper.EveryPhysicalControlIsReachable` still asserts that every `RawInput`
value produces some action, so a control added here without a binding fails the
build rather than shipping dead.

### How this actually gets settled

Not by a third document. `docs/bring-up.md` has the probe read the device's input
event codes directly, which reports what the hardware has rather than what
someone wrote about it. Until then this ADR is the best available guess, and it
is guessing in the direction that fails quietly.

---

## Confirmed on hardware, 2026-09-19

A TC002 was probed and each control pressed in a known order while `getevent`
watched. No more guessing.

**Three buttons plus a knob that presses and turns.** The amendment was right
and the original decision was wrong, which is the outcome the amendment's
asymmetry argument was designed to make cheap.

`soc:gpio_keys_1` (`/dev/input/event67`) carries four keys:

| Control | Linux code |
|---|---|
| Button 1 | `KEY_DOWN` (108) |
| Button 2 | `KEY_RIGHT` (106) |
| Button 3 | `KEY_LEFT` (105) |
| Knob press | `KEY_UP` (103) |

Rotation is a **separate input device**, `knob_key` (`/dev/input/event68`),
reporting `EV_ABS` on `ABS_X` as small discrete codes rather than a continuous
position.

### The naming decision paid off

The vendor's codes are directional names used as arbitrary identifiers. Nothing
about `KEY_LEFT` says the button is on the left, and nothing about `KEY_UP` says
the knob press means "up" — it is simply the constant they had spare.

Had `RawInput` kept positional names, the obvious thing would have been to wire
`KEY_LEFT` to `AppPrevious` and call it done. That would have been reasoning
from a vendor's arbitrary choice of enum value, and it would have felt correct
while being unrelated to where anyone's fingers are.

`KeyMinus` / `KeyPlus` / `KeyExtra` do not have that failure mode: they cannot
be mapped to these codes without someone first checking which physical button
does what. That check is still owed — which of the three buttons is marked − and
which +, if either, needs a look at the case.

### What is still open

- **Which physical button is which.** Press order established the codes, not the
  labels. `KeyMinus` and `KeyPlus` should be assigned by reading the case, not
  by assuming the capture order matched left-to-right.
- **What the knob's `ABS_X` values mean.** It declares an 8-bit 0-255 range,
  but observed values do not step like a detent position: two captures gave
  `11 8 1 8 1 ...` and `13 11 13 11 ...`. Position, quadrature state and
  gesture code all remain possible. A slow single-direction capture settles it.
  This is local to the device adapter — `InputMapper` consumes `RotaryLeft` and
  `RotaryRight` and does not care how they were derived.
