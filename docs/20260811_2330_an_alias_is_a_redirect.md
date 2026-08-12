# An alias is a redirect

## Three changes

Property fan-out stopped happening inside the setter. `FanOutSubscribers`
used to walk the subscriber list and call each handler while the write was
still on the stack; now it copies the value into a chunk and hands it to
`SndMsgNode`, which costs one task insert and returns. Delivery happens later,
from `ExecTasks`, flat.

The change-check came out of `SetPropStr` / `SetPropInt` / `SetPropLong`. A
write no longer compares the new value to the old one to decide whether
anything happened.

And a control in a panel stopped holding a copy of the property it shows. Its
`Value` is now a link to the object's property — one node, reached from two
addresses.

The first two are the same statement twice: **a write is an event, and every
event goes through the scheduler.** The third is what that statement makes
possible.

## Why the second one used to crash

Take the check out on its own and the engine died instantly. Not slowly, not
under load — on the first press.

The check was not a filter. It was a **fixed-point detector for a two-copy
sync protocol nobody meant to write.** A control and the property it displayed
were subscribed to each other: the control edited the property, the property
reflected back into the control. Two nodes holding one datum, each announcing
to the other, and the comparison was the thing that said *they agree now,
stop*. It converged in one round trip, so nobody ever saw it.

That explains all three failure modes at once:

- **Same value** — converges immediately, invisible for years.
- **Alternating values** — never converges. A Button writes `1` then `0`, the
  second write enters while the first is still bouncing, and what comes back is
  always the opposite of what is there. Unbounded. Synchronous, so it nested
  until the stack died.
- **Check removed** — even the same-value case never terminates, and with
  delivery queued it becomes a flat spin at 75% of a core instead of a crash.

`button.c` had been living with this. It writes `1` on press and then writes
`0` through a private setter that deliberately fans out to nobody, with a
comment admitting the cheat: *"You cannot send two events to the same thing in
the same function."* That is not true of the fabric. It was true of the
reconciliation.

## What an alias was

There is an `Alias` class. It stands for one property of another object, and
its own `Value` slot is a node-level link to that property, so the value, the
subscribers, and anything wired to it live only on the original. Its own doc
says it plainly: *there is no forwarding, no second state, nothing to keep in
sync, and this object needs no handlers or tasks of its own.*

The trap is that having a class called `Alias` makes it look like a **species**
— a kind of object, distinguishable from a Button or a Checkbox, that code can
branch on. Follow that reading and you end up converting controls into Aliases
in order to change what they point at, which changes *what class they are* to
express *where their data lives*. Those are not the same operation. Do it and
you have manufactured a second kind of control, and every layer above has to be
taught to render it.

## What it became

An alias is not a thing. **It is a redirect, and any control can have one.**

A Checkbox in a panel and a Checkbox on a canvas are the same Checkbox. One
points its `Value` at the widget's `Enable`; the other points at nothing and
holds its own. Nothing branches on which. The class never changes, the
protocol never changes, and a client that has never heard of links renders both
identically because there is nothing different to render.

In `Widget_Ctl` the whole conversion is one call:

```c
LinkPropertyAs(c, "Value", target, prop);
```

replacing the pair of `Connect`s that used to subscribe the control and the
property to each other. Two nodes became one. The ring cannot form because
there is no second end for it to close on.

## Why it was nearly free

Because the redirect was already respected everywhere that mattered.

`ResolvePort` has been sitting in object.c the whole time, and everything that
reaches a property by name already went through it — `Connect`, `SndMsg`,
`SetOrDeliverProp`. So the moment a control's `Value` became a link, wiring it,
sending to it, and writing it all landed on the original with no new plumbing.
`LinkPropertyAs` did not have to be written; it was already there, built for
the Alias object.

What was missing was the mirror image. **Writes resolved; reads did not.**
`GetPropStr` is node.c and knows nothing about links, so anything that read a
property directly saw the empty slot rather than the data — a script's
`sibget` returned `""`, and a subscription's first value came back blank while
every later change arrived correctly. The fix is one rule, applied in three
places: reading a property resolves the way writing one does. One helper behind
the script host's `get`/`sibget`/`pathget`; one resolve in the bridge's
subscribe; one in the plain setters, so an object writing its own property
reaches the data too.

One asymmetry is worth keeping: resolving is how the data is **found**, never a
reason to re-address the **answer**. Subscribe to a control and you are told
about that control, under its own name. Where the value lives is the engine's
business, and the moment it leaks into the protocol the client has to start
knowing which of its controls are standing in for something.

## The size of it

```
9 files changed, 245 insertions(+), 84 deletions(-)
```

A good share of the insertions are comments and new debug output. Call it 160
lines of change, for: the execution model of the fabric, the meaning of a
write, and the relationship between every control in the system and its data —
about 230 controls, rebound by editing one function.

That ratio is a property of the design, not of the day. One kind of thing means
one place to change what a write means. One definition of delivery means
dispatch changes once. One binding site means every control rebinds together.
A system with typed ports, separate event channels, and per-class rendering
would have paid for the same idea in every one of them.

## What it actually cost

The changes that landed in uniform layers took minutes and worked the first
time. Every hour went to the seams — the places where one thing was already
pretending to be two.

A second render path in the client that had to independently remember to stamp
a widget type, apply `W`/`H`, honour `LabelPos`. A lookup table with no entry
for buttons, whose `PROP_NULL` return read like a decision and was really a
gap. `className === 'Button'` branches selecting a control by class while the
other path selected by widget type. A `Button` that sent an `activate` verb
where a `MoButton` wrote its `Value`, so one worked through the resolve and the
other never touched it — the two are the same control, and the only real
difference is that one of them also reports the release.

The cost was never proportional to how deep the change went. It was
proportional to how many special cases it had to pass through. Which is the
same lesson as the crash: yesterday's stack overflow, the ping-pong, the
manufactured `0`, the private setter, the dead panels were never five bugs.
They were one datum living in two places.
