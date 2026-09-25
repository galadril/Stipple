# TC002 platform findings from a third-party port

- **Date:** 2026-09-16
- **Source:** public README of `github.com/sanderdw/awtrix-ng-tc002`, a TC002 port
  of AWTRIX NG
- **Status:** second-hand. Every item here is someone else's observation of their
  hardware and must be confirmed on our own device before anything depends on it.

## First contact with a real device

- **Date:** 2026-09-19
- **Source:** a TC002 on the maintainer's own network, read over HTTP. First
  hand.
- **Status:** confirmed, but only for this unit at this version.

```
mcuVer : V1.0.17
appVer : 1.1.1
```

Read from the stock firmware's own `GET /getBase`, which also returns the serial,
Wi-Fi SSID, IP and MAC. **Those four are deliberately not recorded here** — they
identify a person's device and home network, and this repository is public. Any
report shared publicly should carry the two version numbers and nothing else from
that response.

### The probe, first hand

Everything below is read from the device, not from anyone's write-up. Raw output
is in `device-probe.md` (not committed — it carries a serial, SSID and MAC).

| | |
|---|---|
| Board | `Zkswe_SSD21X_SPINOR` — SigmaStar SSD21x on SPI NOR |
| OS | ZKOS / FlyThings, build `20260527`, git `b8c8ecf` |
| CPU | **Dual-core** ARMv7 Cortex-A7 (`0xc07`) rev 5, NEON, VFPv3+VFPv4, idiv |
| **RAM** | **36 MB total**, ~16 MB available |
| **libc** | **glibc 2.30** (`/lib/libc-2.30.so`) |
| Launcher | `init.svc.zkswe`, running `/bin/zkgui` + `zkdaemon` + `zkdisplay` |
| Vendor app | `/res/lib/libzkgui.so`, **7.14 MB** |
| Shell | busybox, reduced applets — **no `grep`, `head`, `du`, or `df -h`** |

**Flash layout** (`/proc/mtd`), and this settles the ceiling for good:

| Partition | Size | Mounted | Used |
|---|---|---|---|
| BOOT0 | 320 KB | | |
| KERNEL | 1.94 MB | | |
| rootfs | 4.31 MB | `/` | 100% |
| **res** | **8 MiB** | `/res` | squashfs, 2.75 MB of it |
| config | 704 KB | `/config` | 100% |
| MISC | 256 KB | | |
| data | 8 MiB | `/data` | **5% — 7.6 MB free, writable** |
| UDISK | 8.5 MB | `/mnt/storage` | 34% |

Plus `/tmp` as tmpfs with **16 MB**, which is the tier-2 target and is genuinely
volatile.

### Three things this changes

**1. Static linking was not a precaution — it was required.**

The device has **glibc 2.30**. Our Debian 12 toolchain emits binaries needing
`GLIBC_2.34` purely for `__libc_start_main`. A dynamically linked build would
have refused to start, with an error naming a symbol rather than the cause, and
the obvious suspicion would have fallen on the adapter rather than the
toolchain. The 605 KB static binary has no such dependency and fits the
partition many times over.

**2. RAM is the real constraint, and it is tighter than the flash.**

36 MB total, ~16 MB available with the vendor stack running. Every budget in
`test_memory_budget.cpp` survives comfortably — the framebuffer is 2.5 KB, the
icon store 12 KB, the ring log ~2 KB — but this is the number that makes
blueprint §38's "no unbounded queues" a real rule rather than good manners.

**3. The provisioning gap is closable.**

`/bin/hostapd` **and** `/bin/dnsmasq` are both present, alongside
`wpa_supplicant -Dnl80211` and a `p2p0` interface. That is the entire stack a
first-boot access point and captive portal needs, already on the device.

This was the single largest open risk in the project: a flashed device that
moved house had no documented way back. It is now an engineering task rather
than an unknown. It is not *done* — nothing has been tried — but "can the
hardware even do this" is answered, and the answer is yes.

### The input layout, settled

Read from `getevent` with the controls actually pressed, in a known order. This
is the answer ADR 0016 was guessing at, and the guess was right.

**Four keys on `soc:gpio_keys_1`** (`/dev/input/event67`), which is three
buttons plus the knob's press:

| Pressed | Reports |
|---|---|
| Button 1 | `KEY_DOWN` (108) |
| Button 2 | `KEY_RIGHT` (106) |
| Button 3 | `KEY_LEFT` (105) |
| Knob press | `KEY_UP` (103) |

The codes are directional names doing duty as arbitrary identifiers — the
vendor picked them, and they carry no meaning about where the buttons sit or
what they should do. This is precisely why ADR 0016 refused to name our enum
positionally: binding `KEY_LEFT` to "previous app" would be reasoning from the
vendor's choice of constant rather than from the hardware.

**The knob is a separate device**, `knob_key` (`/dev/input/event68`):

```
ABS_X: value 1, min 0, max 255, fuzz 0, flat 0, resolution 0
```

An 8-bit absolute axis — but **what the values mean is not yet established**, and
this is worth being precise about rather than assuming.

Two captures, one mixed and one of deliberate rotation:

```
11  8  1  8  1  8  1  8  1  8  1  8  1
13 11 13 11 13 11 13 11 13 11 13 11
```

If `ABS_X` were a detent position it should step by ±1 and track the direction
turned. It does neither: the steps are 3 and 7 in the first capture and a strict
alternation between two values in the second. The declared 0–255 range may
simply be an uninitialised default rather than a real span.

So the plausible readings are still open — a position that is being sampled
coarsely, a quadrature state, or a set of gesture codes — and choosing between
them wants a slow single-direction capture with one detent per step. That is an
adapter question for Phase 7, not a blocker: `InputMapper` consumes
`RotaryLeft` / `RotaryRight` and does not care how they were derived, so the
translation is local to the device adapter and its acceleration logic is
unaffected either way.

So the count is **three buttons and a knob that presses and turns** — five
controls, six event sources. The ADR 0016 amendment that restored the third
button was correct, and `RawInput` already has the right shape.

### The platform map

Established by reading which device nodes the vendor's own processes hold open
(`/proc/<pid>/fd`), which is both exact and completely passive — the kernel is
simply asked what is already true.

| `IPlatformServices` | Device | Evidence |
|---|---|---|
| `IFrameBufferDisplay` | `/dev/spidev0.0` | Held by `zkgui`; `sstar,mspi` master at `1f222000.spi0` |
| `IInputDevice` | `/dev/input/event67`, `event68` | Held by `zkgui`; confirmed by `getevent` |
| `IAudioOutput` | `/dev/mi_ao` | Held by `zkgui`; SigmaStar audio-out, **not ALSA** |
| MCU link | `/dev/ttyS1` | Held by `zkgui`; `ms_uart` driver |
| `IStorage` | `/data` | jffs2, read-write, 7.6 MB free |
| `INetworkManager` | `wlan0` | `wpa_supplicant -Dnl80211`; `hostapd` + `dnsmasq` present |
| `IHttpServer` | port 80 | Currently the vendor's; free once `zkswe` stops |

Two corrections to earlier guesses fall out of this.

**`/dev/fb0` is not the panel.** It is a 640×480 32bpp SStar framebuffer — the
SoC's generic display output, configured by `/misc/fbdev.ini` for a screen this
device does not physically have. `zkgui` holds it open, but the 52×16 matrix is
reached over **SPI**. An adapter written against `/dev/fb0` would render
perfectly into a buffer nobody can see, which is a much more confusing failure
than not compiling.

**`zkdisplay` is a different pipeline.** It holds `/dev/mi_disp`, `/dev/mi_panel`
and `/dev/mi_sys` — the SigmaStar display stack driving that 640×480 layer. It
is not the LED path, and stopping it is not what frees the matrix.

So the display work is: open `/dev/spidev0.0`, and work out the wire format the
LED driver chips expect. The MCU on `/dev/ttyS1` is the other half — the
blueprint's warning that the MCU must be initialised before normal LED operation
now has a concrete place to happen.

`/dev/ttyS3` is Bluetooth (`hciattach -n ttyS3 aic`), not ours.

### The vendor HAL, and a correction

`/bin/zkgui` is **9.5 KB**. It is not the application — it is the EasyUI host,
and it links `libdl` plus the vendor's hardware libraries. The application is
`/res/lib/libzkgui.so` at 7.14 MB, loaded at runtime.

**This means blueprint §7.1 was right and my earlier correction was wrong.**
Reading the third-party port's documentation, I recorded that the integration
point is replacing the `zkswe` launcher rather than loading as `libzkgui.so`.
The binaries say otherwise: `zkswe` is the *service* that starts `zkgui`, which
is the *host* that loads `libzkgui.so`, which is the *app*. Replacing the app is
exactly what the blueprint described, and it is far less invasive than replacing
a launcher.

The LED panel has a documented-by-symbols API. From `libzkhw.so` (13 KB, the
low-level HAL):

```
register_ledcdev / unregister_ledcdev
ledc_set_led          set one LED
ledc_set_group        set a group
ledc_set_args         configure
ledc_get_errcode

register_spidev / unregister_spidev
spi_halfduplex_transfer
spi_get_errcode
```

And from `libzkhardware.so`, C++ helpers over it — `LedcHelper`, `SpiHelper`
(`setMode`, `setSpeed`, `setBitSeq`, `halfduplexTransfer`) and
`BrightnessHelper` (`setBrightness`, `screenOn`/`screenOff`,
`backlightOn`/`backlightOff`, `getMaxBrightness`, and a `getInstance`
singleton).

So the adapter does **not** need to reverse-engineer an SPI wire format. `ledc_*`
is the panel interface, with SPI underneath it as transport. `IFrameBufferDisplay`
maps almost directly:

| STIPPLE | Vendor |
|---|---|
| `present(framebuffer)` | `ledc_set_group` / `ledc_set_led` |
| `setBrightness(0-255)` | `BrightnessHelper::setBrightness` |
| panel on/off | `BrightnessHelper::screenOn` / `screenOff` |

### This changes how the device binary must be linked

Static linking was the right call for the smoke test and remains so: it proved
the core runs on ARMv7 with nothing to argue about.

The real device build cannot be static. Calling `ledc_set_group` means linking
`libzkhw.so`, and being loaded as `libzkgui.so` means being a shared object
inside someone else's process. Both are dynamic by nature.

That reopened the glibc question — and then closed it, in the opposite
direction to the obvious guess.

`GLIBC_2.34` came from `__libc_start_main`, which lives in the startup files
linked into an **executable**. A shared library has no such thing. Built as a
`.so` with the same Debian 12 toolchain, the requirement drops to:

```
GLIBC_2.4       CXXABI_1.3   CXXABI_1.3.9
GLIBCXX_3.4     GLIBCXX_3.4.21
```

`GLIBC_2.4` is from 2006. The device carries glibc 2.30 and
`libstdc++.so.6.0.26`, which is comfortably ahead of all of it.

That was checked properly rather than by comparing version numbers. The device's
own `libc`, `libstdc++` and `libgcc_s` were pulled, every symbol they export
collected, and every versioned symbol the `.so` imports looked up in that set:
**18 needed, 8396 available, nothing missing.** `tooling/probe/abi-check.py`
does this on demand, because "their version is higher than ours so it will be
fine" is an inference and this is a lookup.

**So the existing toolchain builds the real artifact.** No old-toolchain hunt is
needed, and the note above about Arm GNU 9.2-2019.12 describes what the
third-party port chose, not a constraint we share.

### The HAL, confirmed at runtime

`stipple_hal_probe` ran on the device and resolved every symbol:

```
libzkhw.so loaded
  register_ledcdev  unregister_ledcdev
  ledc_set_led      ledc_set_group     ledc_set_args    ledc_get_errcode
  register_spidev   unregister_spidev
  spi_halfduplex_transfer              spi_get_errcode
```

It **calls none of them** on purpose. `register_ledcdev` would claim the LED
controller while the vendor application is still driving it, and a probe that is
only safe on a broken device is not much of a probe. Resolving a symbol and
invoking it are separate questions, and only the first one can be answered
without taking the panel away from whoever currently owns it.

### …and then the hardware said no

Disassembling those functions made them look wonderfully simple:

```
ledc_set_led(handle, index, colour)   ->  write(handle[44], {colour, index}, 8)
ledc_set_group(handle, uint32*, n)    ->  write(handle[48], array, n * 4)
ledc_set_args(...)                    ->  return 0;   // a stub
```

`ledc_set_group` taking a flat array of 32-bit colours is exactly a framebuffer,
and for about ten minutes the display looked solved.

**It is not, on this device.** The strings in `libzkhw.so` show what those
handles open:

```
/sys/class/leds/num  /sys/class/leds/sctrl  /sys/class/leds/gctrl
```

`/sys/class/leds` **does not exist on the TC002**, and `lsmod` shows no LED
driver among the loaded modules — only the Wi-Fi driver, `fbdev`, and the
SigmaStar `mi_*` stack. `register_ledcdev` would open nothing and fail.

That API is real, but it targets a different ZKSWE board: one whose kernel
provides an LED class driver. The library is shared across the vendor's product
line, and this variant does not use that path.

What this device actually uses is visible in `zkgui`'s open file descriptors:
**`/dev/spidev0.0`**, with `spi0.0` bound to the generic `spidev` driver and no
LED class anywhere. The panel is driven by writing frames over raw SPI to the
LED driver chips.

So the display question is *not* narrow after all. It is: **what bytes does the
matrix expect over SPI?** — which is genuine protocol reverse-engineering, and
is the one piece of this bring-up that cannot be answered by reading
configuration.

Writing guessed bytes to unknown driver chips is not a reasonable way to find
out, so nothing was written.

Two notes on getting there, both of which cost time:

- **Executables need a different toolchain from libraries.** The bookworm image
  builds the `.so` fine, but its *executables* demand `GLIBC_2.34` for
  `__libc_start_main`, and a static binary cannot `dlopen`. A second image
  (`tooling/cross/Containerfile.bullseye`, glibc 2.31) builds dynamic
  executables that ask only for `GLIBC_2.4`. Libraries come from bookworm,
  probes from bullseye.
- **`readlink` does not exist on the device either.** `abi-check.py` used it to
  resolve library symlinks and silently fetched nothing, which made every
  `dl*` symbol look unsatisfiable — an artifact that appeared broken when the
  fetch was. `adb pull` follows symlinks by itself; the clever step was the bug.

### The display protocol, decoded

Captured by `LD_PRELOAD`-ing a shim over `zkgui` that logs `open`, `write` and
`ioctl` on the spidev descriptor and forwards every call unchanged. The vendor
app drew its ordinary boot screen while being recorded; nothing was driven by
us. Source: `firmware/tools/spi_spy/spi_spy.c`.

**SPI configuration**, printed by zkgui itself and confirmed by the ioctl
sequence (`magic='k'`, nr 1/2/3/4 — mode, LSB-first, bits-per-word, max-speed):

```
mode 0, 8 bits per word, MSB first, 10 MHz
```

**Frame format**: a single `write()` of **3072 bytes** per frame. No header, no
addressing, no chunking.

```
3072 = 1024 pixels x 3 bytes        64 columns x 16 rows, one byte per channel
offset(col, row) = (row * 64 + col) * 3
```

The panel is **52 columns wide but addressed as 64**. Columns 52–63 are padding
and were never lit in any captured frame — the rightmost pixel the vendor app
ever touched was column 50.

The proof is that the frames render as legible text when laid out that way. The
boot sequence reads `U CLOCK` and then `CONNECT`:

```
.....##..##......####..##.......................
.....##..##.....##..##.##..####...####..##..##..
.....######.....######.##.######.######.##..##..
......####.......####..##..####...####..##..##..
```

That is about as unambiguous as a layout hypothesis gets.

**`IFrameBufferDisplay::present()` is therefore**: expand the 52x16 framebuffer
into a 64x16x3 buffer, leaving the last twelve columns zero, and `write()` it.
No vendor library is needed — `libzkhw.so` turned out to be a detour, and the
real interface is plain spidev. The adapter can stay statically linked after
all.

**Channel order is still unknown.** Every lit pixel in the capture was
`255,255,255`, because the boot screen is white — white is identical under RGB,
GRB or BGR. Determining it needs either a capture while the panel shows
something coloured, or one test frame of our own with the channels deliberately
unequal.

### GPIO 35 is the latch, and without it every frame is invisible

**STIPPLE rendered on real hardware on 2026-09-20.** `demo::drawTestPattern`,
through `Canvas` and the real `Framebuffer`, on the panel. The missing piece was
not the frame format — that was already right — but a line nobody had looked at.

Writing 3072 bytes to `/dev/spidev0.0` only loads the driver chips' shift
registers. **GPIO 35 latches them onto the panel: low before the write, high
after.** Without the strobe every `write()` succeeds, returns 3072, and lights
nothing.

That single fact explains every confusing observation from the bring-up:

- A well-formed frame lit nothing once `zkgui` was stopped — no strobe.
- Frames *did* flicker through while `zkgui` was running — it was strobing the
  latch on its own schedule, and our data was whatever sat in the registers when
  it did. What looked like two writers fighting was one writer latching another's
  data.
- Suspending `zkgui` with `SIGSTOP` did not help. Its file descriptors stayed
  open and the hardware stayed initialised, but a stopped process strobes
  nothing. This was the experiment that ruled out "enable is held state" and, in
  hindsight, pointed straight at an action rather than a resource.

`/sys/class/gpio/gpio35` exists, exports cleanly and accepts `direction=out`.
Other lines are already exported by the platform: gpio2, gpio6, gpio24, gpio61.

**Why the capture never found it.** The `LD_PRELOAD` shim watched `open`,
`write`, `ioctl`, `read` and `close`, and its filter was widened to everything
under `/dev` and `/sys`. It still saw nothing, because `zkgui` does not reach the
GPIO through sysfs — it goes through `/dev/oflash`, a vendor driver carrying
ioctls with magic `'o'` (nr 3, 8, 9 seen). No amount of watching `/sys/class/gpio`
would have shown it. That is the lesson worth keeping: *absence in a capture is
evidence about the capture, not about the device.*

**Provenance.** The fact came from reading the third-party TC002 port's hardware
source after the clean-room capture had stalled — a deliberate, recorded decision
rather than a drift. What was taken is a hardware fact ("GPIO 35, low before the
write, high after"), which is not copyrightable; `Tc002Display` is our own
implementation and no code was copied. Their project is **PolyForm
Noncommercial 1.0.0**, which is incompatible with GPL-3.0-or-later, so their code
cannot be linked or vendored here regardless — see the licence note under "A
second source".

The same source gives the MCU link's baud rate as **1,500,000**, which the
capture could not show.

### The controls, read off the hardware

Measured 2026-09-20 with `firmware/tools/input_probe`, by pressing each control
and reading what arrived. Two evdev nodes, and `/proc/bus/input/devices`
describes both accurately:

```
event67  soc:gpio_keys_1   EV=3 (EV_SYN|EV_KEY)  KEY bitmap 1680 -> 103,105,106,108
event68  knob_key          EV=9 (EV_SYN|EV_ABS)  ABS=1 -> ABS_X
```

All four declared keys are wired, and the mapping is:

| Control | Code | Linux name |
|---|---|---|
| − | 108 | `KEY_DOWN` |
| middle | 105 | `KEY_LEFT` |
| + | 106 | `KEY_RIGHT` |
| knob press | 103 | `KEY_UP` |

The names are meaningless — the device tree picked four arrow keys for four
GPIOs — so nothing should ever read intent from them. Only the mapping matters.

**The third button is real.** ADR 0016 named it `KeyExtra` because one source
reported a middle button and another did not, and deliberately refused to guess
at a purpose. It exists, it sits between − and +, and it is now `KeyMiddle`
throughout. The enum shape ADR 0016 chose — two labelled buttons, a middle one,
a rotary press and two detent directions — turned out to be exactly right.

**The knob reports `ABS_X` but is not an axis. The direction is the value pair.**

A slow single-direction capture settled it:

```
clockwise  (18 detents):   8 1 8 1 8 1 8 1 ...
turnaround:                11 8 1 13
counter-cw (20 detents):   13 11 13 11 13 11 ...
```

So `{1, 8}` is clockwise and `{11, 13}` is counter-clockwise. One detent, one
event, and the value never ramps the way a real position would.

**Why it alternates at all:** evdev suppresses an `EV_ABS` event whose value has
not changed. With a single value per direction, a second detent the same way
would emit nothing. The toggle exists to keep events flowing — it is a
mechanism, not information, and an adapter that tried to read meaning from
*which* of the two values arrived would be decoding noise.

This is also why the first capture looked unreadable. It was a back-and-forth
turn, and alternating values from a reversing knob cannot distinguish "direction
code" from "wrapped position". The data was fine; the experiment was wrong.

**The kernel generates it, not `zkgui`.** The capture above ran with `zkgui`
confirmed stopped and events still arrived, so `knob_key` — virtual though it is
(`/devices/virtual/input/input1`, vendor `dead`, product `beef`) — does not
depend on the vendor application. The adapter reads `event68` and nothing else.

Implemented in `platform::tc002::Tc002Input` and verified end to end on
hardware: three buttons and the knob press produce clean `Down`/`Up` pairs,
clockwise produces `RotaryRight`, counter-clockwise `RotaryLeft`, with no
autorepeat noise and no dropped events. Unrecognised `ABS_X` values are dropped
rather than guessed — a wrong direction moves the carousel the way the user did
not turn, which is worse than a missed detent.

### The MCU protocol, and where battery comes from

There is no battery in sysfs. `/sys/class/power_supply` does not exist, nor
`/sys/class/hwmon`, nor `/sys/bus/iio/devices` — all three checked on hardware.
The MCU on `/dev/ttyS1` is the only source, exactly as the third-party port's
documentation implied.

Framing, decoded from the `zkgui` capture and then confirmed by asking the MCU
ourselves with `firmware/tools/mcu_probe`:

```
ff 55 <cmd> <len> <payload[len]> <trailer[2]>          at 1500000 baud

->  ff 55 11 00 01 65                                  ask the version
<-  ff 55 11 07 56 31 2e 30 2e 31 37 02 e7             "V1.0.17" in ASCII
<-  ff 55 03 03 5a 0c 4e 02 0e                         telemetry, pushed unprompted
<-  ff 55 02 01 01 01 58                               charge state: 01 = on the cable
```

**The trailing two bytes are a checksum**: a big-endian 16-bit sum of every
byte before them, headers included. It holds on every frame captured —
`ff+55+11+00 = 0x0165` and the query ends `01 65`; `ff+55+02+01+01 = 0x0158`
and `01 58`; `ff+55+03+03+5a+0c+43 = 0x0203` and `02 03`. It is verified rather
than skipped, because a 1.5 Mbaud link with no flow control can drop a byte and
the cost of not checking is a battery percentage assembled from whatever
followed a corrupted header.

The version handshake is what makes the rest trustworthy: the MCU answered
`V1.0.17`, which matches the version the stock firmware reports, so the port
settings and the framing are both right rather than merely plausible.

**Battery is payload byte 0 of command `0x03`.** The evidence:

- It sat at `0x5b` (91) in one session and `0x5a` (90) hours later — a slow,
  monotonic decrease.
- It stays inside 0–100, which the other two bytes do not have to.
- Nothing else on this device can report charge, and the MCU is documented as
  the place it comes from.

Bytes 1 and 2 are **cell voltage in millivolts, big-endian**. They had been
written off here as an undecoded fast-moving value, on the grounds that nothing
drifts that quickly; a cell under a varying load does. `0c 43` is 3139 mV and
`0c 4e` is 3150 mV — one cell wobbling by a few millivolts, not a second sensor.

### Command `0x02` is the charge state

Dismissed in an earlier revision of this document as "a flag, always 01". It is
always 01 when every capture is taken over USB, which every capture was.

A 75-second capture taken with the cable deliberately pulled settles it. The
payload held `01`, went to `00` within a second of the cable coming out, stayed
there for twelve seconds, and returned to `01` on reconnect:

| Time | `0x02` | Voltage | Percent |
|---|---|---|---|
| 0–43.3 s | `01` | 3158–3163 mV | 90 |
| 43.3 s | `00` — unplugged | falls to 3111–3122 mV | sags to 88 |
| 55.8 s | `01` — replugged | 3158–3163 mV | back to 90 |

This also explains a battery app that looked broken. The MCU's percentage is
voltage-derived, so it sags under load and recovers on the cable — a user
pulling the cable sees the number drop several points and putting it back sees
it climb, with nothing on screen explaining why. The charge flag is the
explanation, and the battery app now draws it.

### The microphone has to be asked for

Command `0x01` carries the level: a big-endian 16-bit amplitude, roughly 22 Hz,
a few hundred in a quiet room. **It is sent only after the microphone is
switched on**, and the switch is command `0x04`:

```
->  ff 55 04 01 01 01 5a       microphone on
->  ff 55 04 01 00 01 59       microphone off
<-  ff 55 01 02 01 a6 01 fe    level: 0x01a6 = 422
```

Both values of the switch were captured going out of the vendor application as
its visualiser appeared and was navigated away from. The bytes STIPPLE sends are
that capture verbatim, and `Tc002Mcu` sends the off frame when it closes the
port — the enable outlives the process, and leaving it on would mean a device
that had once run STIPPLE kept streaming audio to whatever ran next.

**The switch is sticky.** The MCU keeps streaming until something turns it off
or the device loses power. That is the entire history of this feature: the
vendor application enabled it, STIPPLE inherited a microphone it had never asked
for, the visualiser worked for a while, and a reboot took it away with nothing
in the code having changed.

#### How this was nearly recorded as a hardware limitation

Worth keeping, because the reasoning looked sound at every step and the
conclusion was wrong.

Three captures were taken looking for audio: a 19-second `LD_PRELOAD` trace of
the vendor application, a 75-second listen with someone deliberately making
noise at the device, and the vendor application left alone on its clock face for
35 seconds. All three contained command `0x02` and command `0x03` and nothing
else, and in none of them did the vendor application write anything to the link
but the version query.

The conclusion drawn was that `0x01` had never been observed, that the
`kMicLevel` constant was belief rather than evidence, and that the device should
report it could not hear. That was written into this document and into the code.

It was contradicted by two things already in the repository. The commit that
fixed the visualiser's auto-gain describes *"a finger snap raised the window and
every column already on screen shrank at the same instant"* — an observation
nobody can make without a live microphone. And the person who owns the device
said plainly that it had worked.

Every capture was true. Each was taken with something on screen that did not
want audio, so what they measured was the absence of a reason to stream, not the
absence of a microphone. A fourth capture, taken with the vendor visualiser
actually displayed, had 451 audio frames in twenty seconds and the enable
command in plain sight.

The lesson is narrow and worth stating: **a capture proves what was happening
while it ran.** Three of them agreeing proves only that the same thing was not
happening three times. When a capture disagrees with someone who watched the
device work, the capture is not the witness to trust.

A practical footnote, because it nearly cost a fourth wrong conclusion: the
`LD_PRELOAD` shim caps itself at 600 records to protect a 36 MB device from
filling tmpfs. Narrowed to the MCU link with `STIPPLE_SPY_ONLY=ttyS`, that cap is
reached in nine minutes and the log simply stops — which looked exactly like a
device that had nothing more to say. `STIPPLE_SPY_MAX` raises it.


Implemented as `platform::tc002::Tc002Mcu`, which reads only. The MCU also
drives the panel's power rails, and sending commands whose meaning is a guess is
not worth a clock. Values outside 0–100 are discarded rather than clamped:
clamping 200 to 100 would invent a full battery.

Confirmed end to end — `GET /api/v1/device` on the running firmware returns
`"battery":{"known":true,"percent":90}`.

**The one check still owed:** watch the value fall while running on battery with
the charger out. Everything above is consistent with a discharging battery, but
a deliberate discharge is what would make it certain.

### Audio is reachable, and not the way it looked

`IAudioOutput` was the least-understood capability: no ALSA, no `/dev/snd`, and
`zkgui` driving `/dev/mi_ao` through proprietary SigmaStar ioctls (`magic='i'`,
903 of them in a ten-second capture). Decoding those means reconstructing
`MI_AO_Attr_t` and friends without headers, which is exactly the kind of
guessing this project refuses.

It turns out not to be necessary. Two vendor libraries sit above those ioctls:

**`/lib/libmi_ao.so`** — the SigmaStar MI audio API, needing only `libc.so.6`:
`MI_AO_Init`, `MI_AO_SetPubAttr`, `MI_AO_Enable`, `MI_AO_EnableChn`,
`MI_AO_SendFrame`, `MI_AO_SetVolume`, `MI_AO_SetMute`. Usable, but
`SetPubAttr` takes a struct whose layout we would still be inferring.

**`/lib/libzkmedia.so`** — the vendor's own C++ wrapper, and the useful one:

```
media::SoundDevice::init(unsigned int, unsigned int)      // rate, channels
media::SoundDevice::output(unsigned char*, unsigned int)  // raw PCM
media::SoundDevice::setVolume(float)
media::SoundDevice::deinit(bool)

media::ZKAudioPlayer::play(const char*)                   // a file, by path
media::ZKAudioPlayer::stop / pause / resume / seekTo
media::ZKAudioPlayer::getDuration / getCurrentPosition
media::ZKAudioPlayer::setVolume(float)
```

Every argument type is legible from the mangled names, and there is no opaque
struct anywhere in it. That maps onto `IAudioOutput` almost one to one:
`playTone` synthesises PCM and calls `output`, `playSound` calls `play`,
`setVolume` calls `setVolume`.

**Two things stand between this and working audio, and both are decisions
rather than unknowns.**

*The firmware is statically linked, and a static binary cannot `dlopen`.* That
was the right call for bring-up — it removed the entire `GLIBC_2.34` question
(see "This changes how the device binary must be linked"). Using either vendor
library means a dynamically linked `stipple_device`, built with the bullseye
toolchain whose executables ask only for `GLIBC_2.4`. `stipple_hal_probe`
already proves that combination loads and runs on this device, so the path is
known to work; it is the trade that needs deciding, not the mechanism.

*Linking a GPL-3.0-or-later program against proprietary vendor libraries.*
GPLv3 §1 excludes "System Libraries" — components that come with the operating
system the program runs on. `libzkmedia.so` and `libmi_ao.so` ship in this
device's firmware image and are exactly that kind of platform component, which
is the ordinary reading. It still deserves to be written down and decided
deliberately rather than assumed, because it is the first time STIPPLE would
link against anything it did not write.

*A smaller third thing:* `SoundDevice` is a C++ class, so using it through
`dlsym` means allocating storage for an object whose size we do not know.
Over-allocating is the usual trick and it usually works; it is also precisely
the sort of "usually works" this project has been avoiding. `ZKAudioPlayer::play`
may sidestep it if a factory function can be reached instead of a constructor.

### Things that need design work

- **The knob is an absolute axis, not detents.** `/proc/bus/input/devices` shows
  two devices: `soc:gpio_keys_1` (a key bitmap) and `knob_key`, which reports
  `EV=9` / `ABS=1` — that is `EV_ABS` on `ABS_X`. `InputMapper` assumes discrete
  left/right ticks, so the device adapter has to convert position changes into
  detents. The mapper's acceleration logic is unaffected; only the source
  changes.
- **There is no ALSA.** *(Superseded — see "Audio is reachable, and not the way
  it looked". The conclusion below is correct about ALSA and wrong about the
  consequence: `libzkmedia.so` exposes a usable PCM and file-playback API above
  those ioctls.)* No `/dev/snd`, no `/proc/asound/cards`. Audio is not
  reachable through any standard Linux interface, which matches the report that
  the speaker is driven through the vendor SDK's AudioManager. `IAudioOutput`
  has no obvious binding, and this is now the least-understood capability.
- **The display path is not yet obvious.** `/dev/fb0` exists, and so does
  `/dev/spidev0.0` and a `/bin/test_fb`. Whether the panel is reached through
  the framebuffer, through SPI directly, or only through `zkdisplay` is the next
  thing to establish.
- **`/res` is a read-only squashfs**, 2.75 MB of an 8 MiB partition. Replacing
  `libzkgui.so` means rebuilding that image, not copying a file into place.
  Writable persistent storage is `/data` with 7.6 MB free — that is where
  `IStorage` belongs, not `/res`.

### Why the versions matter more than they look

They are **exactly** the pair the third-party port states it was validated
against: "Confirmed on TC002 stock app 1.1.1 / MCU V1.0.17."

That changes how much weight the rest of this document can carry. Everything
recorded below was second-hand observation of *some* TC002; it is now
second-hand observation of a device at the same stock-app and MCU version as the
one in front of us. The packaging format, the 8 MiB res ceiling, the `zkswe`
launcher and the `/tmp` trial path are still unverified here — but they are no
longer being read across an unknown version gap, which was the largest reason to
distrust them.

It does not make them true. It makes them worth testing first.

### What else the stock firmware already tells us

- **ADB is open on 5555 out of the box.** No unlocking, no developer mode, no
  gesture. The deployment path ADR 0008 assumes is simply available.
- **A web server runs on port 80**, serving `/settings/*` and a small API:
  `/getBase`, `/checkUpdate`, `/update`, `/resetConfig`. Only `/getBase` was
  called; the other three mutate or phone home.
- **Stock settings are**: `brightnessLevel`, `brightnessLow/Mid/High`, `volume`,
  `carouselSpeed`, `scrollSpeed`, `timezone`, `dateFormat`, `showWeek`,
  `weekStart`, `lowBatteryAutoSleep`. Brightness is three presets plus a level,
  not one slider, and `lowBatteryAutoSleep` confirms a battery worth reading.
- **Wi-Fi can be reconfigured over HTTP.** The info page carries the note 如需修改
  WiFi，请进入配置页提交新的 SSID 和密码 — "to change Wi-Fi, go to the config page
  and submit a new SSID and password."

  That last one matters for the provisioning gap. It does not close it — this is
  the *stock* web server, which STIPPLE replaces — but it proves the platform
  exposes Wi-Fi reconfiguration to a userspace HTTP handler. Whatever mechanism
  that page uses is one STIPPLE can use too, and it is a far better answer than
  hoping for AP mode. Finding out what it calls is now a probe question.

## A second source

- **Date:** 2026-09-18
- **Source:** a TC002 owner's write-up of using the **stock** firmware with Home
  Assistant ([r/homeassistant](https://www.reddit.com/r/homeassistant/comments/1w54oi0/ulanzi_tc002/))
- **Status:** second-hand, and an owner's summary rather than a teardown — but
  independent of the port below, which makes the points where they agree much
  stronger and the points where they disagree worth taking seriously.

Their hardware list: 52×16 RGB matrix, "Linux-based Z21 platform", Wi-Fi **and
BLE**, **one knob with press and rotate**, **three separate buttons**, a speaker
with MP3 playback, **microphone volume reporting**, USB-C, and a
**recovery/reset option**.

### This contradicts ADR 0016, and ADR 0016 is the one that loses

The port's documentation describes a knob and two buttons marked − and +. This
owner counts a knob **and three buttons**. Both cannot be right about the count.

The likeliest reconciliation is that both are accurate about different things:
the port's README describes what *their firmware does with the controls*, not an
inventory of them, and a firmware that uses two of three buttons would read
exactly like that.

Where the two sources disagree, the safer assumption wins, and here that is
clearly the higher count:

- Model three buttons, hardware has two → one enum value never fires. Invisible.
- Model two, hardware has three → a physical button on a shipped device does
  nothing, and the owner reasonably concludes the firmware is broken.

So the input model carries three buttons plus the knob again. See the amendment
in [ADR 0016](../adr/0016-tc002-input-layout.md).

### What else it adds

- **BLE exists.** Nothing in the blueprint or the port's docs mentioned it. Not
  useful yet, but it is another provisioning route worth remembering given that
  first-time Wi-Fi setup is still unsolved.
- **A recovery/reset option exists**, alongside USB-C. This is the first
  independent support for ADR 0008's assumption that a hardware recovery path
  exists at all. What it actually restores is still unverified, and the installer
  gates do not relax until it is.
- **A microphone reports volume**, matching the blueprint's `IMicrophone`.
- **"Z21 platform"** — we have recorded the SoC as SigmaStar SSD21x. Whether Z21
  is a different name for the same thing, the vendor's board name, or a
  transcription of SSD21x is unresolved. The probe should settle it from
  `/proc/cpuinfo` rather than anyone guessing.

### And it explains why this project exists

On stock firmware they got MQTT and Home Assistant discovery working, but only
"very basic entities": a connect-state binary sensor and a device-topic sensor.
Drawing primitives and base64 PNG/GIF images work. **Notifications, text
payloads, drawing text and any sound over MQTT do not.** Their conclusion was
that the stock TC002 needs "a local bridge or custom app to become really
useful", and that a TC001 running AWTRIX is still the better Home Assistant
device today.

That is a fair description of the gap STIPPLE is aimed at, and it is worth
keeping in view: text, notifications and a predictable MQTT surface are the
things an owner actually misses. All three already work in the emulator.

One practical detail: the stock firmware's own MQTT namespace looks like
`ulanzi2_a435/custom/{app}` — `{prefix}_{last4}/custom/{app}`. Ours is
`stipple/{deviceId}/...`, so the two cannot collide, and a device that has been
flashed will simply stop answering on the old topics.

## Why this document exists

Blueprint §46 lists hardware questions we deliberately refused to guess at, and
Stage 0 asks that reversed platform behaviour be written down rather than left in
someone's head. A working third-party port is the strongest evidence available
short of owning the device, so it is worth recording what it demonstrates —
and, just as importantly, what it does not.

**Nothing here is copied from that project.** ADR 0001 permits studying other
products as a reference and forbids taking their source; these are facts about
Ulanzi's hardware, not anyone's implementation. No code, markup or assets have
been read into this repository.

The line held while gathering this: **their documentation was read, their source
was not.** A README describing which buttons the hardware has is a fact about a
Ulanzi product. Their implementation of how to read those buttons is their work,
and reading it would compromise the independence this project is built on — so it
was left alone, and should stay that way when someone revisits this at Phase 7.

## What it settles

### The build path works (§46 Q13)

Cross-compiled for ARMv7 with the Arm GNU toolchain (they pin 9.2-2019.12),
driven by CMake, producing stripped binaries. Headless, no FlyThings IDE.

That is the approach ADR 0011 assumed and `docs/development/toolchain.md`
describes. It is no longer an assumption.

### An HTTP server runs on the device

Their build "serves the web UI on port 80". So a listening socket inside the
replaced application is possible.

This matters directly for `platform::IHttpServer`, which today returns null
everywhere with a comment saying the transport is unknown. It is not unknown any
more — only unwritten.

### Frame rate has more headroom than assumed

They report "about 42 FPS" for full 52×16 output — roughly a 24 ms frame.

Blueprint §9.4 warns against intervals below ~15 ms, and our `FrameScheduler`
takes 15 ms as the floor with a 30 FPS target. Both numbers look comfortable
rather than optimistic. Worth re-measuring ourselves, since 42 FPS may be their
achieved rate rather than a ceiling.

### Persistent install is a real, documented mechanism (§46 Q5)

An `update.img` is built against the stock image and validated on a "platform
header, CRC, payload MD5, filesystem bounds and 8 MiB res partition limit",
copied to `/tmp` alongside a helper binary, and installed by running the helper,
which "writes flash and reboots". A separate `restore-stock.img` returns the
device to the original filesystem.

So the format is structured and checkable, there is a real size ceiling
(**8 MiB** for the res partition), and a restore path exists. Blueprint §27.4's
list of safety gates before offering persistent flashing to users still applies
in full.

### The temporary path is even more temporary than we assumed

Their trial mode stops the `zkswe` launcher service, runs from an isolated `/tmp`
data directory, restarts the stock service afterwards — and is time-limited to
about 180 seconds.

Our `docs/development/toolchain.md` describes `/tmp` sideloading as the default
development mode. A three-minute window changes what that loop feels like, and
is worth confirming before Phase 7 plans around it.

### The replacement target is the launcher, not a library

They stop and replace the **`zkswe` launcher service**.

Blueprint §7.1 describes STIPPLE loading "as `libzkgui.so` inside that host". The
evidence points at replacing the launcher process rather than injecting a library
into it. If that holds, the §53 boundary is unaffected — our platform adapter
still sits underneath everything — but the Phase 7 entry point is a `main()`
rather than a library export. That is a smaller change than it sounds, and
`ApplicationHost::tick()` was already shaped for a caller-owned loop.

### Input is a knob plus two buttons

Described behaviour: "Turn the knob to move between apps", "Tap −/+ to lower/raise
speaker volume by 5 percentage points on release", "Hold −/+ for 0.7 seconds to
lower/raise brightness by 10."

So the physical layout is a **rotary encoder and two labelled −/+ buttons**, and
the natural mapping puts navigation on the knob, volume on a tap and brightness
on a hold.

`InputMapper` already models a rotary with acceleration, which is the part that
would have been painful to retrofit. Its defaults assumed `left / middle / right
/ rotary-press` — three buttons plus the knob — and bound left and right to
previous/next app, duplicating what the knob does.

**Acted on.** `RawInput` is now `KeyMinus / KeyPlus / RotaryPress / RotaryLeft /
RotaryRight`, with navigation on the knob, volume on a tap and brightness on a
hold. See [ADR 0016](../adr/0016-tc002-input-layout.md), which records the
reasoning, what it costs if this is wrong, and exactly which test should fail
first. Volume was implemented at the same time, because a default binding to an
unimplemented action is just a dead button.

This is the one finding in this document that has been built on rather than
merely recorded, so it carries the most risk if the source is wrong.

### The platform is Android-flavoured, not plain Linux init

Services are controlled with `setprop ctl.start zkswe` — Android's property
service, not sysvinit or systemd. Anything Phase 7 writes to start, stop or
supervise the application should expect that model.

### TLS is available, via a bundled OpenSSL

They ship "certificate-verified HTTPS with the bundled OpenSSL 3.5.8". So the
device can do real TLS, at the cost of carrying the library.

This does not threaten ADR 0012. TLS belongs to the platform adapter, below the
§53 boundary; `stipple_core` stays dependency-free either way. It does mean the
8 MiB res ceiling has to be budgeted against a bundled crypto library if we ever
want HTTPS.

### Audio, mDNS and NTP are all real

Speaker playback (including MP3 and HTTP streaming), volume control, mDNS
discovery and NTP sync are all listed as working. `IAudioOutput` has something to
bind to, and the splash's "show the IP address" fallback could eventually be
joined by an mDNS name.

### There is a concrete compatibility baseline

"Confirmed on TC002 stock app 1.1.1 / MCU V1.0.17."

The first real data point for §46 Q1/Q2. It does not tell us how much variation
exists across units, but it does mean stock-app and MCU versions are worth
recording whenever we test — a report without them is not reproducible.

## What it settles unhappily

### The TC002 has no ambient light sensor

Quoted: "the TC002 has fixed wiring and no light, temperature or humidity
sensor; those GPIO controls are hidden."

**This invalidates a setting we already shipped.** `config.display.autoBrightness`
came from surveying what a pixel-clock settings page usually offers. On this
hardware nothing could ever honour it, so it is a switch that does nothing —
precisely the kind of quietly-lying control this project keeps refusing to build
elsewhere. It has been removed.

If a future device does have a sensor, the honest shape is an optional platform
capability that reports its presence, exactly as ADR 0013 handles audio and
network — not a config flag that hopes.

Battery percentage *is* available from the MCU, and the microphone is documented
by Ulanzi, so a sensor interface is still worth having eventually. It should
report what exists rather than assume a fixed set.

### There is a display quirk nobody has explained

They report flicker on specific dim greens — `#004200` and `#004B00` — at any
brightness, cause unknown, worked around by nudging the colour.

Recorded because it would otherwise cost days: a renderer producing those exact
values would look broken through no fault of its own. If we see it, this is the
first thing to check rather than the last.

## What it does not settle

- **First-time Wi-Fi provisioning.** No AP mode, hotspot or captive portal is
  documented. Their instructions are explicit that you "connect the TC002 to
  Wi-Fi using its stock app and find its IP address" *before* installing
  anything — so the stock application does the provisioning and the replacement
  inherits a configured network.

  That is a real gap rather than a solved problem: it means a device that is
  flashed and then moved to a new network has no documented way back. Our splash
  showing the IP address helps only once the device is already on a network.
  Whether the platform can bring up an access point at all is still unknown, and
  it is the single most useful thing to test when hardware arrives.
- **RAM.** No figure given. The 8 MiB res limit is flash, not memory. Every
  budget in `test_memory_budget.cpp` — the 12 KB icon store, the 128 KB
  worst-case app storage — is still unvalidated.
- **The MCU protocol.** Its version matters (V1.0.17 above) and the blueprint
  says it must be initialised before normal LED operation, but nothing here
  describes the link itself.
- **Which hardware revisions this applies to** (§46 Q1, Q2). One device, one
  stock-app version, one MCU version.

## Update: their installer tooling (2026-09-17)

The same project has since published an end-to-end install path — a one-line
`curl | sh` installer, a RAM-only trial runner, and a documented recovery
procedure. Same provenance rule as above: **their README was read, their scripts
were not.** What follows is the workflow they describe, because the *sequence* is
the reusable insight; their implementation of it is theirs.

Worth saying plainly, because it is easy to assume otherwise: this tooling is
not AWTRIX NG's. It is TC002-specific work by that project's author, so ADR 0001
does not speak to it. What does speak to it is licensing — GitHub reports the
repository's licence as `NOASSERTION`, meaning no recognised licence could be
identified. Compatibility with our GPL-3.0-or-later cannot be established from
that, so their code stays out regardless of ADR 0001, and §42's dependency
register would have nothing valid to record. Reimplementing a documented
workflow is unaffected.

### The shape of their install path

Three tiers, escalating in permanence:

1. **Trial** — ADB-push a binary, stop the launcher, run from an isolated `/tmp`
   data directory, web UI on port **18081**, killed after ~180 s. Confirms the
   180-second figure already recorded above, and that the trial deliberately
   uses a *different* port from the installed app's 80.
2. **Install** — read the device's application partition, verify it against
   supported stock versions, build the image locally, run a preflight **on the
   clock**, require the operator to type `flash`, then write in place (~3 min).
3. **Restore** — the same helper run against a `restore-stock.img`.

Their preflight checks, which is the part worth copying as a *list*: platform
header, CRC, payload MD5, filesystem bounds, the 8 MiB res limit, vendor files
against recorded fingerprints, and partition geometry. Unrecognised stock
firmware is refused unless explicitly forced.

### Three facts that change our plans

**There is no A/B partition.** Quoted: installing "writes the `res` flash
partition in place. There is no A/B copy on the clock." A power cut mid-write
leaves a partition needing recovery. Blueprint §27.4 already demands rollback be
verified before persistent flashing ships — this says rollback cannot be an
A/B swap, so it has to be the restore image plus the recovery path below.

**Recovery is a hardware gesture, and it has a floor.** Holding the knob while
powering on launches the vendor application, which brings back the stock UI,
updater and ADB. Independently, three crashes in a row at start-up triggers a
launcher fallback on the fourth boot. Both matter to us directly: the second one
means *our* application must not crash-loop silently, because the platform will
quietly stop running it — and a STIPPLE that has been fallen back from looks
identical to one that was never installed. Phase 7 should expect to surface that
state rather than let the user guess.

They are explicit that below this there is nothing validated: if neither the
vendor application nor ADB returns, recovery needs the stock bootloader's update
path or a serial connection, untested. So the recovery story has a documented
floor, not a guaranteed one.

**The images are built from the user's own device, not downloaded.** Their
generator takes a stock image and a live res dump from a `device-private/`
directory that is kept out of the repository, and the install path reads the
clock's own partition. Nothing vendor-derived is redistributed.

That is the answer to §46 Q5's redistribution half, and it is a constraint on our
release process rather than an implementation detail: **STIPPLE must never publish
a `restore-stock.img` or any vendor-derived blob as a release asset.** The
restore image is something the installer *produces locally* from the device in
front of it. A release can ship our payload and the tool; it cannot ship
Ulanzi's filesystem.

### What this does not give us

A payload. Their installer's hard part is validating and writing the `res`
partition; the binary it writes is their application. We have no ARM preset, no
TC002 platform adapter and no device build, so there is nothing for an
equivalent installer to carry yet. The ordering stands: Phase 7 produces a
binary, and only then is an installer meaningful.

It also still does not settle first-time Wi-Fi provisioning — the gap recorded
above is untouched by any of this. Their install path assumes a device already
on the network, and a flashed device moved to a new network remains without a
documented way back.

## Consequences taken

1. `display.autoBrightness` removed from configuration and the API.
2. `platform::IHttpServer` keeps its shape; only the implementation is missing,
   and it is now known to be possible.
3. Phase 7 should expect to provide a `main()` replacing the launcher rather than
   a `libzkgui.so` export. No change to the §53 boundary.
4. The 180-second trial limit, the 8 MiB res ceiling and the green-flicker quirk
   are recorded here so Phase 7 does not rediscover them.
5. `RawInput` and the default bindings were rewritten for a knob plus two
   labelled buttons (ADR 0016), and `Action::VolumeUp` / `VolumeDown` were
   implemented so those bindings do something.
6. Phase 7 test reports should record the stock-app and MCU versions, since a
   report without them cannot be compared against anything.
7. Releases publish only what exists. `.github/workflows/release.yml` packages
   the emulator, states in the release notes that no installable firmware
   exists, and marks every 0.x tag a prerelease. Device artifacts join that
   workflow in Phase 7.
8. No vendor-derived blob ever becomes a release asset. The restore image is
   generated locally from the device being installed onto.
9. Crash-loop visibility is now a Phase 7 requirement, not a nicety: the
   platform falls back to the vendor launcher after three failed start-ups, and
   that state must be reported rather than left looking like a failed install.

## The Wi-Fi control interface

`init.rc` starts the supplicant as:

```
wpa_supplicant -iwlan0 -Dnl80211 -c/data/misc/wifi/wpa_supplicant.conf                -C/dev/socket/ -e/data/misc/wifi/entropy.bin
```

so its **control socket is /dev/socket/wlan0**, a UNIX datagram socket taking
plain text commands. There is no `wpa_cli` binary on the device, but none is
needed: the socket is the whole interface, and a client binds a socket of its
own — the daemon replies to the address it was sent from.

`STATUS` returns `wpa_state`, `ssid` and `ip_address` among others.
`SCAN_RESULTS` returns tab-separated rows of bssid, frequency, signal level,
flags and SSID, after a header line naming those columns.

**This matters for provisioning.** The alternative was editing
`/data/misc/wifi/wpa_supplicant.conf` by hand — a persistent file, on the only
path back to the device. The daemon owns that file, knows how to write it, and
`ADD_NETWORK` / `SET_NETWORK` / `SAVE_CONFIG` let it do so. ADR 0018 takes that
route for exactly that reason.

`/data/misc/wifi/hostapd.conf` also exists, already configured with a WPA2 PSK
— the vendor's own fallback access point. A hotspot mode has a working
configuration to start from rather than one to invent.

`/data/misc/wifi/` is persistent. `/tmp` is not, which is where a client socket
belongs.

## There is no DHCP client on this device

Found by testing the hotspot, which is the only way it was going to be found.

The complete contents of `/bin`:

```
adbd busybox cat chmod chown cp date df dnsmasq echo fsync getevent getprop
hostapd kill ln logcat logd ls mkdir mknod mksh mount mv ping ps pwd reboot
rm rmdir setprop sh ssd_init.sh sync test_fb touch umount vold wpa_supplicant
zkdaemon zkdisplay zkgui
```

No `udhcpc`, no `dhcpcd`, no `dhclient`. The busybox build has no applets at
all — `busybox --list` answers "applet not found" — so `/sbin/ifconfig` and the
handful of other symlinks are the whole of it. `init.rc` creates
`/data/misc/dhcp` and nothing ever writes there.

**The vendor application obtains the lease itself.** `wpa_supplicant` is an
init service and only ever associates; the address appears when `zkgui` runs
and nothing else on the device is capable of asking for one.

Two consequences, and the second is the serious one.

**A hotspot cannot simply hand the radio back.** Stopping `hostapd` and
restarting `wpa_supplicant` re-associates and leaves the interface with no
address, which is what happened on the first live test: the access point
disappeared, the station came back, and the device was unreachable until it was
power-cycled. Restoring the network needs something to ask for an address.

**STIPPLE does not hold its own lease.** It has been running on addresses
obtained by the vendor application before it started — every session so far
began with `setprop ctl.stop zkswe` on a device that was already online. The
address stays configured because nothing removes it, but nothing renews it
either, so a STIPPLE device left alone will lose its network when the lease
expires. Nobody has seen it because no unit has run for a full lease period
without being restarted.

So a DHCP client is not a hotspot detail. It is a thing STIPPLE needs in order
to be the application on this device at all, and it has to be written: there is
nothing here to call.

### Confirmed while writing one

STIPPLE now obtains and renews its own lease, and the live run settled three
things that were guesses beforehand.

**`AF_PACKET` works on this kernel.** A `SOCK_DGRAM` packet socket binds and
receives; `/proc/net/packet` shows it with proto `0800` on `wlan0`, alongside
`wpa_supplicant`'s own `888e` EAPOL socket. So a client here can bootstrap
from no address at all, which is what first boot needs.

**The lease this device is issued is 86400 seconds.** That is the number
behind the whole problem: a STIPPLE device left alone loses its network after a
day.

**The router honours option 50.** Asking to keep the address already on the
interface returned the same address, so taking the lease over from the vendor
application is invisible to everything else on the network.

`/sbin/ifconfig` is a busybox symlink and there is no `/bin/ifconfig`, but
none of it is needed: `SIOCSIFADDR`, `SIOCSIFNETMASK` and `SIOCADDRT` all
work directly, which is better than parsing a tool's output anyway.

## The flash layout, and why nothing here can write it yet

```
mtd0  0x00050000  BOOT0        mtd4  0x000b0000  config   (squashfs, ro)
mtd1  0x001f0000  KERNEL       mtd5  0x00040000  MISC
mtd2  0x00450000  rootfs (squashfs, ro)   mtd6  0x00800000  data (jffs2, rw)
mtd3  0x00800000  res    (squashfs, ro)   mtd7  0x00880000  UDISK (vfat)
```

**`/data` is the only writable persistent filesystem** — 8 MiB of jffs2 on
mtd6, mounted rw. `/`, `/res` and `/config` are all read-only squashfs.

**Nothing in init runs anything from `/data`.** `/etc/init.rc` lives on the
read-only rootfs, and every service it declares points at `/bin` or `/res`.
So a program in `/data` cannot be started at boot, which is why STIPPLE is
still a thing you run rather than a thing the device runs — and why
persistence needs a write to mtd2 or mtd3.

One line of init.rc is worth keeping in mind for later:

```
export LD_LIBRARY_PATH /tmp:/res/lib:/lib
```

`/tmp` comes *first*, so a library dropped there shadows the vendor's own.
That is what makes the volatile trial mode work at all, and it is tmpfs, so
it is also why that mode cannot be made to persist.

### The device can be flashed; it has no tool that does

`/dev/mtd/mtd0` through `mtd7` exist as character devices, mode `crw-------`
and owned by root, which STIPPLE runs as. The read-only aliases `mtdNro` are
there too, which is what a restore-image capture should read from.

So the missing piece is a program, not a capability: `MEMGETINFO`, then
`MEMERASE` per eraseblock, then write, then read back and compare. On the
order of a hundred and fifty lines.

That resolves half of what [ADR 0008](../adr/0008-installer-helper.md) is
waiting on. It does **not** resolve the other half — the ADR requires a
restore path that has been *demonstrated*, not one that ought to work, and
demonstrating it means writing the tool and then using it to put a captured
image back on a device that has been deliberately broken. The gates stand.

## The device already knows how to flash itself

Found while working out how STIPPLE could persist, and it made writing a
flasher unnecessary. All of this is first-hand: read off a real unit and
proven by rebuilding a factory image byte for byte.

**It is NOR flash.** `/sys/class/mtd/mtd3/type` says `nor`, `writesize` is 1
and `oobsize` is 0. No OOB, no ECC, and **no bad blocks** — erase then write,
byte-addressable. Every partition reports the same.

**There is a vendor update path, and it is good.** `/mnt/storage/update.img`
sits on the device's own USB volume — the vfat partition that appears as mass
storage when it is plugged into a computer. The loader checks a header CRC32
and a payload MD5 before writing, and writes only the `res` partition
directly; other partitions go through a u-boot handoff.

**Recovery is a physical button.** Holding reset during power-up reflashes
from `/mnt/storage`, needing no network, no computer and no shell.

### The update.img container

```
0x000  magic  "ZKSWEV1.0-180127"    (the first 9 bytes are what is checked)
0x010  prefix length 0x30, entry count 1
0x014  partition index               3 = res
0x018  payload offset                0x23c (572) - the header size
0x01c  payload length                padded to 4 KiB
0x020  the real first 16 bytes of the filesystem image, relocated here
0x035  device code 0xaa550606        Zkswe_SSD21X_SPINOR, and unaligned
0x238  CRC32 over header[0:0x238]
payload[0:16]                        MD5 of the image with its first 16 bytes restored
```

The two sixteen-byte swaps are the only unusual part: the filesystem's
opening bytes move into the header, and the MD5 takes their place at the
start of the payload.

Confirmed by extracting the factory image and repacking it — the result is
byte-identical. `tooling/imgtool/` does both.

Large parts of the header from 0x60 onwards are not understood. There is a
table of some kind with a regular four-byte cadence. Nothing here invents it:
the tooling copies a known-good header and edits the five fields that must
change.

### The shipped recovery image is not the firmware that is running

On the unit this was written against:

```
installed res   squashfs, bytes_used 2 787 758
shipped udisk   payload              2 781 184     they differ
```

**Holding the reset button on this device installs an older image than the
one it has.** The third-party TC002 documentation reports the same on their
unit, so it is not a one-off.

That is the evidence behind [ADR 0008](../adr/0008-installer-helper.md)'s
insistence that a restore image be captured from the unit in front of you.
`tooling/imgtool/capture.py` does it, reading through the kernel's read-only
alias `/dev/mtd/mtd3ro` and verifying the result against the capture.

### USB is a recovery path after all

`/sys/bus/platform/devices/soc:usbotg/otg_role` reads `usb_host` and can be
set to `usb_device`, which brings up the ADB gadget over the cable — root
shell with no network involved.

CLAUDE.md says USB-C on this device is mass storage and not a flashing path.
That is true of its **default role** and not of the port, and the distinction
matters: it means there is a way into a device whose Wi-Fi is broken, which
is the failure this project keeps running into.

## How the vendor application is actually loaded

Blueprint §7.1 says `/bin/zkgui` "loads the application from
`/res/lib/libzkgui.so` at runtime". The conclusion is right and the mechanism
is not, and the difference matters to anyone trying to replace it.

**`zkgui` does not link `libzkgui.so`.** Its dynamic section has twenty-five
`NEEDED` entries and that is not among them. It is a nine-kilobyte launcher
over three singletons:

```
EasyUIContext::getInstance / initEasyUI / runEasyUI / deinitEasyUI
NetManager::getInstance / start
HardwareManager::getInstance
```

from `libeasyui.so`, `libzknet.so` and `libzkhardware.so`.

**The application is `dlopen`ed.** Confirmed by starting the stock app and
reading `/proc/<pid>/maps`: `/res/lib/libzkgui.so` is mapped by a process
whose `NEEDED` list does not mention it.

`libeasyui.so` imports `dlopen` and `dlsym` and contains no `libzkgui`
string anywhere, so the path is constructed at runtime rather than stored -
most plausibly from the program name, which would make `/bin/zkgui` load
`/res/lib/libzkgui.so` by convention. That is a guess and is flagged as one.

Still unknown, and it is the thing that decides whether STIPPLE can persist as
a drop-in replacement: **which symbol is looked up after the library is
opened.** Until that is known, building STIPPLE as `libzkgui.so` is not
something anyone can attempt.

## The stock firmware will flash an image for you, over HTTP, unauthenticated

Probed live on the device. This is the easiest flashing path that exists and
it needs no tool from us at all.

`/bin/zkgui` runs a `ConfigWebServer` on port 80 with an OTA handler,
`ConfigWebServer::otaUpdateRequest`. It takes a **URL to download**, not an
uploaded file:

```
POST /update
{}                              -> {"code":400,"message":"Missing parameter: mcu/app.downloadUrl"}
{"app":{"version":"9.9.9"}}     -> {"code":400,"message":"Missing parameter: app.downloadUrl"}
```

Supplying `version` makes it commit to the `app` branch and name the field
precisely, which gives the shape:

```json
{"app": {"version": "0.1.0", "downloadUrl": "http://host/stipple.img"}}
```

with `mcu` as the sibling for MCU firmware.

**No authentication.** No token, no signature, no nonce - it answered a bare
POST from an unrelated machine on the LAN. The device downloads the image
itself and then applies the same validation the loader always does: magic,
device code, header CRC32, payload MD5. Nothing checks *who* built it.

That is the whole flashing story, and it is the vendor's own validated path:
serve an image from any HTTP server on the network, hand the device its URL,
and it installs it.

It is also worth saying out loud that this is an unauthenticated remote
firmware write, reachable by anything on the same network as a stock TC002.
STIPPLE does not expose anything like it - `/api/v1/system/restore-image`
stages a file to the USB volume and cannot flash - and the difference is
deliberate.

Two related endpoints seen in the same handler table: `/checkUpdate` and
`/firmware/checkUpdate`, which talk to Ulanzi's cloud and do use a token
(the log strings mention a 401 and a refresh). Those are for *discovering* an
update. Applying one needs none of it.

### An aside on provenance

The vendor binary carries symbols namespaced `awtrix` - `awtrix::Updater`,
`awtrix::ConfigWebServer`, and a path `../src/awtrix/ota/Updater.cpp`. Noted
because it bears on licensing and on where the official Ulanzi sources sit,
not because anything here derives from it. STIPPLE shares no code with it and
does not reference it in anything it ships.

## Where STIPPLE could hook in, measured rather than guessed

Two candidate hooks, both tested on hardware. One is ruled out, one is left
standing, and the reason the standing one cannot be tested yet is the whole
remaining problem.

### The framework loads its application by absolute path

`EasyUIContext::initLib()` is where it happens:

```
ConfigManager::getInstance()
ConfigManager::getStartupLibPath()      <- the path comes from configuration
dlopen(path, RTLD_LAZY)
dlsym(handle, <name 1>)                 <- three function pointers,
dlsym(handle, <name 2>)                    stored and used later
dlsym(handle, <name 3>)
```

**The three symbol names are obfuscated.** They live in `.data`, not
`.rodata`, as short high-entropy byte runs - `67 66 3d 61 53 59 2d 49 49 66
69 4c` and two others - decoded at runtime by something that runs before
they are used. A single-byte XOR does not recover them.

**And the path is absolute.** Tested directly: a stub `libzkgui.so` placed in
`/tmp`, which is *first* on `LD_LIBRARY_PATH`, was never loaded -
`/proc/<pid>/maps` still showed `/res/lib/libzkgui.so`. So the application
library cannot be shadowed the way a `NEEDED` library can, and replacing it
means writing the `res` partition.

**None of which necessarily matters**, because `dlopen` runs a library's
static constructors before the caller can `dlsym` anything. A replacement
whose constructor takes over the process never has to satisfy the three
obfuscated entry points at all. That is the route worth trying, and trying
it needs a `res` image.

### Shadowing libeasyui.so works, and then does not

The other idea: `/bin/zkgui` imports exactly four symbols from
`libeasyui.so`, and `/res/lib` precedes `/lib` on the library path.

```
EasyUIContext::getInstance / initEasyUI / runEasyUI / deinitEasyUI
```

An 8 KB shim exporting those four, dropped in `/tmp`, **was** picked up - the
shadowing mechanism works. It then failed one library further along:

```
/bin/zkgui: symbol lookup error: /lib/libzkupgrade.so:
            undefined symbol: _ZN4Json5Value12removeMemberEPKc
```

`libeasyui.so` exports 2564 symbols, and the other vendor libraries lean on
more than the launcher does. Intersecting every `NEEDED` library's undefined
symbols against its exports gives the real contract: **74 symbols**, not
four. Tractable in size, and the composition is the problem rather than the
count - `Json::Value`, `Thread`, `Mutex`, `Condition`, `MessageQueue`,
`StoragePreferences`. Those are C++ classes whose *memory layout* other
libraries were compiled against, so a replacement has to be ABI-compatible
and not merely API-compatible. That is a bad thing to depend on and a worse
thing to get subtly wrong.

So this route is recorded and set aside.

### What that leaves

Replace `/res/lib/libzkgui.so` with a library that takes over in its
constructor. No vendor ABI, no obfuscated symbols, no 74-symbol contract.

It cannot be tested from `/tmp` - that is what the stub test established -
so the first attempt has to be a `res` image written to the device. Which
makes demonstrating the restore path the precondition for finding out
whether the approach works at all, rather than a formality before shipping.

### And the hook works

`/res/etc/EasyUI.cfg` is plain JSON on the read-only squashfs, and
`ConfigManager::getStartupLibPath()` reads one field of it:

```json
"startupLibPath": "/res/lib/libzkgui.so"
```

Changing that field is the entire integration surface.

Proven without writing any flash. `mount -o bind` works over the read-only
squashfs, so a modified copy of the config was bound over the original and
pointed at `/tmp`:

```
$ mount -o bind /tmp/EasyUI.cfg /res/etc/EasyUI.cfg
$ setprop ctl.start zkswe
$ cat /tmp/stipple-zkgui-stub.log
[11586] static constructor ran - dlopen reached us
```

`/proc/<pid>/maps` showed `/tmp/libstipple.so` mapped and
`/res/lib/libzkgui.so` absent. **A 7.6 KB library replaced the 7.5 MB vendor
application outright**, and `/bin/zkgui` ran on top of it.

The three obfuscated `dlsym` names never mattered: `dlopen` runs static
constructors before the caller can look anything up, so a library that takes
over in its constructor never reaches them.

Unmounting restored the original, and a power cycle would have done the same.
Any future change to the startup path should be tried this way before it is
written to flash. See
[ADR 0021](../adr/0021-stipple-as-the-startup-library.md).

## The application owns the Wi-Fi, all of it (2026-09-24)

Measured after a flashed STIPPLE booted with no network *and* no setup
hotspot. Both symptoms have one cause, and it is not the one the earlier
"there is no DHCP client on this device" section implies.

**There is no `wlan0` until somebody loads the driver, and nobody does but the
application.**

```
/lib/modules/4.9.84/aic8800_bsp.ko     74712 bytes
/lib/modules/4.9.84/aic8800_fdrv.ko   310464 bytes
```

That directory holds the two modules and **nothing else** - no `modules.dep`,
no `modules.alias`. Without an index the kernel cannot autoload them, so
`request_module` and `modprobe` are both out. Nor does anything in userspace
start them: `/etc/init.rc` has no `insmod` at all, `/bin/ssd_init.sh` loads
only the display and audio modules (`mhal`, `mi_*`, `fbdev`), and
`/bin/zkdaemon` does not mention them.

`/lib/libzknet.so` does. It is the vendor's network HAL, and the stock
application drives it:

```
'start to insmod %s'        'insmod args: %s'      'insmod %s failed!'
'aic8800_bsp#aic8800_fdrv'  'aic_load_fw#aic8800_fdrv'
'ifname=wlan0 if2name=p2p0'
'ctl.start'  'ctl.stop'  'init.svc.wpa_supplicant'
'/data/misc/wifi/wpa_supplicant.conf'
```

`libzkgui.so` itself only `insmod`s `aic_btusb.ko`, the Bluetooth driver; the
Wi-Fi pair comes through this HAL. The `#`-separated strings are the load
order - `bsp` first, then `fdrv`, which depends on it.

`wpa_supplicant` is a service, and a deliberately inert one:

```
service wpa_supplicant /bin/wpa_supplicant -iwlan0 -Dnl80211 \
    -c/data/misc/wifi/wpa_supplicant.conf -C/dev/socket/ \
    -e/data/misc/wifi/entropy.bin
    class main
    disabled
    oneshot
```

`disabled` means it never starts on its own. `ctl.start wpa_supplicant` is the
only thing that starts it, and the HAL above is what sends it.

### Confirmed by tearing it down and putting it back

Not inferred. On a live device, with the vendor application stopped:

```
busybox rmmod aic8800_fdrv        rc=0
busybox rmmod aic8800_bsp         rc=0
ls /sys/class/net/                lo                <- no wlan0, no p2p0
busybox insmod .../aic8800_bsp.ko  rc=0
busybox insmod .../aic8800_fdrv.ko rc=0
ls /sys/class/net/                lo  p2p0  wlan0
/sbin/ifconfig wlan0 up           rc=0
```

The interface is **created by the module load**. Remove the driver and there
is no `wlan0` to configure, to associate, or to hand to `hostapd` - which is
why a STIPPLE that replaced the application had neither a network nor a setup
hotspot. The hotspot was not failing to broadcast; it could not start, because
its first step is `ifconfig wlan0 ...` on an interface that did not exist.

### What STIPPLE has to do, and where

`Tc002Hotspot::ensureRadio()` loads the pair if `/sys/class/net/wlan0` is
absent, building the path from `uname()` rather than hard-coding `4.9.84`.
`ensureStation()` then brings the interface up and sends `ctl.start
wpa_supplicant`. Both are idempotent and both are cheap when the work is
already done, because during development the stock application has usually
done it already - which is exactly how this went unnoticed for so long.

### Why it was missed

The startup hook was proven by bind-mounting a modified `EasyUI.cfg` over the
read-only one. A bind mount does not survive a reboot, so that test ran on a
system where the stock application had already loaded the driver, brought up
the interface and started the supplicant. **The test inherited the very thing
it should have been checking for**, and the lease it observed was real but
told us nothing about a cold boot.

The general lesson is worth more than the specific bug: a test that reuses a
running system's state cannot tell you what happens without it.

## zkdaemon will delete STIPPLE if STIPPLE does not announce itself (2026-09-24)

The most consequential thing on this device, and the explanation for a revert
that had been blamed on a stale upgrade flag.

`/bin/zkdaemon` is 13 KB, runs as a `oneshot` service in `class main`, and is
not a daemon in the usual sense. It is the recovery mechanism. Its strings,
in full for the part that matters:

```
'/sys/class/gpio/gpio%d/value'      'gpio%d is not exist, need to export.'
'[D][zkdaemon] Start key monitor loop'
'[D][zkdaemon] Key pressed confirmed'
'[D][zkdaemon] Key long press %dms detected, trigger recovery'
'setprop ctl.stop zkswe'            'rm -rf /data/*'
'[D][zkdaemon] Key released early (%dms), ignore'

'sys.zkapp.state'    'running'      'ZK_APPCHECK_DELAY'
'[D][zkdaemon] app state: %s'
'[D][zkdaemon] Auto recovery triggered'

'persist.zkupgrade.dir'  '/mnt/storage'  '%s/update.img'
'/bin/zkupgradebin'      'sys.zkupgrade.flag'   'ctl.restart'
```

Two paths into the same recovery. One is the reset button, read straight off
GPIO. **The other needs no button at all**: it reads the property
`sys.zkapp.state`, waits `ZK_APPCHECK_DELAY`, and if the application has not
declared itself `running`, triggers *auto recovery* - `rm -rf /data/*` and a
reinstall from `/mnt/storage/update.img`.

`libzkgui.so` carries the string `sys.zkapp.state`; `libzknet.so` does not.
**The stock application sets it, and nothing else does.**

### What that meant for the first flashed build

STIPPLE replaced the application and never set the property, so:

1. STIPPLE flashed, booted, and ran - the shim and the panel both worked.
2. `zkdaemon` waited, saw no `running`, and called it a failed application.
3. `rm -rf /data/*` deleted **`/data/stipple/libstipple.so`** and
   **`/data/misc/wifi/wpa_supplicant.conf`** in the same sweep.
4. It reinstalled `/mnt/storage/update.img` - which at the time held the
   *stock* image, staged there as a safety net.
5. The device came back on stock **with no Wi-Fi credentials**.

Every observed symptom falls out of that: the unexplained progress bar, the
revert, and the detail that had refused to reconcile - why *stock* also came
up without Wi-Fi, when `/data` had obviously survived well enough to keep
`libstipple.so` from an earlier test. It had not survived. It had been emptied
and partly rewritten.

It also reframes the earlier lockout. That was attributed to holding the reset
button wiping `/data`; the button certainly does that, but auto recovery
reaches the same `rm -rf /data/*` with nobody touching anything.

### What STIPPLE has to do

`Tc002Platform::announceRunning()` sends `setprop sys.zkapp.state running`,
and `stippleMain` calls it **before opening the panel, the MCU or the
network** - none of which are worth losing STIPPLE over if they are slow or
fail.

### The consequence for staging an image

An image at `/mnt/storage/update.img` is not passive. It is what *both*
recovery paths install, one of which can fire on its own. Staging a stock
image there as a safety net is therefore a way of arming an automatic revert,
which is exactly what it did. The guidance in `docs/recovery.md` - remove the
stick, keep the volume empty - is now backed by the mechanism rather than by
one bad night.
