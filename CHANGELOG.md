# Changelog

Notable changes per release. The release notes on GitHub are generated from
this file, so what is written here is what people read when they download a
build.

Versions are `MAJOR.MINOR.PATCH`. While on `0.x` every release is a
prerelease: the interfaces move, and nothing here installs onto a stock
device without a capture of that device first.

## 0.2.0 — Scripting

Apps you write yourself, in [Berry](https://github.com/berry-lang/berry), on
the device.

### Added

- **Berry scripting.** A script is a class with a `draw()` method; it joins
  the carousel, draws on the panel, and can take the button. Written in the
  device's own web page under **Scripts**, or pushed over
  `/api/v1/scripts`. See [docs/scripting.md](docs/scripting.md).
- **A script editor in the device web UI.** Write, save and delete; the
  carousel keeps up by itself. Saving something that does not compile
  succeeds — the source is stored with the compiler's message beside it,
  because an editor holds work in progress and a device that refused to save
  until the code compiled would be one you could not edit on.
- **A script shop** at [/shop/](https://galadril.github.io/Stipple/shop/),
  built from `scripts/` in this repository. Every script published there is
  compiled and run by the test suite first: ninety frames on a real
  52 × 16 framebuffer, buttons pressed, then six hundred more checked for
  leaks.
- **Scripts can see the device.** `hour()`, `minute()`, `second()`,
  `weekday()`, `day()`, `month()`, `year()`, `battery()`, `charging()` —
  each with a companion (`time_known()`, `battery_known()`) that says whether
  the value means anything. A device that has never synchronised its clock
  does not have a time, and a script drawing `00:00` there has invented one.
- **`store.get` / `store.set`**, so a high score survives a power cut, and
  **`scroll_text()`**, which returns completed passes — the only way a script
  can know its message has been read.
- **`duration()`**, so a script that cycles through several readouts can ask
  for longer on screen than the carousel's default.
- `/api/v1/scripts` and `/api/v1/scripts/{id}`, documented in
  [openapi.yaml](docs/openapi.yaml).

### The sandbox

A script arrives over the network from whoever can reach the device and runs
on the thread that draws the panel, so:

- No filesystem, no dynamic loader, no bytecode loader. `open()` exists as a
  name — Berry's builtin table always has it — and calling it raises.
- No `os`, `sys`, `debug`, `introspect` or `solidify`. `debug` in particular
  would let a script remove its own instruction budget.
- An instruction budget per frame and per button handler. A script that loops
  for ever loses its frame; the panel keeps rendering and the carousel keeps
  moving.
- A script that fails is disabled rather than retried thirty times a second.
- One interpreter per script, so one cannot reach another's state. About 4 KB
  each, measured rather than estimated.

### Fixed

- A one-slot-per-frame leak in the script host. Sixteen bytes a frame is
  invisible in any short test and fatal after about two minutes on screen,
  once Berry's 4000-slot stack ran out.
- Vendored headers are no longer held to this project's warning flags, which
  broke the ARM build in a way no Windows build could show.
- The script shop was missing from the published site: the Pages workflow
  copies files by name and nobody had added a line. It now checks that every
  page the navigation links to actually exists.

### Changed

- The site uses the same palette as the device's own configuration page.
  Pure black read as a void rather than a surface. The LED panel itself stays
  literally black, which is what an unlit pixel is.

## 0.1.0

First tagged build. Framebuffer, canvas, font and text engine, scenes, the
app carousel, notifications, configuration, the `/api/v1` server, the device
web UI, MQTT, and the TC002 adapter — display, input, MCU, HTTP, Wi-Fi and
SNTP — plus the browser emulator.
