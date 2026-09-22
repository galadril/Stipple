# 0018 — Getting on the network, and keeping people off it

- **Status:** Implemented; amended twice after live testing
- **Date:** 2026-09-22

## Context

Three things are missing and they are the same thing: a device that has just
come out of a box has no network, no password, and nobody standing in front of
it who knows what it is for.

**There is no way to join a network.** Wi-Fi credentials are whatever the stock
firmware was left holding. Every NOTRIX device therefore depends on the vendor
application having been configured first, which is not a product — it is a
development shortcut that has not been noticed because every unit so far was
already on a network when NOTRIX first ran on it.

**There is no way to keep anyone out.** `ApiOptions::authToken` exists and is
never set from configuration, so in practice the API is open to everything on
the LAN. That is a defensible default for a clock — it is also a printer's worth
of advice from 2003, and it means anyone on the guest Wi-Fi can rewrite the
panel, read the log and reboot the device.

**There is no first run.** The device boots into a clock showing the wrong time,
and nothing tells a new owner where its configuration page is or that one
exists.

The pieces needed are already here: `hostapd` and `dnsmasq` are on the device,
the navigation model gives us physical controls with defined meanings, the panel
can display text, and the web UI exists.

## The thing that makes this different

Everything built so far has been safe to get wrong. A bad overlay is ugly; a bad
timezone is an hour out; a crashed process is a `/tmp` binary and a power cycle
away from the stock firmware.

**These three are not.** Wi-Fi credentials live in `/data`, which survives a
reboot. So does configuration. A device that is misconfigured into not joining a
network, or locked behind a password nobody has, is a device with no remaining
route in: USB-C on the TC002 is mass storage, there is no serial header exposed,
and ADB arrives over the very network that just broke.

So the ordering principle for this work is not "what is most useful first". It
is **nothing ships without a way back that does not need the network.**

## Decision

### 1. A physical escape hatch, before anything else

Hold **minus and plus together for five seconds**. The panel counts down, and at
zero the device clears its access password and starts its hotspot.

It does not clear anything else. This is a way back in, not a factory reset —
somebody locked out of a clock wants their apps and their settings to still be
there when they get back in.

Five seconds and two buttons because neither is reachable by accident, and the
countdown means a hand resting on the case cannot do it silently.

This is built and tested first, because it is what makes the rest safe to build.

### 2. Provisioning: add, never replace

The device does not rewrite `wpa_supplicant.conf`. It **appends a network
block** and gives it a higher priority than the ones already there.

A wrong password therefore fails back to the network that was already working,
rather than stranding the device. That single property is worth more than
anything else in this section, and it is why joining is not implemented as
"write the file the user asked for".

A join is confirmed before it is kept: the device tries, waits for an address,
and on failure removes the block it added and says so. Only a join that produced
an address is written down.

No `wpa_cli` exists on this hardware, so the daemon is signalled directly and the
result is read from the interface rather than from a tool's opinion of it.

### 3. Scanning is read-only and therefore first

Listing networks touches nothing. It ships ahead of joining, is useful on its
own, and exercises the plumbing — the interface name, the ioctl path, the
parsing — while the failure mode is still "an empty list".

### 4. The hotspot is what a device does when it has nowhere to go

No stored network, or stored networks that will not join within a timeout: start
`hostapd` and `dnsmasq`, show the hotspot name and the address on the panel, and
serve the existing configuration page on it.

It is not a separate mode with its own UI. It is the same web UI, reachable
from a different network, which means there is one page to maintain and one to
get right.

The hotspot stops as soon as a real network is joined. A device that keeps an
open access point running forever is a worse security problem than the one this
ADR is trying to fix.

### 5. Access control is HTTP Basic, and it covers everything

One mechanism for the API and the browser, because they are the same server and
two schemes would mean two things to get wrong. A browser prompts for it
natively; `curl -u` and every automation library already speak it.

Off by default, because a device that demanded a password before showing a clock
would be a worse first five minutes than the risk it removes — and because the
first run (below) is where it gets offered, rather than being something to
discover.

**The password is never returned by the API**, exactly like the MQTT password.
That also keeps it out of backups, which are taken from the API: a settings file
in somebody's downloads folder should not be a credential.

It is stored on the device in the clear, and that is worth stating plainly
rather than implying otherwise. Anyone with ADB can read it, but anyone with ADB
can already replace the firmware — the threat this defends against is the rest
of the LAN, not somebody holding the device. Hashing it would be better and is a
later increment; it is not a reason to ship nothing.

### 6. First run is a state, not a wizard

On a device with no stored configuration the panel says what it is, that it has
no network, and how to reach it — the hotspot name, then the address, in
rotation. It is the clock's own display doing the explaining, because that is
the only screen a new owner is guaranteed to be looking at.

The web UI, on a device in that state, opens on the network step rather than on
the live view. Not a modal, not a sequence of screens to click through: the same
page, with the thing that matters first.

## Consequences

**`INetworkManager` grows, and it is the first optional capability that can
fail slowly.** Scanning and joining take seconds. Neither may block the render
loop (§16), so both are started and polled, like the MCU and the transports.

**Two new failure modes worth designing for.** A join that half-works — an
address on an unusable network — and a hotspot that will not start. Both must
leave the escape hatch working.

**Testing joining on real hardware risks the link to it.** The escape hatch
makes that recoverable rather than fatal, which is the whole reason it comes
first.

**The order is fixed:** escape hatch, then scan, then hotspot, then join, then
auth, then first run. Each step is usable alone, and every step after the first
has a way back.

## Alternatives considered

**WPS.** One button, no typing. It is deprecated, widely disabled, and its
common implementation is broken by design; shipping it as the primary path would
be shipping a known vulnerability as a feature.

**Bluetooth provisioning**, as a phone app does it. There is no evidence of a
usable Bluetooth stack on this hardware, and a phone app is a second product.

**A token in the URL instead of Basic auth.** Simpler to implement and worse in
every other way: it ends up in browser history, in logs, and in whatever
somebody pastes into a chat window asking why their clock is broken.

**Replacing `wpa_supplicant.conf` wholesale.** Simpler, and one typo from a
device nobody can reach. Rejected on that alone.

## Amended after testing the hotspot on hardware

The hotspot came up — the access point broadcast and was visible — and the
test still failed twice over. Both failures were worth more than the feature.

**No client could get an address.** `dnsmasq` never ran at all — and the
cause turned out to be two missing directories rather than anything about
Wi-Fi. **This device has no `/var`.** dnsmasq refuses to start when it cannot
create its lease file or its pid file, and both default to somewhere
underneath it:

```
dnsmasq: cannot open or create lease file /var/lib/misc/dnsmasq.leases: No such file or directory
dnsmasq: failed to open pidfile /var/run/dnsmasq.pid: No such file or directory
```

Two separate failures, and fixing only the first gets you the second. With
the lease file in `/tmp` and the pid file disabled it starts clean and stays
up. Checked on the device, not reasoned about.

hostapd kept running throughout, which is exactly why this was invisible: the
access point looked perfectly healthy to anyone standing in front of it.

**The revert did not restore the network.** The access point stopped and
`wpa_supplicant` came back, and the device stayed unreachable until it was
power-cycled — the exact "no access point and no station" state this ADR set
out to make impossible.

The cause is the same for both, and it is not in this code: **there is no DHCP
client on the device.** Not in `/bin`, not as a busybox applet — busybox here
has no applets at all. The vendor application obtains the lease itself, which
is why every NOTRIX session so far has had an address: each one began by
stopping a vendor application that had already got one.

That makes a DHCP client a prerequisite rather than a detail, and it is larger
than the hotspot:

**NOTRIX does not hold its own lease today.** It inherits one and never renews
it. A device left running long enough will lose its network, and no unit has
been up for a full lease period without a restart, so nobody has seen it.

### The order changes

1. ~~Physical escape hatch~~ — done.
2. ~~Scanning~~ — done.
3. ~~A DHCP client~~ — done, and running on hardware. See
   [ADR 0019](0019-notrix-speaks-dhcp.md). It turned out to be needed by
   NOTRIX as the application on this device regardless of provisioning: the
   lease this device is issued is 86400 seconds, and nothing was renewing it.
4. Hotspot — reverts by restoring the station *and asking for an address*.
5. ~~Joining~~ — done, and proven live: hotspot, configuration page, network
   chosen, device joined and came back on the LAN.
6. ~~Access control~~ — done. HTTP Basic, off by default, covering the page
   and the API through one gate in `ApplicationHost::handle`.
7. ~~First run~~ — done.

**All seven are built.** Every one of them has run on real hardware except
the first-run state itself, which needs a device with nothing stored.

### And a rule about testing this

`/tmp` is tmpfs, so a power cycle takes the logs with it. The first test left
nothing to read: the dnsmasq log, the generated configs and the evidence of
what failed were all gone before the device came back. Anything diagnosing a
network failure has to write where a power cycle cannot reach, or report over
a channel that does not depend on the network it is breaking — the panel is
the obvious one, and it is right there.
