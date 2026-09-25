# Security

Stipple is firmware that listens on your network. This page says what it
protects, what it does not, and how to tell us when something is wrong.

## Reporting a vulnerability

**Use [GitHub's private vulnerability reporting](https://github.com/galadril/Stipple/security/advisories/new)**
rather than a public issue, so there is time to fix it before it is described
in the open.

Please include what you found, how to reproduce it, and what an attacker
gets. If you are not sure whether something counts, report it — deciding that
is our job, not yours.

**What to expect.** This is a hobby project maintained in spare time. There is
no service-level agreement and no bounty. Reports are read and taken
seriously, and you will get a reply, but "quickly" means days or weeks rather
than hours. Please say if you have a disclosure deadline in mind.

## The threat model, honestly

Stipple assumes **your local network is trusted**. That assumption is doing a
lot of work, and you should know exactly how much.

### What it does protect against

- **Other people on your LAN**, once you set an access password. Without one,
  anyone who can reach the device can reconfigure it.
- **Malformed input**, to the extent hand-written tests reach. The HTTP
  parser, JSON parser, DHCP client, SNTP client and MQTT client all read
  untrusted bytes, each has tests for truncated, oversized and nonsense
  input, and the whole suite runs under ASan and UBSan in CI. Request bodies,
  queues, icon storage and app counts are all bounded — nothing grows without
  a limit.

  **There is no fuzzing.** That is a real gap rather than an oversight in
  this description, and a fuzzing harness would be a genuinely valuable
  contribution.
- **Bricking itself.** A firmware that will not load falls back to the copy
  flashed beside it, and then to the stock clock, rather than to nothing.

### What it does not protect against

Stated plainly, because a security page that only lists wins is not one.

- **There is no TLS.** The web UI and API are plain HTTP. The access password
  is sent in the clear over your network, and so is everything else. Anyone
  who can see your LAN traffic can see and replay it.
- **Secrets are stored in the clear on the device.** The access password, the
  MQTT password and your Wi-Fi key are readable by anyone who can get a shell
  or read the flash. The device has no secure element and no key storage.
- **Physical access is total access.** Holding − and + for five seconds
  clears the access password by design — it is the way back in for somebody
  locked out of their own clock. That same gesture is available to anyone
  standing in front of it.
- **The setup access point is open.** While hosting `Stipple-setup` there is
  no password on it, because you need to reach it before you have configured
  anything. Anyone in radio range during that window can connect and send
  Wi-Fi credentials to the device. It reverts on a timer for exactly this
  reason.
- **MQTT is as secure as your broker.** Stipple will use TLS if the broker
  offers it, but the credentials still live in the clear on the device.

**Do not put this on a network you do not control**, and do not treat the
access password as protection from anything more determined than a housemate.

## The browser emulator

The emulator on the website runs the real firmware compiled to WebAssembly.

**It stores nothing and sends nothing.** There is no `localStorage`, no
cookie and no IndexedDB anywhere in it; the simulator's storage is a
`std::map` in WASM heap memory that is gone on reload. MQTT is an in-memory
broker of one, so no socket is opened to anything. The page makes no network
request after it has loaded.

**The device's configuration page is deliberately not embedded there.** It
used to be, and it worked — but it is a faithful copy of a real settings
screen, with fields for a Wi-Fi key, a broker password and an access
password. Nothing it collected went anywhere, and that was never quite the
point: a convincing form invites a real password, and real passwords end up
in screenshots. The panel, the controls and the API are demonstrable without
it.

One rule follows, worth stating rather than leaving as a happy accident:
**the emulator must never persist anything.** GitHub Pages user sites share a
single origin — `galadril.github.io` is the same origin for every project
published there — so anything written to browser storage by one page is
readable by all of them. Nothing persists today, which is exactly why that is
harmless. Adding storage would change it, quietly.

## Scope

**In scope:** anything in this repository — the firmware, the web UI, the API,
the tooling, the build.

**Out of scope:**

- Ulanzi's own firmware, bootloader and kernel. Stipple replaces the
  application only. Report those to Ulanzi.
- Anything that requires physical access to the device, which is documented
  above as total access by design.
- The absence of TLS. It is known, it is listed above, and a report saying
  "the API is unencrypted" tells us nothing new. A *proposal* for how to do
  TLS on 36 MB of RAM without a dependency is very welcome — as an issue,
  not an advisory.

## Supported versions

The latest release, and `main`. This is a 0.x project with one maintainer;
there are no backports to older versions.

## If you are worried about what is in a release

Every release publishes a SHA256 for each artifact and a `manifest.json` with
the commit it was built from. The device library is built in a pinned
container in CI, and CI refuses to publish one that could not load on the
target hardware.

No restore image or partition capture is ever published — those are Ulanzi's
firmware and specific to a single unit.
