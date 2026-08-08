# How Not To Add A Message Id

Kept as a counter-example. The accompanying `.patch` is the whole failed
attempt, backed out of the tree; nothing here shipped.

## What was being fixed

An instance destroyed by a route the bridge never sees - a script's
`destroy()`, a widget tearing down its own children - left the bridge holding
tap records with dangling pointers. The allocator later handed a recycled
property-node address to something new, `Bridge_FindTap` matched the stale
record, and the truth-on-demand emit dereferenced a freed instance. Confirmed
from a core dump, not guessed:

```
#0 CmpName (node=0x8, name="Name")              src/node.c:228
#1 GetPropStr (node=<freed instance>, "Name")   src/node.c:550
#2 PathOfInstance                                src/object.c:136
#3 Bridge_AliasForInstance                       bridge.c:451
#4 Bridge_TapEmit                                bridge.c:1986
#5 Bridge_Subscribe                              bridge.c:2192
```

asan passed clean the whole time, because asan quarantines freed memory so the
address is never recycled and the stale record never matches. ubsan and gcov
crashed. That asymmetry is a fingerprint of address reuse, not of sanitizer
strictness.

The diagnosis was right. Everything after it was wrong.

## The wrong turns, in order

**1. A new id, delivered to every existing handler.** `msg_eoc` (end of
connection) announced from `DeleteInstance` down every wire before the frees.
It fired `ActivateOnMsg`, which activates on any id except `msg_eof`, so a
wire coming up ran the sink's Activate - at boot that is every widget building
its panel, cascading. The web TCP was activated before its port was set and
the framework never finished booting.

**2. Making the message payload-free to make it harmless.** Then handlers that
read the payload without checking the id read *nothing* as *empty*:
`TPLink_OnEnable` did `local->enabled = GetValueInt(data) ? 1 : 0` and a wire
coming up disabled the object and tore it down. The panel came up unchecked
and refused every button.

**3. Guarding the handlers.** Eleven widget `_OnIn` handlers got `|| !data`.
Then a proper scan found **81 handlers across 40 files** that read the payload
without requiring one. At that point the size of the fix is the signal: this
was not 81 bugs, it was one change violating the contract all 81 were written
against.

**4. Guarding in the core instead.** `DeliverToSubscriber` dropping any
delivery with no payload. One place, and it does protect all 81 - but it also
means a payload-free message reaches nobody, including the one subscriber the
whole exercise existed to notify. The two rules together are unsatisfiable: to
be delivered it must carry data, and if it carries data every handler acts on
it.

## The actual root cause

`msg_change` and `msg_send` name **how a message arrived** - a property's
synchronous fan-out versus a queued `SndMsg` - not what it *is*. So no handler
can ask "is this data?"; it can only ask "did this arrive the way I expect?",
and each one picked a different answer:

- `Out_OnEnable`, `TCPPort_OnEnable`: `message != msg_send` -> drop
- `TPLink_OnEnable`: `message == msg_eof` -> drop, everything else acts
- `Textbox_OnIn`: no filter at all, deliberately, "a write is a write"

Three conventions for one question, none of them wrong on its own terms. A new
id cannot be added safely on top of that, and the danger predates the new id -
`msg_eoc` only made a latent problem fire.

## The shape that works

A positive name for the thing itself: `msg_data` means *this carries a value*.
A handler acts on `msg_data` and every other id - `soc`, `eoc`, `eof`, and
whatever gets added in five years - is inert without the handler knowing they
exist. The taxonomy belongs in the enum, once, not in 81 filters.

## The process lesson, which is the expensive half

Four attempts, each one patching the symptom the previous attempt produced,
each one wider than the last: two ids, then eleven handlers, then eighty-one,
then the core's delivery path. A fix that keeps growing is not converging - it
is a missing distinction being routed around, and the right move is to stop at
the second attempt and go find it.
