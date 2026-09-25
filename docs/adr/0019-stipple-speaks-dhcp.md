# 0019 — STIPPLE speaks DHCP, because nothing else here can

- **Status:** Accepted; the client is built, tested and running on hardware
- **Date:** 2026-09-22

## Context

There is no DHCP client on the TC002. The whole of `/bin`:

```
adbd busybox cat chmod chown cp date df dnsmasq echo fsync getevent getprop
hostapd kill ln logcat logd ls mkdir mknod mksh mount mv ping ps pwd reboot
rm rmdir setprop sh ssd_init.sh sync test_fb touch umount vold wpa_supplicant
zkdaemon zkdisplay zkgui
```

No `udhcpc`, no `dhcpcd`, no `dhclient`, and this busybox has no applets at
all — `busybox --list` answers "applet not found". `init.rc` creates
`/data/misc/dhcp` and nothing ever writes there. `wpa_supplicant` runs as an
init service and only ever *associates*; the address appears when `zkgui`
starts. **The vendor application obtains the lease in-process.**

This was found by testing the hotspot (ADR 0018), which came up, served no
addresses, and could not hand the radio back — because putting
`wpa_supplicant` back gets you an association and no address.

But the hotspot is the small half of it.

**STIPPLE has never held a lease.** Every session has run on an address the
vendor application obtained *before* STIPPLE started: each one begins with
`setprop ctl.stop zkswe` on a device that is already online. The address stays
configured because nothing removes it, and nothing renews it either. The
lease this device is issued is **86400 seconds**. A STIPPLE device left alone
loses its network after a day, and nobody has seen it because no unit has run
that long without a restart.

So this is not a provisioning feature. It is a thing STIPPLE needs in order to
be the application on this device at all.

## Decision

### Write one, because there is nothing to call

Not a wrapper around a binary, not a shell-out. There is no binary. Roughly
600 lines including the framing, which is less than the effort already spent
working out why the hotspot could not come back.

### The protocol goes in the core, the sockets stay in the adapter

`stipple::net::dhcp` holds the message format, the state machine and the
IP/UDP framing. `Tc002Dhcp` holds the sockets, the ioctls and the routing
table. The §53 line falls exactly where it always does, and it is worth
saying why it matters *here* specifically:

**everything that can be wrong about DHCP is a timer or a checksum**, and
neither can be tested from beside the socket that produced it. A renewal that
fires at the wrong fraction of the lease looks perfect for twelve hours. A
wrong checksum is not an error — it is a packet something else on the network
drops, which is indistinguishable from a server that did not answer. Both are
half-day bugs on hardware and half-second tests on a host.

38 tests, including one that runs the client for a simulated day.

### Two sockets, because one cannot do both jobs

A **packet socket** (`AF_PACKET`, `SOCK_DGRAM`) carries the broadcasts. A
client asking for its first address has no address: there is nothing to put in
a UDP socket's source field and no route to 255.255.255.255, so the IP and UDP
headers are built by hand. Confirmed present on this kernel —
`/proc/net/packet` shows the socket with proto `0800` on `wlan0`.

A **UDP socket** on port 68 carries the renewals, which are unicast to a
server we can already reach by the time they happen.

A BPF filter narrows the packet socket to UDP port 68. Without it the socket
sees every frame on the interface, and on a busy network the reply we are
waiting for is the one dropped when the buffer fills.

Where `AF_PACKET` is missing, broadcasts fall back to the UDP socket. That
works whenever the interface already has an address — the common case, and
never the first-boot one. The fallback is reported rather than hidden: it is
the difference between a client that can bootstrap from nothing and one that
cannot.

### It asks for the address it already has

Option 50, seeded from whatever is configured on the interface. On this device
that is the address the vendor application obtained, and getting it back is
what makes the handover invisible to everything else on the network. Confirmed
on hardware: `192.168.1.238` in, `192.168.1.238` out.

Option 61 carries the MAC as a client identifier, so the address does not
wander between reboots for reasons nobody can see.

### It never gives up

Retries back off from four seconds and stop at a minute apart. A client that
stops asking is a device nobody can reach, and nobody is standing next to a
clock waiting to restart it. A minute is often enough to catch a router that
rebooted and rare enough to be invisible.

### An expired lease means the address comes off

This cuts every open connection, including whichever one is reading the log
line that says so. It is still right: keeping an expired lease is how two
devices end up sharing one address, and the second to notice is the one that
stops working. A device that is quietly wrong on the network is worse than one
that is visibly restarting.

### There is an observe mode, and it is not decoration

`--dhcp-observe` runs the entire conversation and writes nothing to the
interface. The first run of this code on a real device happens over the very
network it is negotiating; a lease that came back with a different address
would cut the connection mid-test. Observing costs one run and answers the
only question that matters.

### The lease is reported, and its absence is reported louder

`NetworkStatus` grows `leaseKnown`, `leaseSeconds` and `leaseState`, shown on
the web UI. **`leaseKnown` false does not mean "no address"** — it means
nothing is renewing one, which is a real and different state. A device
running on an inherited address looks identical to a healthy one right up
until the address is taken back, and then nobody can reach it to find out
why. Reporting that as an ordinary connection would hide the one fault that
cannot be diagnosed afterwards. Same reasoning as ADR 0013.

## Consequences

**The hotspot can now hand the radio back**, which was the blocker. Restoring
the station means restoring `wpa_supplicant` *and* asking for an address.

**Two things depend on this that did not obviously need it.** Joining a
network cannot be confirmed without an address to wait for — ADR 0018 says a
join is only kept once it produced one, and until now nothing could produce
one. And first boot on an unprovisioned device has no inherited address to
run on at all.

**Renewal is unproven.** The live test bound and held; T1 on an 86400-second
lease is twelve hours away. The timing is tested on a host, the wire format
is tested against a real server, and the two have not yet been tested
together. That is stated rather than assumed.

**Running as root is now load-bearing.** `AF_PACKET` and `SIOCSIFADDR` both
need it. STIPPLE already runs as root on this device, so nothing changes, but
it is now a requirement rather than a convenience.

## Alternatives considered

**Ship a `udhcpc` binary.** Cross-compiling busybox for this device is real
work, it is another binary in the image with its own update story, and it
would mean parsing a script's output to find out what happened. Writing the
client is smaller and the result is testable.

**Static addressing, configured by the user.** Simpler, and wrong for a
consumer clock: it requires the user to know their subnet, and a static
address that collides is a support problem nobody can debug from the device.
Worth offering *as well*, later; not instead.

**Ask `wpa_supplicant` to do it.** It does not. It associates, and that is
deliberately all it does.

**Leave it, and accept a daily reconnect.** This is what happens today, and
the reason it looks survivable is that no device has run long enough to show
it. It is also silent: the clock keeps showing the time, so the first symptom
is that the web UI stopped answering some time yesterday.
