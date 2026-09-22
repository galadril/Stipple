# 0021 — NOTRIX is the startup library, and it lives in /data

- **Status:** Accepted; the hook is proven on hardware, nothing has been flashed
- **Date:** 2026-09-22

## Context

[ADR 0020](0020-persistence-through-the-vendor-update-path.md) settled *how*
to write the `res` partition and left the real question open: what goes in
it. Blueprint §7.1 said NOTRIX becomes `/res/lib/libzkgui.so`, the 7.5 MB
application the EasyUI framework loads, which implied implementing against a
vendor C++ framework — the thing the §53 boundary exists to prevent.

Two routes were measured on hardware. Both are recorded in
`docs/research/tc002-platform-findings.md`; the short version is that the
obvious ones are worse than they look.

**Shadowing `libeasyui.so` works and then does not.** `/bin/zkgui` imports
exactly four symbols from it, and `/res/lib` precedes `/lib` on the library
path, so an 8 KB shim exporting those four is picked up. It then fails one
library further along, because `libzkupgrade.so` wants `Json::Value` — which
`libeasyui.so` also happens to export. The real contract is **74 symbols**,
including `Json::Value`, `Thread`, `Mutex`, `Condition` and
`MessageQueue`: C++ classes whose *memory layout* other libraries were
compiled against. ABI compatibility, not API compatibility. Rejected.

**Replacing the application library means satisfying obfuscated entry
points** — or appears to. `EasyUIContext::initLib()` `dlopen`s the app and
then `dlsym`s three functions whose names are stored in `.data` as
high-entropy byte runs, decoded at runtime.

## Decision

### Take over in a static constructor, and never reach the entry points

`dlopen` runs a library's constructors before the caller can look up
anything. A replacement whose constructor takes over the process never has
to satisfy those three symbols, whatever they are called.

**Proven on hardware.** A 7.6 KB library was loaded by the real framework
and its constructor ran:

```
[11586] static constructor ran - dlopen reached us
```

with `/proc/<pid>/maps` showing `/tmp/libnotrix.so` mapped and
`/res/lib/libzkgui.so` absent. The vendor application was replaced outright
by something a hundredth its size, and `/bin/zkgui` carried on regardless.

No vendor ABI. No obfuscated names. Four kilobytes of decisions instead of
seventy-four symbols.

### The path comes from one line of JSON

`/res/etc/EasyUI.cfg`, read by `ConfigManager::getStartupLibPath()`:

```json
"startupLibPath": "/res/lib/libzkgui.so"
```

Plain text on the read-only squashfs. **That single field is the whole
integration surface**, and it is what gets changed rather than the 7.5 MB
library beside it.

### Point it at `/data`, not at `/res`

```json
"startupLibPath": "/data/notrix/libnotrix.so"
```

This is the decision that matters, and it follows from where the writable
storage is.

**Flash once, update forever.** `/data` is 8 MiB of jffs2 and is the only
writable persistent filesystem. Putting NOTRIX there means the `res`
partition is written exactly once, to change one JSON field, and every
NOTRIX release after that is a file copy over the network. No flashing, no
`update.img`, no reset-button gamble for a routine update.

**The vendor application stays where it is.** Nothing overwrites
`/res/lib/libzkgui.so`, so the stock experience is intact and one config
field away.

**And the failure mode is the good one.** If `/data/notrix/libnotrix.so` is
missing or will not load, `initLib` logs the `dlerror` and carries on with a
null handle — the device boots, `init` runs, `adbd` starts, the network comes
up. A NOTRIX that fails to load leaves a reachable device rather than a
brick. That is worth more than it sounds: it means a bad NOTRIX build is
recovered by copying a file, not by holding a button.

## Consequences

**The `res` change is one line, which makes it reviewable.** A diff nobody
can read is a diff nobody can check; this one is a single JSON field, and
the image is built by swapping that file and repacking with `imgtool`.

**Rebuilding the squashfs needs tooling this project does not have yet.** It
is squashfs 4.0, xz, 128 KiB blocks, 234 inodes. `squashfs-tools` has to go
into the pinned container — a host build tool, not a firmware dependency,
and it needs pinning like everything else there.

**Testing this required no flash at all**, which is worth keeping. The whole
hook was proven by bind-mounting a modified `EasyUI.cfg` over the read-only
one and pointing it at `/tmp` — `mount -o bind` works on this squashfs, and
a power cycle undoes everything. Any future change to the startup path
should be tried that way before it is written to flash.

**ADR 0008's gates are unchanged.** The hook being proven does not
demonstrate a restore. What *has* changed is that a restore is now less
likely to be needed: the failure mode is a device that boots without an
application rather than one that does not boot.

**§53 survives.** NOTRIX does not implement a vendor interface; it is
`dlopen`ed and takes the process. The TC002 adapter grows a shared-library
entry point and nothing above the boundary knows.

## Alternatives considered

**Implement the three `dlsym` entry points.** Requires breaking the
obfuscation first, and then implementing whatever contract they turn out to
be — a vendor plugin ABI, with the framework still driving. More work for a
worse outcome.

**Replace `/res/lib/libzkgui.so` directly** rather than adding a path. Works,
and throws away the stock application and the ability to switch back by
editing one field. The config indirection costs nothing.

**Put NOTRIX in `/res` beside the vendor app.** Then every NOTRIX update is
a flash, which is exactly what this decision exists to avoid.

**Replace `/bin/zkgui` or an init service.** Both live on the rootfs, whose
failure means the device does not boot. Rejected in ADR 0020 and still
rejected.
