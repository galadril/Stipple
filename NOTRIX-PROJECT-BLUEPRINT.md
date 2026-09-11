# NOTRIX — Project Blueprint

> A consumer-friendly, open-source alternative software stack for the Ulanzi TC002 pixel clock.
>
> **Status:** Design / research blueprint  
> **Target device:** Ulanzi U-Clock TC002 / Pixbar 2  
> **Display:** 52 × 16 RGB (832 pixels)  
> **Platform:** Z21 / SigmaStar SSD21x, ARMv7 Cortex-A7, FlyThings / EasyUI environment  
> **Primary goal:** Build a reliable, polished, extensible TC002-native experience inspired by the *ideas* that made AWTRIX useful, without copying AWTRIX NG source code.

---

# 1. Vision

NOTRIX should turn the TC002 into a genuinely open, reliable and extensible pixel display.

The project should not try to reproduce the current Ulanzi firmware internally. It should provide a clean software architecture with:

- predictable rendering;
- first-class custom apps;
- notifications;
- smooth scrolling text;
- proper Unicode/glyph handling;
- transitions and animations;
- HTTP and MQTT APIs;
- Home Assistant / Domoticz / Node-RED friendly integration;
- local-first operation;
- safe development and recovery;
- automated builds;
- reproducible releases;
- consumer-friendly installation;
- a polished documentation website;
- an emulator / preview environment;
- a long-term path toward one-click installation and updates.

The user experience should eventually be:

1. Buy a TC002.
2. Connect it to Wi-Fi.
3. Open the NOTRIX website.
4. Click **Install**.
5. Select or discover the clock.
6. Install NOTRIX.
7. Open the device web UI.
8. Configure integrations.
9. Never need FlyThings IDE, Eclipse, ADB commands or manual compilation.

Developer complexity may exist internally, but it must not leak into the normal installation experience.

---

# 2. Non-goals

NOTRIX is **not**:

- a binary port of AWTRIX 3;
- a binary port of AWTRIX NG;
- an ESP32 firmware;
- dependent on AWTRIX source code;
- dependent on the official Ulanzi cloud;
- dependent on Ulanzi Studio after installation;
- a collection of hacks layered around the existing DIY API;
- a project that requires every end-user to install FlyThings IDE.

AWTRIX may be studied as a product/API/UX reference. Concepts such as custom apps, notifications, app rotation, indicators and MQTT integration are generic ideas and can be independently reimplemented.

---

# 3. Guiding principles

## 3.1 Reliability before feature count

A small feature that works 100% of the time is more valuable than five half-working features.

Examples:

- scrolling must never randomly clip;
- app order must be deterministic;
- notifications must have deterministic lifetime semantics;
- two text elements must always both render;
- reboot behavior must be predictable;
- corrupted user configuration must not brick the clock.

## 3.2 Renderer owned by NOTRIX

Do not depend on Ulanzi's DIY text/layout renderer for the core experience.

NOTRIX should own a complete 52×16 framebuffer and render:

- pixels;
- lines;
- rectangles;
- sprites;
- icons;
- bitmap fonts;
- scrolling text;
- progress bars;
- graphs;
- transitions;
- animations.

At the lowest level:

```text
Application
    ↓
Scene
    ↓
Renderer
    ↓
52×16 RGB framebuffer
    ↓
TC002 display adapter
    ↓
PageBase::sendLedData(...)
```

This isolates higher layers from FlyThings-specific quirks.

## 3.3 Hardware abstraction first

All hardware-specific code should live behind interfaces.

```text
IFrameBufferDisplay
IInputDevice
IAudioOutput
IMicrophone
INetworkManager
IStorage
ISystemClock
IRebooter
IUpgradeManager
```

The rest of NOTRIX should not know whether it runs on real hardware or in the desktop/browser emulator.

## 3.4 Local first

Core functionality must work without internet access.

Cloud services can be integrations, never requirements.

## 3.5 Compatibility where useful, not at any cost

Provide a useful AWTRIX-compatible API subset so existing integrations can migrate easily.

Do not constrain the entire architecture to historical AWTRIX behavior.

Use two API layers:

```text
/api/*       Compatibility API
/api/v1/*    Native NOTRIX API
```

## 3.6 Recovery is a feature

No persistent firmware release is considered consumer-ready until:

- recovery is tested;
- factory recovery is documented;
- failed boot rollback is tested;
- downgrade is possible;
- release artifacts are signed/checksummed;
- a bad configuration cannot cause a boot loop.

---

# 4. Known TC002 platform facts

The official Ulanzi repository currently documents:

- 52×16 RGB LED matrix;
- SPI display path;
- rotary encoder with clockwise/counter-clockwise/push;
- three additional buttons;
- speaker;
- microphone level reported by MCU;
- Wi-Fi;
- BLE;
- GPIO_06 and GPIO_85;
- USB-C operating as mass-storage;
- Wi-Fi ADB for development;
- non-persistent debug deployment;
- persistent image generation through the FlyThings build process;
- anti-brick application-state mechanism;
- reset-button factory recovery.

The official demo indicates that the MCU must be initialized before normal LED-board operation.

The documented display API includes an internal throttle and Ulanzi warns against frame intervals below approximately 15 ms.

Community research on a real TC002 reports:

- SigmaStar/SStar SSD21x family SoC;
- ARMv7 / Cortex-A7;
- `arm-linux-gnueabihf` cross compilation;
- FlyThings / EasyUI runtime;
- application loaded as `libzkgui.so`;
- `/tmp`-based non-persistent application override;
- power cycle restores stock firmware after temporary sideload;
- headless Docker cross-compilation is possible without FlyThings IDE.

These findings must be verified against every hardware/firmware revision we decide to support.

---

# 5. Licensing strategy

This must be decided before substantial implementation.

The official Ulanzi TC002 source repository is GPL-3.0-or-later.

If NOTRIX derives from or incorporates GPL-covered Ulanzi code, the distributed combined/derived work must comply with the GPL.

Recommended project license:

```text
GPL-3.0-or-later
```

Reasons:

- simplest compatibility with the official source base;
- permits modification and redistribution;
- encourages improvements to stay available;
- avoids license ambiguity for contributors.

Maintain:

```text
LICENSE
THIRD_PARTY_NOTICES.md
NOTICE.md
docs/legal/
```

Every imported dependency must have:

- source;
- version;
- license;
- reason for inclusion;
- redistribution status.

Do not copy AWTRIX NG source into the project unless separately reviewed for license compatibility and intentionally accepted.

Preferred approach: independent implementation based on public behavior, concepts and documented protocols.

---

# 6. Proposed repository

Project naming:

- **Product / firmware:** `NOTRIX`
- **Current GitHub repository:** `galadril/notrix`
- **Repository model:** monorepo
- **Possible future organization:** `notrix-os`
- **Possible future repository URL:** `notrix-os/notrix`

The project should start under the existing `galadril` account to minimize setup and friction. GitHub repositories can be transferred to an organization later, so the codebase, package names, documentation links and automation should avoid hard-coding the current owner wherever practical.

Working repository:

```text
github.com/galadril/notrix
```

Possible future move:

```text
github.com/notrix-os/notrix
```

The code, package names and public branding should always use **NOTRIX**, not `galadril`, so a future repository transfer is an infrastructure change rather than a product rename.

Suggested monorepo:

```text
galadril/notrix (repository root)
├── .github/
│   ├── ISSUE_TEMPLATE/
│   ├── workflows/
│   │   ├── firmware-ci.yml
│   │   ├── firmware-release.yml
│   │   ├── web-ci.yml
│   │   ├── docs.yml
│   │   ├── codeql.yml
│   │   └── nightly.yml
│   └── dependabot.yml
│
├── firmware/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── app/
│   │   ├── core/
│   │   ├── display/
│   │   ├── graphics/
│   │   ├── input/
│   │   ├── audio/
│   │   ├── network/
│   │   ├── storage/
│   │   ├── api/
│   │   ├── mqtt/
│   │   ├── apps/
│   │   ├── notifications/
│   │   ├── scheduler/
│   │   ├── settings/
│   │   ├── system/
│   │   └── platform/
│   │       ├── tc002/
│   │       └── simulator/
│   ├── include/
│   ├── assets/
│   │   ├── fonts/
│   │   ├── icons/
│   │   └── animations/
│   └── tests/
│
├── sdk/
│   ├── schemas/
│   ├── examples/
│   ├── javascript/
│   ├── python/
│   └── dotnet/
│
├── simulator/
│   ├── core/
│   └── web/
│
├── installer/
│   ├── helper/
│   ├── cli/
│   └── protocol/
│
├── web/
│   ├── site/
│   ├── installer/
│   ├── emulator/
│   └── device-ui/
│
├── integrations/
│   ├── domoticz/
│   ├── home-assistant/
│   ├── node-red/
│   └── examples/
│
├── tooling/
│   ├── toolchain/
│   ├── build/
│   ├── packaging/
│   ├── flashing/
│   └── asset-converter/
│
├── docs/
│   ├── architecture/
│   ├── development/
│   ├── hardware/
│   ├── protocol/
│   ├── api/
│   ├── installation/
│   ├── recovery/
│   ├── contributing/
│   └── adr/
│
├── scripts/
├── tests/
│   ├── protocol/
│   ├── api/
│   ├── integration/
│   └── hardware/
├── LICENSE
├── THIRD_PARTY_NOTICES.md
├── CONTRIBUTING.md
├── SECURITY.md
├── CODE_OF_CONDUCT.md
└── README.md
```

---

# 7. Core software architecture

## 7.1 Process model

Initial implementation should remain compatible with the FlyThings application model.

Conceptually:

```text
zkswe / EasyUI host
        │
        └── libzkgui.so
                │
                └── NOTRIX runtime
```

Do not assume we can replace the entire lower-level operating system in Stage 1.

The first goal is replacing the *user application experience*, not Linux bootloader/kernel/platform services.

Long term, deeper replacement can be researched separately.

---

# 8. Runtime components

## 8.1 ApplicationHost

Responsible for:

- startup;
- dependency wiring;
- watchdog heartbeat;
- hardware initialization;
- clean shutdown;
- application loop;
- recovery-safe boot state.

Pseudo structure:

```cpp
class ApplicationHost {
public:
    bool initialize();
    void run();
    void shutdown();
};
```

Boot order:

```text
1. logging
2. crash marker / boot state
3. MCU initialization
4. display
5. persistent settings
6. networking
7. API
8. MQTT
9. app engine
10. input
11. mark system healthy
12. enter normal event loop
```

The platform-specific "application running" anti-brick signal must be set at the correct safe point, based on real-device testing.

---

# 9. Display architecture

## 9.1 Framebuffer

Canonical framebuffer:

```cpp
struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

Rgb framebuffer[16][52];
```

Memory usage must be carefully measured because reported TC002 RAM is limited.

Potential internal alternatives:

- RGB888 framebuffer;
- RGB565 framebuffer;
- indexed/palette framebuffer;
- double buffering only when memory permits.

Start simple, profile immediately.

## 9.2 Canvas API

```cpp
Canvas::clear();
Canvas::pixel(x, y, color);
Canvas::line(x1, y1, x2, y2, color);
Canvas::rect(x, y, w, h, color);
Canvas::fillRect(...);
Canvas::bitmap(...);
Canvas::sprite(...);
Canvas::text(...);
Canvas::measureText(...);
Canvas::progressBar(...);
Canvas::graph(...);
```

## 9.3 Compositor

Support layers:

```text
background
app content
indicators
notification overlay
system overlay
debug overlay
```

Each element gets clipping boundaries so one component cannot corrupt another region.

## 9.4 Frame pacing

Target modes:

- static: render only when dirty;
- low animation: 10–20 FPS;
- smooth animation: 30 FPS;
- maximum experimental: constrained by hardware and the documented ~15 ms minimum.

Do **not** default to 60 FPS merely because the display API may technically approach that interval.

Use:

```text
dirty rendering
frame budget
animation clock
dropped frame metrics
```

---

# 10. Font and text engine

This is a first-class component.

Requirements:

- UTF-8 input;
- glyph fallback;
- degree symbol;
- accented Latin characters;
- configurable bitmap fonts;
- proportional and fixed-width fonts;
- text measurement;
- clipping;
- left / center / right alignment;
- top / middle / bottom vertical alignment;
- multi-line text;
- marquee;
- bounce;
- automatic scroll delay;
- pixel-perfect deterministic rendering.

Fonts should be converted at build time into compact bitmap tables.

Possible tiers:

```text
3×5
4×6
5×7
6×8
8×8
variable 8–12 high
```

Do not initially promise full Unicode. Architect for Unicode while shipping curated glyph packs.

---

# 11. Scene model

The native API should describe scenes rather than exposing low-level implementation internals.

Example:

```json
{
  "name": "living-room",
  "duration": 10,
  "elements": [
    {
      "type": "icon",
      "x": 1,
      "y": 3,
      "icon": "thermometer"
    },
    {
      "type": "text",
      "rect": [11, 0, 40, 8],
      "text": "21.4°C",
      "align": "left"
    },
    {
      "type": "text",
      "rect": [11, 8, 40, 8],
      "text": "Living room",
      "scroll": "auto"
    }
  ]
}
```

Scene elements:

```text
pixel
line
rectangle
text
icon
bitmap
sprite
progress
graph
animation
group
```

Future:

```text
audio visualization
QR-style tiny patterns
clock widgets
weather widget
sensor widget
```

---

# 12. App model

An app is a persistent logical item in the carousel.

```text
App
├── id
├── name
├── enabled
├── position
├── duration
├── lifetime
├── scene
├── transitions
└── metadata
```

App types:

```text
system
local
remote
integration
temporary
```

The app manager owns ordering.

Never infer app ordering from filesystem enumeration or associative container iteration.

---

# 13. Notifications

Notifications should be separate from apps.

Model:

```text
priority
title
message
icon
sound
repeat
duration
stacking
wake-display
hold
dismissable
```

Priority example:

```text
0 informational
1 normal
2 important
3 urgent
```

Queue policy must be deterministic.

Support:

```http
POST /api/v1/notifications
DELETE /api/v1/notifications/{id}
DELETE /api/v1/notifications
```

Compatibility:

```http
POST /api/notify
```

---

# 14. Carousel / scheduler

Responsibilities:

- deterministic app order;
- per-app duration;
- pause;
- manual next/previous;
- pinned app;
- temporary app;
- sleep mode;
- notification interruption;
- transition management.

Rotary control default:

```text
rotate left  → previous app
rotate right → next app
press        → app action / details
```

Buttons can be mapped by users.

---

# 15. Input system

Convert physical events to logical actions.

Raw events:

```text
KEY_LEFT
KEY_RIGHT
KEY_MIDDLE
ROTARY_LEFT
ROTARY_RIGHT
ROTARY_PRESS
```

Logical actions:

```text
APP_PREVIOUS
APP_NEXT
APP_ACTION
NOTIFICATION_DISMISS
BRIGHTNESS_UP
BRIGHTNESS_DOWN
VOLUME_UP
VOLUME_DOWN
CUSTOM_ACTION
```

Support:

- short press;
- long press;
- double press only if reliable;
- rotary acceleration.

Mappings belong in configuration.

---

# 16. Audio

Stage 1 audio:

- notification sound;
- short local audio files;
- volume;
- mute.

Later:

- alarms;
- timers;
- generated tones;
- local playback;
- optional streaming experiments.

Audio must never block rendering.

---

# 17. Network model

Prefer:

```text
Wi-Fi
├── HTTP REST API
├── WebSocket/SSE status
├── MQTT client
├── NTP
└── optional mDNS
```

Do not require MQTT.

Do not require Home Assistant.

---

# 18. Device discovery

Consumer installation and integrations benefit enormously from discovery.

Research/implement in order:

1. mDNS announcement;
2. SSDP only if needed;
3. MQTT discovery where relevant;
4. manual IP fallback.

Suggested hostname:

```text
notrix-xxxx.local
```

Suggested service:

```text
_notrix._tcp
```

---

# 19. API design

## 19.1 Native API

```text
GET    /api/v1/device
GET    /api/v1/health
GET    /api/v1/version

GET    /api/v1/apps
POST   /api/v1/apps
GET    /api/v1/apps/{id}
PUT    /api/v1/apps/{id}
DELETE /api/v1/apps/{id}
POST   /api/v1/apps/{id}/activate

GET    /api/v1/notifications
POST   /api/v1/notifications
DELETE /api/v1/notifications/{id}

GET    /api/v1/settings
PATCH  /api/v1/settings

POST   /api/v1/display/preview
POST   /api/v1/display/frame

GET    /api/v1/assets
POST   /api/v1/assets
DELETE /api/v1/assets/{id}

POST   /api/v1/system/reboot
POST   /api/v1/system/update
```

## 19.2 Compatibility API

Initial target:

```text
POST   /api/custom
DELETE /api/custom/{name}
POST   /api/notify
POST   /api/settings
GET    /api/stats
```

Do not claim complete AWTRIX compatibility until contract tests prove it.

Maintain a public compatibility matrix.

---

# 20. MQTT design

Native namespace:

```text
notrix/{deviceId}/...
```

Example:

```text
notrix/abc123/status
notrix/abc123/app/set
notrix/abc123/notification/set
notrix/abc123/settings/set
notrix/abc123/button
```

Optional compatibility topics can be enabled separately.

Use retained messages carefully.

Device status should include:

```json
{
  "online": true,
  "version": "0.4.0",
  "uptime": 123456,
  "brightness": 80,
  "activeApp": "clock",
  "rssi": -51
}
```

---

# 21. Configuration

Store versioned configuration.

```json
{
  "schemaVersion": 3,
  "device": {},
  "display": {},
  "network": {},
  "mqtt": {},
  "apps": [],
  "input": {},
  "audio": {}
}
```

Requirements:

- transactional writes;
- checksum;
- backup copy;
- migration code;
- default recovery;
- unknown-field tolerance where practical.

Never brick due to malformed JSON.

---

# 22. Logging and diagnostics

Provide structured logging:

```text
TRACE
DEBUG
INFO
WARN
ERROR
FATAL
```

Ring buffer only; avoid excessive flash writes.

Expose:

```http
GET /api/v1/diagnostics
GET /api/v1/logs
```

Metrics:

```text
uptime
free memory
minimum free memory
frame time
dropped frames
HTTP requests
MQTT reconnects
Wi-Fi RSSI
app switches
notification queue depth
last reset reason
firmware version
build commit
```

Never expose Wi-Fi passwords or secrets through diagnostics.

---

# 23. Security

Initial LAN-first product does not mean "ignore security".

Requirements:

- no default internet exposure;
- APIs bind only to intended interfaces;
- optional API token;
- CORS restricted;
- no shell endpoint;
- uploaded assets validated;
- update packages cryptographically verified;
- no arbitrary paths from API input;
- size limits;
- input bounds checking;
- fuzz API parsers where practical.

Long term:

- signed releases;
- signed update manifest;
- secure update channel;
- optional HTTPS if feasible on hardware.

---

# 24. Emulator

The emulator is essential.

It should execute the same:

```text
scene parser
layout engine
font engine
animation engine
app scheduler
```

as the device whenever possible.

Only the hardware adapter differs.

Browser representation:

```text
┌────────────────────────────────────────────────────┐
│ 52 × 16 pixel display                             │
└────────────────────────────────────────────────────┘

[Left] [Middle] [Right]     ↶ [Rotary] ↷ [Press]
```

Controls:

- brightness simulation;
- button events;
- rotary;
- time speed;
- notification injection;
- network state;
- screenshot;
- GIF/video capture;
- frame-time diagnostics.

This becomes both:

- developer tool;
- documentation demo;
- consumer app designer.

---

# 25. Web UI

Device-local UI:

```text
Dashboard
Apps
Notifications
Icons
Display
Integrations
MQTT
Network
Buttons
Audio
System
Update
Diagnostics
```

Desktop and mobile responsive.

Use the same scene editor as the public website if size allows.

A device with limited memory may serve a compressed minimal web UI while the full editor remains hosted on the project website.

---

# 26. Public website

Initial website:

```text
https://galadril.github.io/notrix/
```

Preferred custom domain later:

```text
https://notrix.dev/
```

Possible organization-hosted GitHub Pages later:

```text
https://notrix-os.github.io/
```

Once available, the custom domain should become the canonical public URL so a future GitHub repository transfer does not affect user-facing links.

Sections:

```text
Home
Install
Demo
Apps
Integrations
Documentation
API
Developer Guide
Hardware
Recovery
Releases
FAQ
GitHub
```

Home page should communicate:

> Open firmware for the Ulanzi TC002.  
> Local-first. Fast. Extensible. Built for integrations.

Buttons:

```text
Install NOTRIX
Try Emulator
Read Documentation
View GitHub
```

GitHub Pages is sufficient initially.

Possible stack:

```text
VitePress
Docusaurus
Astro Starlight
```

Prefer a static site with GitHub Pages deployment.

---

# 27. Installer / "online flasher"

This needs careful terminology.

## 27.1 Constraint

The TC002 official documentation states that USB-C behaves as mass-storage and normal development/debug deployment uses Wi-Fi ADB.

Therefore a normal ESP32-style browser WebSerial/WebUSB flasher should **not** be assumed possible.

## 27.2 Stage A — browser-guided temporary installer

Public website:

```text
Install
  ↓
Download NOTRIX Helper
  ↓
helper runs locally
  ↓
website connects to helper on localhost
  ↓
discover TC002
  ↓
ADB over LAN
  ↓
temporary sideload
```

Helper responsibilities:

- discover device;
- test connectivity;
- establish ADB session;
- verify model/firmware;
- push temporary bundle;
- restart FlyThings app;
- stream installation status;
- restore stock runtime;
- collect safe diagnostics.

Website ↔ helper:

```text
localhost WebSocket
or
localhost HTTP
```

Security:

- random session token;
- local origin allowlist;
- confirmation for destructive actions;
- no remote arbitrary-command endpoint.

## 27.3 CLI installer

Always provide:

```bash
notrix install --host 192.168.1.42
notrix restore --host 192.168.1.42
notrix doctor --host 192.168.1.42
```

The CLI is the reference implementation.

The GUI helper wraps the same library.

## 27.4 Persistent installer

Do **not** release persistent flashing to normal users until the image packaging and upgrade mechanism is completely understood.

Requirements before enabling:

- reproducible `update.img`;
- at least two hardware devices tested;
- factory reset tested;
- interrupted update tested;
- invalid image rejection tested;
- old → new upgrade tested;
- new → old downgrade tested;
- release checksum verified;
- boot health flag verified;
- rollback verified.

## 27.5 Long-term true one-click install

If a reliable network-accessible upgrade interface is found:

```text
website
  ↓
local helper
  ↓
download signed firmware
  ↓
verify SHA-256/signature
  ↓
upload/update device
  ↓
reboot
  ↓
health check
```

The website should never directly fetch a random GitHub artifact and blindly flash it.

---

# 28. Update system

Release manifest example:

```json
{
  "channel": "stable",
  "version": "1.2.3",
  "hardware": ["tc002"],
  "minimumBootVersion": "x",
  "url": "...",
  "sha256": "...",
  "signature": "...",
  "releaseNotes": "..."
}
```

Channels:

```text
stable
beta
nightly
```

Stable should be default.

OTA behavior:

```text
download
verify
stage
reboot
health check
commit
```

If platform update mechanics do not support safe A/B behavior, design the safest equivalent possible and preserve the hardware reset recovery path.

---

# 29. Development workflow

## 29.1 No developer should need a Windows-only IDE for normal builds

FlyThings IDE may remain useful as a reference tool, but CI should build headlessly.

Preferred development:

```text
Git clone
    ↓
Docker toolchain
    ↓
CMake/Make
    ↓
libzkgui.so + resources
    ↓
ADB temporary sideload
```

Community research has already demonstrated a headless ARM cross-compile path using the FlyThings packages/toolchain.

We should independently reproduce and document it.

## 29.2 Developer prerequisites

Ideal:

```text
Git
Docker
Python or Node only for helper tooling
ADB client bundled by tooling where licensing permits
```

Command:

```bash
./dev build
./dev test
./dev run-simulator
./dev deploy 192.168.1.42
./dev logs 192.168.1.42
./dev restore 192.168.1.42
```

Windows PowerShell equivalent:

```powershell
.\dev.ps1 build
```

## 29.3 Temporary hardware debug cycle

Development loop:

```text
edit
 ↓
unit tests
 ↓
cross compile
 ↓
push bundle to /tmp
 ↓
write temporary EasyUI config
 ↓
restart zkswe
 ↓
observe logs
 ↓
iterate
```

Because the temporary app resides in volatile storage, power cycling restores the normal installed application/stock firmware.

This should be the default development mode.

## 29.4 Device profile

Create a `tc002 doctor` command:

```text
Device found: TC002
IP: 192.168.1.42
SoC: SSD21x
Architecture: armv7
Firmware: 2.6.2
MCU: T1.0.13
ADB: available
Temporary sideload: supported
Persistent flash: disabled
Memory: ...
Disk: ...
```

Unknown hardware revisions should trigger warnings rather than installation.

---

# 30. Debug architecture

Provide compile modes:

```text
DEBUG
RELEASE
ASAN-SIMULATOR
PROFILE
```

Real device may not support every sanitizer.

Simulator should use:

- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- strict compiler warnings;
- tests under valgrind where useful.

Compiler policy:

```text
-Wall
-Wextra
-Wpedantic
-Werror (CI where practical)
```

Device debugging:

```text
ADB logs
internal ring logger
crash markers
boot counters
reset reason
free-memory watermark
```

---

# 31. Test strategy

## 31.1 Unit tests

Host-native tests for:

```text
scene layout
text measurement
scroll calculations
scheduler
notification queue
JSON parsing
configuration migration
MQTT topic handling
API validation
color conversion
clipping
```

## 31.2 Golden-image rendering tests

Extremely useful.

Input scene:

```json
{ ... }
```

Expected framebuffer:

```text
testdata/weather-expected.rgb
```

Test:

```text
render(scene) == expectedFramebuffer
```

This catches pixel regressions.

Generate visual PNG snapshots in CI for failed tests.

## 31.3 API contract tests

Run against:

- simulator;
- real hardware test unit.

Same test suite.

## 31.4 Compatibility tests

Have fixtures for known AWTRIX-compatible payloads.

Explicit matrix:

| Feature | Supported | Notes |
|---|---:|---|
| basic text | yes | |
| icon | yes | |
| duration | yes | |
| scroll | yes | native implementation |
| progress | planned | |
| sound | planned | |
| advanced effects | partial | |

## 31.5 Hardware-in-loop

Eventually attach a dedicated TC002 to a self-hosted GitHub runner.

Safe tests:

```text
deploy temp build
health endpoint
display frame
input event
MQTT
API
restart
restore
```

Do not persistently flash every PR.

Persistent-flash HIL belongs to release qualification only.

---

# 32. Build system

Recommended layering:

```text
CMake
  +
Docker pinned cross-toolchain
  +
dependency lock file
```

The build must be reproducible.

Never have CI silently download "latest" FlyThings dependencies.

Pin:

```text
compiler
SDK package revisions
headers
libraries
build image digest
Node/Bun version
Python version
```

Artifact outputs:

```text
notrix-runtime.tar
libzkgui.so
resources/
manifest.json
symbols/debug bundle
SBOM
checksums.txt
```

Persistent phase:

```text
notrix-X.Y.Z.update.img
```

---

# 33. GitHub Actions

## Pull requests

```text
format
lint
license check
host build
unit tests
golden renderer tests
API tests
web build
simulator build
dependency audit
```

## Main branch

Everything above plus:

```text
ARM firmware compile
integration package
documentation build
nightly artifacts
```

## Tags

Tag:

```text
v0.3.0
```

Triggers:

```text
clean build
all tests
ARM release build
SBOM
checksums
signing
GitHub Release
docs version
installer metadata update
```

Example release assets:

```text
notrix-0.3.0-sideload.zip
notrix-helper-windows-x64.exe
notrix-helper-linux-x64
notrix-helper-macos-arm64
SHA256SUMS
SBOM.spdx.json
source.tar.gz
```

Later:

```text
notrix-0.3.0-update.img
```

---

# 34. Versioning

Use Semantic Versioning:

```text
0.x     development
1.0     consumer-stable
```

Firmware reports:

```text
version
git commit
build date
release channel
hardware target
API version
config schema version
```

Example:

```json
{
  "version": "0.5.0-beta.2",
  "commit": "6cf801a",
  "api": 1,
  "schema": 4,
  "target": "tc002-z21"
}
```

---

# 35. Branch strategy

Keep it simple:

```text
main
feature/*
fix/*
research/*
```

No long-lived `develop` branch initially.

Use short PRs and feature flags.

---

# 36. ADRs

Architectural decisions should be captured immediately.

Initial ADRs:

```text
0001-project-scope.md
0002-gpl-license.md
0003-framebuffer-renderer.md
0004-flythings-runtime-boundary.md
0005-headless-build.md
0006-temporary-adb-development.md
0007-native-api-plus-compatibility.md
0008-installer-helper.md
0009-update-security.md
0010-font-strategy.md
```

---

# 37. Stage roadmap

---

# Stage 0 — Research and reproducibility

## Goal

Know exactly what the hardware/runtime does and reproduce the build without relying on undocumented manual IDE steps.

## Tasks

- fork/reference official Ulanzi repository;
- archive relevant public documentation links;
- inventory official demo source;
- document TC002 hardware;
- confirm architecture on our own physical device;
- inspect firmware version;
- inspect MCU version;
- reproduce Wi-Fi ADB connection;
- reproduce temporary debug deployment;
- reproduce headless ARM build in Docker;
- identify exact runtime files;
- document `/tmp` sideload mechanism;
- test power-cycle restoration;
- document reset-button recovery;
- investigate persistent `update.img`;
- clarify TF-card vs USB/mass-storage upgrade behavior for actual retail hardware revision;
- record every command in scripts.

## Deliverables

```text
docs/hardware/tc002.md
docs/development/device-probe.md
docs/development/headless-build.md
docs/development/sideload.md
docs/recovery/factory.md
tooling/toolchain/Dockerfile
scripts/device-doctor.*
```

## Exit criteria

A new developer can clone the repo and run:

```bash
./dev build
./dev deploy <ip>
```

and see a NOTRIX test pattern on a physical TC002.

A power cycle returns to the previous persistent application.

---

# Stage 1 — Minimal display runtime

## Goal

Replace Ulanzi's rendering path with our own deterministic renderer.

## Features

- application boot;
- MCU initialization;
- 52×16 framebuffer;
- clear/pixel/line/rectangle;
- bitmap;
- custom font;
- basic UTF-8;
- degree symbol;
- static text;
- aligned text;
- clipping;
- display brightness;
- frame scheduler;
- debug statistics.

## Demo screen

```text
┌────────────────────────────────────────────────────┐
│ NOTRIX                21.4°C                     │
│ build 0.1             23:04:17                     │
└────────────────────────────────────────────────────┘
```

## Exit criteria

24-hour soak test with:

- no crash;
- no corrupted frames;
- no runaway memory growth;
- no random missing elements.

---

# Stage 2 — Simulator and rendering tests

## Goal

Make most UI development independent of hardware.

## Features

- desktop/browser 52×16 simulator;
- shared render model;
- button simulation;
- rotary simulation;
- golden-image tests;
- animation timing;
- screenshot export;
- JSON scene playground.

## Exit criteria

A scene looks pixel-identical between emulator and TC002 for supported primitives.

---

# Stage 3 — App engine

## Goal

Turn renderer into a useful clock platform.

## Features

- app registry;
- app ordering;
- carousel;
- per-app duration;
- clock app;
- date app;
- demo weather app;
- next/previous;
- pause;
- enabled/disabled state;
- persisted settings;
- transitions.

## Exit criteria

A complete standalone clock experience runs without external services.

---

# Stage 4 — HTTP API

## Goal

External systems can control NOTRIX.

## Features

- `/api/v1/health`;
- `/api/v1/device`;
- apps API;
- scene API;
- settings API;
- notifications;
- JSON validation;
- API token option;
- request limits;
- OpenAPI schema.

## Exit criteria

A laptop can add/update/delete apps without touching the device UI.

---

# Stage 5 — Notifications and text effects

## Goal

Reach the first major "AWTRIX-like utility" milestone.

## Features

- notification queue;
- priorities;
- timeout;
- dismiss;
- sound;
- marquee;
- auto-scroll;
- bounce;
- progress;
- indicators;
- simple animations.

## Exit criteria

Long text, multiple text elements and notifications behave deterministically.

---

# Stage 6 — MQTT

## Goal

Native smart-home integration.

## Features

- broker settings;
- TLS if feasible;
- auth;
- reconnect strategy;
- status topic;
- availability;
- app update;
- notify;
- button events;
- Home Assistant discovery experiments.

## Exit criteria

NOTRIX can be fully used from MQTT without HTTP.

---

# Stage 7 — Compatibility layer

## Goal

Make existing AWTRIX-oriented integrations easy to migrate.

## Features

Implement selected compatibility endpoints/topics.

Build a contract-test repository of payloads.

Your Domoticz AWTRIX NG integration is an excellent real-world compatibility client.

## Exit criteria

The Domoticz plugin can drive a NOTRIX device with either:

- no changes for supported basics; or
- a small NOTRIX mode.

Document incompatibilities rather than silently accepting unsupported fields.

---

# Stage 8 — Device Web UI

## Goal

No app needed for day-to-day configuration.

## Features

- dashboard;
- apps;
- settings;
- brightness;
- volume;
- Wi-Fi status;
- MQTT;
- button mappings;
- icon manager;
- update page;
- diagnostics;
- reboot.

## Exit criteria

A normal user can configure everything after installation using a browser.

---

# Stage 9 — Public website and documentation

## Goal

Make the project approachable.

## Features

- polished landing page;
- interactive emulator;
- installation guide;
- recovery guide;
- API docs;
- developer docs;
- release notes;
- compatibility matrix;
- screenshots/GIFs;
- FAQ.

## Deployment

GitHub Pages on every merge to `main`.

Preview environments for website PRs if useful.

---

# Stage 10 — Consumer sideload installer

## Goal

One-click-ish installation without FlyThings IDE.

## Features

### CLI

```text
discover
doctor
install
restore
logs
```

### Desktop helper

Windows first, then:

```text
Windows x64
Linux x64/ARM64
macOS x64/ARM64
```

### Web installer

```text
Open website
Install helper
Discover clock
Select
Check compatibility
Install temporary NOTRIX
Open NOTRIX
```

## Exit criteria

A technical-but-non-developer user can install a temporary build in under a few minutes without CLI or IDE.

---

# Stage 11 — Persistent firmware packaging

## Goal

Produce reproducible persistent firmware safely.

## Research

- reverse/document `update.img`;
- inspect official image builder;
- identify image headers;
- identify partitions;
- identify checks/signatures;
- identify update trigger;
- confirm actual upgrade media/interface on retail hardware;
- test rollback;
- test reset factory recovery.

## Strong release gate

Do not put a public **Flash permanently** button online before all safety tests pass.

---

# Stage 12 — Consumer persistent installer

## Goal

Normal users can permanently install NOTRIX.

Flow:

```text
Select device
   ↓
Compatibility check
   ↓
Backup device information
   ↓
Download signed matching release
   ↓
Verify
   ↓
Install
   ↓
Reboot
   ↓
Health check
   ↓
Show success / recovery instructions
```

Provide:

```text
Install stable
Install beta
Restore official firmware
```

If restoring official firmware legally/technically requires the built-in reset mechanism rather than redistributing Ulanzi firmware, direct users to that mechanism.

---

# Stage 13 — OTA updater

## Goal

Updates become boring.

Features:

- update check;
- stable/beta channel;
- release notes;
- download verification;
- staged update;
- post-boot health;
- rollback/recovery;
- downgrade support.

No forced automatic updates initially.

---

# Stage 14 — Integration ecosystem

## Goal

Become a platform rather than only a clock firmware.

Targets:

### Domoticz

First-class plugin support.

### Home Assistant

Potential options:

- MQTT discovery;
- native integration;
- blueprints.

### Node-RED

Example flows / node package later.

### REST

Examples:

```text
curl
PowerShell
Python
JavaScript
C#
```

SDKs are convenience only; REST/MQTT remains canonical.

---

# Stage 15 — Advanced TC002 features

Once the foundation is reliable:

- microphone audio visualizer;
- rotary-driven menus;
- timers;
- stopwatch;
- scoreboard;
- games;
- alarm clock;
- speaker notifications;
- richer graphs;
- 2-line dashboards;
- custom pixel editor;
- animation editor;
- app marketplace/catalog;
- downloadable icon packs;
- local scripting if resources permit.

Avoid adding a scripting VM until memory usage is proven safe.

---

# Stage 16 — 1.0 release

1.0 requires:

- reliable persistent install;
- reliable recovery;
- OTA;
- HTTP;
- MQTT;
- web UI;
- apps;
- notifications;
- scroll;
- fonts;
- simulator;
- automated builds;
- signed releases;
- documentation;
- installation website;
- compatibility matrix;
- multi-week soak testing;
- hardware revision support policy.

---

# 38. Memory budget

Community probes suggest the TC002 may have very limited RAM.

Treat memory as a hard design constraint.

Rules:

- no heavyweight framework on-device unless proven acceptable;
- static allocations where sensible;
- bounded queues;
- bounded HTTP payloads;
- bounded notification count;
- bounded asset size;
- stream uploads;
- avoid duplicated framebuffers;
- avoid unnecessary `std::string` churn;
- track free-memory low watermark.

Create CI/device regression threshold:

```text
boot free memory >= X
normal clock >= Y
10 notifications >= Z
```

Exact thresholds established after Stage 0 profiling.

---

# 39. Performance targets

Initial engineering targets:

```text
boot to clock               < 5 s after app runtime starts
API basic command           < 100 ms LAN processing target
button-to-visible-response  < 100 ms
normal animation            20–30 FPS
static screen               near-zero unnecessary redraw
MQTT reconnect              robust exponential backoff
```

These are project goals, not hardware guarantees, until measured.

---

# 40. Consumer UX

## First boot

```text
NOTRIX
Starting...
Wi-Fi ✓
23:04
```

If Wi-Fi is unavailable:

```text
No Wi-Fi
192...
```

or configuration mode if we can implement one safely.

## Error handling

Never show:

```text
ERR -17 JSON parse fail
```

to a normal user.

Show:

```text
MQTT offline
```

Detailed error is available in diagnostics.

---

# 41. Branding

The project name is:

# NOTRIX

Working interpretation:

> **NOTRIX — definitely not AWTRIX.**

The joke can be part of the community personality, but the primary product branding should remain clean and credible.

Recommended main tagline:

> **Open pixel firmware for the Ulanzi TC002.**

Alternative taglines:

- Open pixels. Your rules.
- Local-first firmware for the TC002.
- Your pixels, your apps, your automations.
- An open platform for the TC002.

Brand hierarchy:

```text
NOTRIX
├── Firmware / runtime
├── Web UI
├── Installer
├── Simulator
├── SDK
└── Integrations
```

Current repository:

```text
github.com/galadril/notrix
```

Possible future organization:

```text
github.com/notrix-os/notrix
```

Package, binary and protocol naming must use `notrix`, never the GitHub owner:

```text
notrix-runtime
notrix-installer
notrix-cli
notrix-simulator
notrix-sdk
```

That keeps a future move from `galadril/notrix` to `notrix-os/notrix` painless.

Release channels may have a little personality without sacrificing clarity:

```text
Nightly  — "Probably Blinks"
Beta     — "Mostly Pixels"
Stable   — normal semantic release names
1.0      — "Definitely NOTRIX"
```

Keep project visuals distinct from AWTRIX and Ulanzi. Do not copy their logos, iconography or product artwork.

README disclaimer:

> NOTRIX is an independent open-source community project and is not affiliated with or endorsed by Ulanzi or AWTRIX. Ulanzi, U-Clock and other product names are trademarks of their respective owners.

---

# 41.1 Repository ownership and future transfer

Start with one repository:

```text
galadril/notrix
```

Do **not** split the project into multiple repositories during the early stages.

Keep these together:

```text
firmware/
installer/
simulator/
web/
sdk/
integrations/
docs/
tooling/
tests/
```

Reasons:

- one issue tracker;
- one pull request can update firmware, API, simulator and docs together;
- Claude Code gets the complete project context;
- one CI/CD setup;
- one release process;
- easier refactoring while architecture is still evolving;
- fewer version-skew problems.

CI and tooling should derive repository ownership from environment variables instead of hard-coded owner names.

Conceptually:

```text
PROJECT_NAME=notrix
REPOSITORY_OWNER=${GITHUB_REPOSITORY_OWNER}
PUBLIC_SITE=https://notrix.dev
```

Avoid embedding `galadril` in:

- firmware identifiers;
- MQTT topics;
- API names;
- package names;
- update manifests;
- persistent device configuration.

It is fine for temporary README links, badges and GitHub URLs.

If the project becomes large enough to justify an organization, transfer:

```text
galadril/notrix
        ↓
notrix-os/notrix
```

Only split repositories when there is a concrete reason such as independent release cadence, permissions or community ownership.

Possible *future* split examples:

```text
notrix-os/notrix
notrix-os/notrix-homeassistant
notrix-os/notrix-hardware
```

But the default remains: **one NOTRIX monorepo**.

---

# 42. Release channels

## Nightly

Every successful `main` build.

For developers only.

## Beta

Manual promotion after tests.

For enthusiasts.

## Stable

Only tested firmware.

Website should make the distinction visually obvious.

---

# 43. GitHub project management

Labels:

```text
area:firmware
area:renderer
area:web
area:installer
area:api
area:mqtt
area:hardware
area:docs
area:integration

type:bug
type:feature
type:research
type:refactor

priority:critical
priority:high
priority:normal
```

Milestones:

```text
0.1 Display
0.2 Apps
0.3 API
0.4 MQTT
0.5 Web UI
0.6 Installer
0.7 Persistent
0.8 OTA
1.0 Stable
```

---

# 44. Claude Code workflow

This document is intentionally suitable as project context for Claude Code.

Recommended approach:

## Never prompt Claude with

> Build the complete firmware.

Instead work stage by stage.

Example:

```text
Read:
- NOTRIX-PROJECT-BLUEPRINT.md
- docs/architecture/*
- docs/adr/*

We are currently implementing Stage 1 only.

Do not implement HTTP, MQTT, installer or OTA.

Task:
Implement a platform-independent 52x16 framebuffer and clipping-safe Canvas
with pixel, line, rect and fillRect primitives.

Requirements:
- no heap allocation during render
- host unit tests
- TC002 adapter behind interface
- preserve existing architecture
- run tests before finishing
```

For every Claude task require:

1. inspect existing code;
2. state assumptions;
3. implement smallest coherent change;
4. add/update tests;
5. run tests;
6. summarize changed files;
7. list risks/TODOs;
8. do not silently widen scope.

## CLAUDE.md

Create root `CLAUDE.md`:

```text
# NOTRIX development rules

Read PROJECT-BLUEPRINT.md before architectural work.

Principles:
- reliability first
- no AWTRIX source copying
- hardware behind interfaces
- simulator must remain supported
- no unbounded allocations/queues
- no persistent flashing logic without explicit task
- tests required for core behavior
- do not edit generated FlyThings files manually
- document reversed platform behavior
- create ADR for significant architectural changes
- keep GPL/third-party notices accurate
```

---

# 45. Suggested first Claude sessions

## Session 1 — Repository bootstrap

Create:

```text
repo skeleton
CMake
Docker build
host test target
firmware skeleton
docs
CLAUDE.md
GitHub Actions
```

No device logic beyond stubs.

## Session 2 — TC002 official demo study

Ask Claude to produce:

```text
docs/research/official-demo-analysis.md
```

Map:

- MCU;
- display;
- keys;
- audio;
- network;
- BLE;
- GPIO;
- build dependencies.

No source copying outside license-reviewed areas.

## Session 3 — Headless toolchain

Reproduce cross compilation.

## Session 4 — Temporary sideload

Automate:

```text
build → deploy → restart → logs → restore
```

## Session 5 — Canvas

Renderer and unit tests.

## Session 6 — Font engine

Bitmap font conversion and text tests.

After this, begin Stage 1 hardware integration.

---

# 46. Research questions that must remain explicit

Do not hide unknowns.

Current important questions:

1. Which retail TC002 hardware revisions exist?
2. Are all revisions SSD21x-compatible?
3. Is Wi-Fi ADB enabled on all firmware versions?
4. Is the `/tmp` override reliable on all supported versions?
5. Exactly how does persistent `update.img` packaging work?
6. What persistent upgrade path exists on physical retail units?
7. Official docs mention TF-card flashing while community probing reports hardware where the TF path is not usable; what applies to each revision?
8. What is the exact factory reset/recovery behavior?
9. Can the full web server/API safely run within device memory limits?
10. How much RAM remains after our renderer/network stack?
11. Which FlyThings libraries may legally be redistributed in binary build containers/releases?
12. Can CI legally cache/package the proprietary FlyThings SDK components?
13. Can we create a completely reproducible toolchain from publicly downloadable packages?
14. Is there an existing update daemon/API suitable for safe OTA?
15. Are firmware images signed/validated?

Each answer should become documentation or an ADR.

---

# 47. Definition of "consumer friendly"

A release is not consumer friendly because it has a GUI.

It is consumer friendly when:

- no command line required;
- no IDE required;
- no compiler required;
- detects incompatible devices before modification;
- explains what will happen;
- has visible progress;
- handles temporary network failures;
- can recover;
- provides a clear factory restore path;
- updates are verified;
- errors contain a human-readable explanation;
- diagnostics can be downloaded for GitHub issues;
- documentation contains screenshots;
- stable releases are clearly separated from experimental builds.

---

# 48. Minimum viable public release

I would make the first public release **non-persistent**.

## NOTRIX 0.1-alpha

Contains:

```text
52×16 renderer
clock
text
degree symbol
scrolling
app rotation
basic HTTP API
notifications
temporary ADB installer
Windows helper
emulator
documentation
```

Why?

It lets people try NOTRIX without permanently replacing the Ulanzi firmware.

Power-cycle recovery makes experimentation much safer.

Real-world feedback arrives before persistent flashing risk is introduced.

---

# 49. First persistent public release

## NOTRIX 0.5-beta

Only after Stage 11 qualification.

Contains:

```text
persistent installation
factory recovery guide
firmware updater
web UI
HTTP
MQTT
compatibility mode
diagnostics
signed/checksummed releases
```

---

# 50. 1.0 product story

The eventual README opening could be:

> **NOTRIX is an open-source firmware platform for the Ulanzi TC002.**
>
> It replaces the limited stock pixel-app experience with a fast 52×16 renderer,
> smooth scrolling, deterministic custom apps, notifications, MQTT, HTTP,
> integrations, a built-in web interface and safe browser-assisted installation.
>
> No cloud required.

---

# 51. Source references / starting points

Official Ulanzi TC002 project:

- https://github.com/UlanziTechnology/Ulanzi-U-Clock-TC002

Official TC002 documentation:

- https://docs.ulanzistudio.com/tc002/en/

Useful community research demonstrating real-device probing, headless ARM builds and `/tmp` sideloading:

- https://github.com/qzz0518/ulanzi-tc002-market-clock
- https://github.com/qzz0518/ulanzi-tc002-market-clock/blob/main/docs/research/flythings-build-path.md
- https://github.com/qzz0518/ulanzi-tc002-market-clock/blob/main/docs/research/tc002-device-probe.md

These are research inputs, not specifications. Verify critical hardware, update and recovery behavior on our own devices before release.

---

# 52. Immediate next actions

When a TC002 is available:

```text
[ ] Create `galadril/notrix` GitHub repository
[ ] Commit this blueprint
[ ] Add GPL-3.0-or-later license
[ ] Add CLAUDE.md
[ ] Add ADR skeleton
[ ] Clone/archive official reference repo separately
[ ] Connect TC002 to isolated/test Wi-Fi
[ ] Record stock firmware + MCU versions
[ ] Verify Wi-Fi ADB
[ ] Run device probe
[ ] Reproduce temporary /tmp sideload
[ ] Power-cycle and verify restoration
[ ] Reproduce Docker cross-compile
[ ] Render test pattern using sendLedData
[ ] Build first Canvas unit tests
[ ] Set up GitHub Actions
[ ] Deploy documentation skeleton to GitHub Pages
```

The key first milestone is deliberately small:

> **A commit pushed to GitHub automatically produces a tested ARM artifact, and one developer command temporarily deploys it to a TC002 where it renders our own 52×16 framebuffer.**

Once that is boring and repeatable, build the product on top of it.

---

# 53. Recommended architectural boundary

The most important boundary in the entire project:

```text
                        NOTRIX CORE
┌─────────────────────────────────────────────────────────┐
│ Apps / Notifications / Scheduler / API / MQTT          │
│                         ↓                               │
│ Scene Model → Layout → Renderer → Framebuffer           │
└─────────────────────────┬───────────────────────────────┘
                          │
                   IPlatformServices
                          │
             ┌────────────┴────────────┐
             ↓                         ↓
      TC002 FlyThings             Simulator
      adapter                     adapter
             │                         │
     MCU / LED / audio              browser
     network / storage              desktop
```

If this boundary stays clean, we can:

- test almost everything without hardware;
- survive changes in Ulanzi/FlyThings internals;
- potentially support another 52×16 device later;
- improve the UI without touching device drivers;
- let Claude make larger changes without entangling platform-specific code everywhere.

That is the foundation that makes the rest of the roadmap realistic.
