# Scripting

Stipple runs [Berry](https://github.com/berry-lang/berry) — a small, dynamically
typed language designed for microcontrollers. A script is an app: it appears in
the carousel, it draws on the panel, and it can take the button.

Scripts are written in the device's own web page, under **Scripts**, or pushed
over `/api/v1/scripts`. There are worked examples in
[`scripts/`](../scripts/), listed at
[the script shop](https://galadril.github.io/Stipple/shop/).

## The shape of a script

A script is a class with a `draw()` method, and the file ends by returning an
instance of it:

```berry
class App
  def draw()
    clear(rgb(0, 0, 0))
    text(2, 1, "hello", rgb(0, 190, 255))
  end
end

return App()
```

`draw()` is called once per frame while the app is on screen. `init()`, if you
write one, runs once when the script is saved.

Forgetting the `return` is the commonest first mistake, and the device says so
in those words rather than reporting a generic failure.

## What a script can do

### The panel

The panel is 52 × 16. Coordinates outside it are clipped, not an error.

| Call | Does |
|---|---|
| `width()`, `height()` | `52` and `16`. Use these rather than the numbers, so a script survives a different panel. |
| `clear(colour)` | Fill everything. |
| `pixel(x, y, colour)` | One pixel. |
| `line(x1, y1, x2, y2, colour)` | |
| `rect(x, y, w, h, colour)` | Outline. |
| `rect_fill(x, y, w, h, colour)` | Solid. |
| `text(x, y, string, colour)` | Draws, and returns the width it used. |
| `text_width(string)` | The width without drawing — for right-aligning. |
| `rgb(r, g, b)` | A colour. Each channel 0–255. |

The font is 5 × 7 and advances six pixels per character, so a line holds eight.
**Text that runs past pixel 51 is clipped silently.** Nothing warns you, the
panel just shows less than you wrote, so measure with `text_width()` rather
than centring by eye — this caught three of the six example scripts.

### Time

```berry
if time_known()
  text(1, 0, string.format("%02d:%02d", hour(), minute()), rgb(255, 255, 255))
else
  text(8, 5, "no time", rgb(180, 60, 60))
end
```

| Call | Does |
|---|---|
| `time_known()` | Whether the device has a wall clock it believes in. |
| `hour()`, `minute()`, `second()` | Local time. |
| `day()`, `month()`, `year()` | Local date. |
| `weekday()` | 0 is Sunday. |
| `now_ms()` | Milliseconds since the device started. Never goes backwards. |
| `elapsed_ms()` | Milliseconds since **this app** came on screen. |

### Which clock

Two, and using the wrong one is the commonest way to write a script that
looks fine and then stops.

`now_ms()` is the device's clock. Use it to **throttle** — to do something
every so often:

```berry
var t = now_ms()
if t - self.last >= 250
  self.last = t
  # ... move something
end
```

That pattern needs a clock that keeps counting while your app is off screen.
The carousel moves on and comes back; if the clock restarted, `self.last`
would hold a number from the future and the comparison would stay false for
as long as the previous showing lasted. The app would simply stop moving.
This is not hypothetical — it is what the aquarium did before `now_ms()` was
fixed.

`elapsed_ms()` is time since **your app appeared**. Use it for an animation
that should begin at the beginning each time somebody sees it, rather than
joining part-way through.

**Check `time_known()` first.** Before the device has synchronised its clock it
does not have a time, and the fields read zero. A script that draws `00:00`
there has invented it, and somebody looking at the panel has no way to tell an
invented midnight from a real one. Absence has to be visible rather than
dressed up as a plausible zero, and the tests hold published scripts to it.

### Battery

| Call | Does |
|---|---|
| `battery_known()` | Whether this device can measure one at all. |
| `battery()` | Percent. |
| `charging()` | |

Same rule, same reason: a device with no battery and a device with a flat one
both report zero, and only `battery_known()` tells them apart.

### The button

```berry
def on_button(name)
  # ...
end
```

If your script has an `on_button`, it receives the action press instead of the
carousel pausing. If it has none, the press does what it always does — a script
that swallowed the only button would be an app you could not pause.

The knob is never offered. It is how somebody moves between apps, and a script
that took it would be a script you could not leave.

### Modules

`string`, `json` and `math` are available with `import`. Everything else is
not — see the sandbox below.

## The sandbox

A script arrives over the network from whoever can reach the device, and it
runs on the thread that draws the panel. So:

- **No filesystem.** `open()` exists as a name because Berry's builtin table
  always has it, and calling it raises. There is nothing on this device a
  script should be reading; the interesting files are the Wi-Fi credentials and
  the firmware.
- **No dynamic loading**, no bytecode loader. Scripts arrive as source and are
  compiled here, where they can be rejected.
- **No `os`, `sys`, `debug`, `introspect` or `solidify`.** `debug` in
  particular would let a script remove its own instruction budget.
- **An instruction budget per frame.** A script that loops for ever loses its
  frame, not the panel. The device keeps rendering and the carousel keeps
  moving.
- **A failed script is disabled, not retried.** One that threw thirty times a
  second would fill the log and starve everything else of time. Save it again
  to re-enable it.

Each script gets its own interpreter, so one cannot reach another's state. That
costs about 4 KB each, measured rather than estimated.

Scripts are capped at 16 KB of source, and the library at 16 scripts.

## When it goes wrong

Saving a script that does not compile **succeeds**. The source is stored and
the error is attached to it, because an editor holds work in progress and a
device that refused to save until the code compiled would be one you could not
edit on.

The editor shows the compiler's message with its line number. On the panel a
broken script shows `SCRIPT ERROR` rather than going black — a black panel is
indistinguishable from a script that drew nothing, from a crashed device, and
from a dead row of LEDs.

## Compatibility with AWTRIX NG

The builtin names match what AWTRIX NG documents, so a script written against
it has a good chance of running here unchanged. That is a reimplementation from
the documented interface. No AWTRIX source was read or used, and none will be:
the project studies other products as a reference for behaviour and never as a
source of code.

Scripts written for a TC001 will need their layout redone regardless. That
panel is 32 × 8 — a quarter of the area — and a layout squeezed into it usually
wants rethinking rather than scaling.
