# NOTRIX design

How things look and move on a 52×16 panel, and why.

This is a specification, not a mood board. On 832 pixels there is no such thing
as a small visual decision: one pixel is 6% of the height, and a glyph that is
one column too wide does not look slightly cramped, it collides. Every number
here exists because something breaks without it.

It was written after the calendar face shipped with the day number overlapping
its own icon border, and after `apps.transitions` turned out to be a switch
connected to nothing. Both are the same failure — a visual decision made once,
in one file, with nothing to check it against.

---

## 1. The canvas

```
52 columns × 16 rows, RGB888, origin top-left
```

Fixed forever. `Framebuffer::kWidth` / `kHeight` — never hard-code 52 or 16.

**Everything is whole pixels.** No sub-pixel positioning, no anti-aliasing, no
scaling. A design that needs half a pixel needs a different design.

**Nothing may rely on colour alone to carry meaning.** At this size a colour
difference can be a single lit pixel. Anything important must also differ in
shape or position — see §4.

---

## 2. Type

One font: `text::font5x7()`. 5 columns × 7 rows, proportional, 1 column of
letter spacing.

Derived widths, all in `ClockApp.cpp` and all load-bearing:

| | Width | Derivation |
|---|---|---|
| Digit | 5 | the font cell |
| Gap | 1 | letter spacing |
| Digit pair | **11** | `5 + 1 + 5` |
| Separator (`:`) | **3** | `1 + 1 + 1` — gap, colon, gap |
| `HH:MM` | **25** | `11 + 3 + 11` |
| `HH:MM:SS` | **39** | `11 + 3 + 11 + 3 + 11` |

**The colon occupies a reserved 3-column slot whether lit or not.** It is one
pixel wide and a space is two, so blinking by swapping glyphs shifts the minutes
sideways twice a second. Pinned by `Clock.BlinkingTheColonDoesNotShiftTheDigits`.

**Digit slots are fixed, not measured.** `0` lights column 0 of its cell and `1`
does not, so laying out from ink extents makes the minutes move when the hour
changes. Pinned by `Clock.TheHourFieldNeverMovesTheMinutes`.

### Vertical rhythm

16 rows, 7-row glyphs. Only three layouts fit:

| Layout | Rows | Use |
|---|---|---|
| **Single line** | `y = 4` (4–10) | one thing, centred: `HH:MM`, a notification |
| **Two lines** | `y = 1` and `y = 9` | title over detail: the splash |
| **Line + rule** | `y = 3` (3–9), rule at 14–15 | content with a progress bar: seconds bar |

Two 7-row lines need 14 of 16 rows. There is no room for a third, and no room
for generous padding — one row above and one below is the whole budget.

---

## 3. Colour

Three roles, not three colours. A face picks what fills them.

| Role | Means | Default |
|---|---|---|
| **Primary** | the content itself — digits, text | `#FFFFFF` |
| **Accent** | structure and the secondary reading — colon, icon frames, bars | `#00BEFF` |
| **Inert** | present but unlit — the empty part of a bar | `#121212` |

Inert is not black. A bar that vanishes when empty reads as broken; a bar that
dims reads as empty. At brightness 40 it is nearly invisible, which is correct —
it is scaffolding, not information.

**Brightness is applied by the platform, after rendering.** Apps always draw in
true colour and never pre-dim. `Tc002Display` scales every channel on the way
out.

---

## 4. Icons and framed elements

A framed icon is a 1px border with content inside. The interior is what the
content must fit, and it is the number people get wrong:

```
interior width  = width  − 2
interior height = height − 2
```

**A frame containing a two-digit number is 13 × 14.** Interior 11 × 12, which is
exactly a digit pair (11) with a 7-row glyph and space for a header band.

This is the calendar icon's spec, and 13 is not negotiable: at 11 the interior is
9, a digit pair is 11, and the number overlaps both borders. That shipped.

```
      13 wide
  ┌───────────┐  ← header band, 3 rows, accent filled
  │▓▓▓▓▓▓▓▓▓▓▓│
  │           │
  │  1 7      │  ← day, primary, 11 wide, centred in the 11-wide interior
  │           │
  └───────────┘  14 tall
```

Content inside a frame is positioned against the **interior**, never the frame
origin. `x + 1`, not `x`.

---

## 5. Motion

The panel is not a screen. Motion is expensive to look at from across a room,
and this device sits in someone's peripheral vision all day.

**Three rules:**

1. **Nothing animates that does not carry information.** The colon blink says
   the clock is running. The seconds bar says where you are in the minute. A
   thing that moves for decoration is a thing that will be switched off.

2. **Animation is a pure function of elapsed time.** `f(elapsedMillis)`, no
   accumulated state. This is what makes golden-image tests possible at all —
   an animation that depends on how many frames happened to render is untestable
   and drifts between the emulator and the device.

3. **Nothing moves faster than it can be read.** Scrolling text runs at 18 px/s
   on the splash, where the window is short, and slower in normal apps.

### Frame budget

15 ms floor (`IFrameBufferDisplay::minimumFrameIntervalMillis`), 20–30 FPS
target. The bus can do ~400 Hz; that is not permission.

**On the TC002 a skipped frame is a dark frame.** The driver chips hold an image
only while being written, so dirty-rectangle rendering may skip the *render* but
never the *write*. `Tc002Display::refresh()` exists for this.

---

## 6. Transitions

When the carousel moves between apps, the change should read as *movement
between two things* rather than one thing being replaced.

| Style | What it is | When |
|---|---|---|
| **None** | instant swap | the default when `apps.transitions` is off |
| **Slide** | outgoing leaves left, incoming enters right | app → app |
| **Fade** | crossfade through black | app → notification, and back |

**Duration: 300 ms.** Long enough to read as motion, short enough that a
four-app carousel at 8 s each does not spend 15% of its life in transit.

Direction follows intent: turning the knob clockwise slides left, so the content
moves the way the knob did. A transition that contradicts the input feels like
lag.

**Transitions are skippable.** A button press during one completes it
immediately. Someone pressing a button has already decided; making them watch an
animation first is the interface arguing.

---

## 7. Overlays

An overlay draws *over* whatever app is showing, without replacing it — rain
falling across the clock, a frost edge, a storm flicker. AWTRIX does this well
and it is worth having, but it needs a rule or it becomes noise on top of the
one thing the user actually wanted to read.

**An overlay may never make the content underneath unreadable.**

That is the whole constraint, and on 832 pixels it is a tight one. It follows
that:

- **Overlays are sparse.** A handful of lit pixels at a time, not a field.
  Rain is a few falling columns, not a curtain.
- **Overlays own their own colour**, and it is neither primary nor accent —
  otherwise a raindrop reads as part of the time. Cool blues and whites, dimmer
  than the content.
- **Overlays never occupy the centre band.** Rows 4–10 are where a single line
  of type lives (§2), so overlays work the edges: rows 0–3 and 11–15, and the
  outer columns.
- **Overlays are additive, never destructive.** They light pixels the app left
  dark; they do not overwrite lit ones. A drop passing "behind" the digits is
  correct.
- **One at a time.** Two overlays composited is a weather report nobody can
  read.

Like every other animation, an overlay is a pure function of elapsed time
(§5.2) so it stays golden-testable.

The set stays small and physical — the things a pixel clock can suggest in a few
pixels: rain, snow, storm, frost. Not a taxonomy of meteorological conditions.

## 8. Interaction

The visual half of this document had no counterpart for a long time, and it
showed: the controls were bound one at a time, until the two most obviously
pressable buttons on the case did nothing at all. The model is ADR 0017; what
follows is what it looks like on screen.

**Each control means one thing, everywhere.** Turn the knob to move between
things, press it to act on one, use − / + to change its value, press the middle
button to go back, hold the knob for settings. The mode changes what a control
applies *to*, never what it means.

### Feedback is not optional

Anything a control changes must be visible on the panel at the moment it
changes. A brightness step is invisible in daylight and at night reads as the
panel having glitched, so − / + while browsing draw a readout. It stays for
1200 ms — long enough to read after the press that caused it, short enough not
to hide the clock.

The readout uses the settings screen's own layout: label on the first line,
value on the second, bar on the last row. It began as a bare number and that
was not enough — it answered "something changed" and not "what", which stopped
being survivable once the same two buttons reached volume on a tap and
brightness on a hold. One visual language for adjustment, however you got
there.

This is not decoration. A control with no feedback is indistinguishable from a
broken one, which is how volume sat on those buttons doing nothing.

### One setting per screen

52×16 fits about eight characters. A scrolling list is a list with one visible
row, so settings show a single entry at a time: label on the left in primary
white, value on the right in the accent colour, and a bar underneath for
anything with a range. Two-state settings show ON/OFF in green or orange and no
bar — a slider that is either full or empty reads as broken.

### No mode is a trap

Settings close themselves after ten seconds of no input. The panel cannot say
"you are in a menu" any other way, so a device left mid-adjustment would
otherwise show `BRIGHT 168` until somebody touched it.

For the same reason the settings screen is drawn *before* panel power is
honoured: turning the panel off from the menu must not black out the control
that turns it back on.

## 9. What this rules out

Written down because each was considered and rejected, and someone will propose
them again:

- **A second font.** 52 columns fits ten 5px glyphs. A larger face fits four
  and cannot show `HH:MM`; a smaller one is unreadable across a room. One font
  is not a limitation here, it is the answer.
- **Anti-aliasing or opacity.** These chips are 8-bit per channel with no
  blending; a "50% white" pixel is just a dimmer pixel, and at this scale it
  reads as a mistake rather than as softness.
- **Decorative animation.** See §5.1.
- **Colour-only state.** See §1.

---

## 10. When adding something visual

1. Does it fit one of the three vertical layouts in §2? If not, say why.
2. Does it use the three colour roles from §3, not new colours?
3. If framed, is the content positioned against the interior?
4. If it moves, is it a pure function of elapsed time, and does the motion mean
   something?
5. Add a golden-image test. The calendar overlap would have been caught by a
   human looking at one PNG.
