# 0017 — One meaning per control: navigating NOTRIX from the device

- **Status:** Accepted, implemented
- **Date:** 2026-09-21

## Context

The bindings grew one at a time and never had a model behind them. What is on
the device today:

| Control | Short | Long |
|---|---|---|
| Knob turn | previous / next app | — |
| Knob press | pause carousel | dismiss notification |
| − | **volume down** | brightness down |
| Middle | next app | dismiss notification |
| + | **volume up** | brightness up |

Four things are wrong with that, and one of them is a live defect.

**The two most-pressed buttons do nothing.** `− ` and `+` are bound to volume on
short press, and this hardware reports `audio: false` — `/dev/mi_ao` exists but
nothing speaks the SigmaStar protocol yet. So the obvious thing to do with a
button marked `+` is press it, and pressing it has no effect whatsoever. This is
the same defect as the `autoBrightness` switch removed in ADR 0016's change: a
control that looks real and is not.

**Middle duplicates the knob.** Both advance the carousel. Five controls, and two
of them do the same job while brightness hides on a long press.

**There is no hierarchy.** Every binding is a global verb. Nothing can be entered
or left, so nothing that needs more than one control — choosing a clock face,
changing a colour, setting volume if audio ever lands — is reachable from the
device at all. Everything beyond "next app" requires a browser.

**Nothing is discoverable.** Brightness on a long press is not something anyone
finds; it is something they are told.

## Decision

**Each control means one thing, everywhere.** The mode changes what the thing
applies *to*, never what the control means.

| Control | Meaning, in every mode |
|---|---|
| **Knob turn** | Move between things |
| **Knob press** | Act on the thing |
| **− / +** | Adjust the thing's value |
| **Middle** | Back — leave, cancel, dismiss |
| **Knob long-press** | Enter settings |

That is the whole model. It is learnable in one sentence because the hardware
already suggests it: a knob is a selector, `−` and `+` are labelled for
adjustment, and a third button with no label is the one free to mean "back".

### What that produces

**Browsing** (the default, and where the device sits almost always)

- Turn → previous / next app
- Press → pause or resume the carousel
- `−` / `+` → **brightness**, immediately, with an on-screen bar
- Middle → dismiss a notification if one is showing, otherwise return to the
  clock

Brightness moves from a hidden long-press to the plain press, because it is the
only adjustment that always applies and the buttons are otherwise dead.

**Settings** (knob long-press)

- Turn → move through the settings list
- `−` / `+` → change the selected value
- Press → toggle, or commit a choice
- Middle → back out
- **Auto-exits after ten seconds of no input**, so the device cannot be stranded
  in a mode by someone who walked away

The list is one setting per screen — a label and a value, which is all 52×16
affords. Volume lives here rather than on a button, so if audio ever arrives it
has a home that does not need a new gesture.

### Why volume does not get a button

It is the obvious thing to put back on `−` / `+`, and it is wrong twice over: it
does not work on this hardware, and it would not be the most useful thing there
even if it did. Brightness applies to every device, every app, all the time.

When audio lands, `IAudioOutput` is already an optional capability, so the
settings list can show volume only where a speaker exists — which is what ADR
0013 is for.

## Consequences

**The `Action` enum has to change shape.** Today it is a list of global verbs
(`AppNext`, `BrightnessUp`). Under this model, what a gesture does depends on
mode, so `InputMapper` should emit *gestures* — turn, press, long-press, minus,
plus, back — and a new piece of core should own the mode and decide what each
gesture means in it.

That keeps `InputMapper` a pure hardware-to-gesture translator with no policy in
it, and puts the policy somewhere testable. It is a real refactor: the mapper,
its tests, `ApplicationHost`'s action switch, the emulator's on-screen pad and
the `/api/v1/input` endpoint all speak the current vocabulary.

**A settings mode needs rendering.** A label and a value on 52×16, plus a
brightness bar for the browsing mode. Both are small, but they are new screens
and `docs/DESIGN.md` will need to say what they look like.

**Apps can eventually own settings.** The same list mechanism that shows
"BRIGHT 168" can show a clock face or a visualiser palette, which is what task
#16 wants from the web UI. Same data, two front ends.

**Mappings stay configurable.** Blueprint §15 puts them in configuration, and
this ADR only sets the defaults — but the defaults are now a model rather than
an accumulation.

## Alternatives considered

**Keep the flat model and just fix volume.** Cheapest, and it fixes the dead
button. It leaves the device unable to change anything, which is the actual
complaint, and it leaves Middle duplicating the knob.

**A menu with a cursor and a scrolling list.** More conventional, and worse
here: 52×16 fits about eight characters, so a list is one visible row anyway.
One setting per screen is the same thing without pretending to be a list.

**Double-press for a second layer.** Blueprint §15 allows it "only if reliable",
and it is not: detecting it means delaying every single press long enough to see
whether a second arrives, which makes the common case feel broken.

## Implemented

`input::Navigator` owns the mode, the settings cursor and the idle timeout, and
holds no reference to settings or to a platform — it decides *what is selected*,
and `ApplicationHost` decides what changing it does. `InputMapper` was left
producing `Action` rather than being split into gesture translation: the named
actions (`brightnessUp`, `volumeUp`) are API and MQTT surface that a caller with
no on-device context still needs, so the relative ones (`adjustUp`, `back`,
`settingsToggle`) were added alongside them instead of replacing them.

Two things the implementation settled that the decision above did not:

**Settings render before the panel-power check.** Panel power is one of the
settings, so honouring "off" while the menu is open would black out the only
screen showing the control that turns it back on. The panel goes dark on leaving
settings, which is when the user can see it happen.

**Volume is hidden, not disabled.** `Navigator::setAvailable` skips it entirely
where `IAudioOutput` is null, and the cursor moves off it if it is hidden while
selected. A greyed-out entry would have been the same lie in a quieter voice.

The simulator claims audio by default, which is how the dead bindings survived
this long: every test that pressed those buttons had a speaker, and the device
does not.

## Amended once audio existed

The decision above put **brightness** on − / + and moved volume into settings,
on the grounds that volume was a control with nothing behind it: this hardware
reported `audio: false`, so those two buttons did nothing at all.

That has changed. `Tc002Audio` reaches the speaker through the vendor's own
library, the device reports `audio: true`, and volume is a real control again.
So the default flipped:

| Gesture | Browsing |
|---|---|
| Tap − / + | **Volume** where there is a speaker, brightness where there is not |
| Hold − / + | **Brightness** |

The model survives intact. − / + still mean "adjust the thing"; what the thing
is at the top level now depends on what the device can actually do. On a device
that makes noise, volume is what people reach for and brightness is set once
and left. On a device that cannot, the host falls through to brightness rather
than letting the buttons go dead — which is the defect the whole model exists
to prevent, and it must not come back by accident.

Hold is the one place a long press means something other than its short press.
It earns the exception: both are "turn this up", the direction is identical, and
a slip of the thumb changes the other quantity by one step rather than doing
something unrelated.

**The readout grew a label at the same time.** It had been a bare number in the
bottom rows, which answers "something changed" and not "what" — unacceptable
once the same two buttons reach two different quantities. It now uses the
settings screen's own layout, so adjustment reads the same on this device
whether you got there by holding the knob or by tapping a button.
