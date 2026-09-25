# MQTT

Optional, and off by default. A Stipple device is fully usable with no broker at
all, and fully usable from MQTT with no HTTP client (blueprint §20). It will
never connect to a broker nobody configured.

## Topics

`{baseTopic}/{deviceId}/...`, where `baseTopic` defaults to `stipple` and
`deviceId` is derived from the device name — lower-cased, with anything that is
not a letter or digit folded to `-`. "Kitchen Clock" becomes `kitchen-clock`.

| Topic | Direction | Retained | Meaning |
|---|---|---|---|
| `availability` | out | yes | `online` / `offline` |
| `status` | out | yes | device state, JSON |
| `button` | out | no | a control was used |
| `cmd/...` | in | — | commands, below |
| `result/...` | out | no | the answer to a command |

Availability is backed by a **last will**, so a device that loses power is marked
offline by the broker rather than lying `online` until a keepalive expires.

Results are published under `result/` rather than inside `cmd/` on purpose: the
device subscribes to `cmd/#`, so replying there would echo every answer back to
itself.

## Commands

Each command is one call into the same `/api/v1/*` handler that HTTP uses. The
payload is the HTTP request body, and the reply carries the HTTP status.

| Topic | Becomes |
|---|---|
| `cmd/notify` | `POST /api/v1/notifications` |
| `cmd/settings` | `PATCH /api/v1/settings` |
| `cmd/apps` | `POST /api/v1/apps` |
| `cmd/apps/{id}` | `PATCH /api/v1/apps/{id}`, or `DELETE` if the payload is empty |
| `cmd/apps/{id}/activate` | `POST /api/v1/apps/{id}/activate` |
| `cmd/reboot` | `POST /api/v1/system/reboot` |

An empty payload on `cmd/apps/{id}` deletes, because publishing an empty retained
message is how MQTT conventionally says "this is gone".

```bash
mosquitto_pub -t 'stipple/kitchen-clock/cmd/notify' -m '{"text":"Dinner"}'
mosquitto_pub -t 'stipple/kitchen-clock/cmd/settings' -m '{"display":{"power":false}}'
```

**Anything HTTP refuses, MQTT refuses identically** — the routing happens before
any handler, so validation, limits and error shapes cannot drift between the two.
A command MQTT could express but HTTP could not would be a bug.

Unknown commands are *reported*, never guessed at: the counter in
`/api/v1/diagnostics` goes up and a line lands in the log. A typo'd topic is
otherwise indistinguishable from a broken device.

## Reconnection

Exponential backoff from 1 s, doubling, capped at 60 s. The cap matters more than
the rate: without one, a device that was offline overnight would take hours to
notice the broker came back.

On reconnect it republishes availability and status, both retained, so a
subscriber that missed the outage still ends up with the truth.

## Credentials

The broker password is stored — a device has to reconnect unattended — and that
is the **only** place it appears.

- `GET /api/v1/settings` returns `passwordSet: true|false`, never the value, and
  no masked placeholder that a client might helpfully save back.
- `PATCH` accepts it. An empty string clears it; that is the only way to remove
  one through the API.
- It never reaches the log, diagnostics, the status topic or any other published
  payload. `MqttService.TheBrokerPasswordNeverLeavesTheDevice` and the emulator's
  `verify` harness both assert this against every message the device sends.

TLS is a **request, not a guarantee**: `mqtt.tls` asks the transport for it, and
an adapter that cannot provide it must refuse to connect rather than quietly
sending credentials in the clear. Whether the TC002 can do TLS at all is a §46
unknown — a third-party port bundles OpenSSL, which suggests yes at the cost of
carrying the library. See `docs/research/tc002-platform-findings.md`.

## What is not built

- **No transport.** `IPlatformServices::mqtt()` returns nullptr on every real
  adapter; only the simulator implements `IMqttClient`. Everything above is
  exercised against an in-memory broker. The device implementation arrives with
  Phase 7, and nothing above the platform boundary changes when it does.
- **No Home Assistant discovery.** `mqtt.discovery` is stored and does nothing
  yet. It is a setting rather than a promise; the documents it would publish need
  a real broker and a real Home Assistant to test against.
- **QoS 2.** Deliberately not offered. It costs a four-way handshake and
  per-message state on a device with an unmeasured RAM budget, to solve a problem
  this product does not have: a duplicated "show a notification" is a much
  smaller harm than a stalled session.
