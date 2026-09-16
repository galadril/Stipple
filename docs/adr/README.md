# Architecture Decision Records

Short, dated records of decisions that would otherwise have to be reconstructed
by reading code and guessing. Blueprint §36 requires one for every significant
architectural change.

Format: context, decision, consequences. If a decision is later reversed, the
old ADR stays and gains a "Superseded by" line — the history is the point.

## Reserved numbers

Blueprint §36 reserves 0001–0010 for the foundational decisions. Numbers are
claimed up front so that parallel work does not collide, and files appear here
as the decisions are actually made.

| Number | Title | Status |
|---|---|---|
| 0001 | Project scope | Accepted |
| 0002 | GPL-3.0-or-later licence | Accepted |
| 0003 | Framebuffer renderer owned by NOTRIX | Accepted |
| 0004 | FlyThings runtime boundary | Pending — needs hardware |
| 0005 | Headless build | Pending — needs hardware |
| 0006 | Temporary ADB development | Pending — needs hardware |
| 0007 | Native API plus compatibility layer | Withdrawn — see 0015 |
| 0008 | Installer helper | Pending |
| 0009 | Update security | Pending |
| 0010 | Font strategy | Not written — recorded in `firmware/src/text/Font5x7.cpp` |
| 0011 | Simulator-first development order | Accepted |
| 0012 | Dependency-free core and test harness | Accepted |
| 0013 | Required and optional platform capabilities | Accepted |
| 0014 | In-house bounded JSON parser | Accepted |
| 0015 | No AWTRIX compatibility layer | Accepted |
| 0016 | TC002 input layout: a knob and two buttons | Accepted, pending hardware confirmation |
