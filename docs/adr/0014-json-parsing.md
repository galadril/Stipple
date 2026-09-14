# 0014 — In-house bounded JSON parser

- **Status:** Accepted
- **Date:** 2026-09-11

## Context

Scenes (§11), configuration (§21) and the HTTP API (§19) are all JSON. From
Phase 5 onward that JSON arrives over the network from anything on the LAN, so
the parser is the project's primary attack surface and its primary
denial-of-service surface.

[ADR 0012](0012-dependency-free-core.md) made the core dependency-free but
explicitly flagged JSON as the one case where a library might win, precisely
*because* the input is untrusted. Hand-rolling a parser for hostile input is
where bugs become vulnerabilities.

Weighing against that:

- Blueprint §38 makes bounded memory a hard requirement. The parser must cap
  input size, nesting depth and element count, and must not allocate during
  parsing. General-purpose libraries optimise for convenience and typically
  allocate per node; few enforce a depth limit at all, and unbounded nesting is
  the classic JSON stack-overflow DoS.
- The device build has to cross-compile to ARMv7 inside a FlyThings toolchain we
  have not yet reproduced (§46). Every dependency is a risk there.
- JSON's grammar is genuinely small and completely specified by RFC 8259.

## Decision

Write the parser in-house, designed around the bounds rather than having them
bolted on.

**Memory model.** The caller supplies the token array. Parsing allocates
nothing — not one byte — so the worst case is known at compile time and a
hostile payload cannot grow the heap. Tokens reference offsets into the caller's
buffer; no string is copied until something asks for it.

**Bounds, enforced in the parser rather than checked afterwards:**

| Limit | Default | Prevents |
|---|---|---|
| input bytes | caller's choice | oversized payloads |
| token count | caller's array size | element-count blowup |
| nesting depth | 32 | stack exhaustion via `[[[[...` |

**Strict by default.** No trailing commas, no comments, no single quotes, no
`NaN`/`Infinity`, no leading `+`, no leading zeros, no unescaped control
characters, no `.5` or `5.`. Malformed input is rejected with a specific error
rather than guessed at. Being liberal in what we accept would mean the device
and every client library disagree about what a scene means.

**Tested adversarially.** Deep nesting, truncation at every byte offset,
unterminated strings and arrays, bad escapes, lone surrogates, numeric edge
cases, and a sweep that feeds every prefix of a valid document to confirm the
parser always terminates and never reads out of bounds.

## Consequences

**Good**

- Zero allocation, hard bounds, and a worst case that can be reasoned about
  before the payload arrives.
- Still no dependency to pin, licence, or cross-compile.
- Rejecting rather than guessing keeps scene semantics identical across every
  client.

**Bad**

- This is security-sensitive code we own. A parser bug is our bug, and "it is
  well tested" is a weaker guarantee than "it has been deployed a billion
  times". This is the real cost of the decision and it is not hypothetical.
- No streaming: a document must be in memory to be parsed. Fine for bounded
  payloads, and the bound exists anyway.
- Numbers are parsed to `double` and to `std::int64_t`. Values beyond the exact
  integer range of a double are reported as out of range rather than silently
  rounded.

**Revisit when**

- Fuzzing (§23) finds defects faster than they can be fixed, which would mean
  the in-house parser is not converging.
- A dependency is taken on for another reason and already contains a hardened
  JSON implementation, making the marginal cost of using it roughly zero.
