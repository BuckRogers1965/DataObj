# The Same Machine At Two Scales

There are two versions of this design running right now. One is a web
canvas on a Linux box: objects loaded from disk with `dlopen`, wired at
runtime by dragging, a node tree that holds registry, config and layout
alike. The other is a weather station on a microcontroller with 264KB of
RAM, two cores, and a handful of sensors on GPIO pins.

They were not written to match. They match anyway, and the places they
diverge turn out to be the interesting part.

## The scheduler is identical

Not similar - identical in discipline:

```c
AddTaskMilli(task, registry.getItem_id(idx)->update_interval_ms,
             &readAM2302a_temp, idx, 0);
```

`CreateTask()`, `AddTaskMilli(task, ms, fn, a, b)`, a callback that
re-arms itself as its first statement, a single-threaded pump. On the
device it is `DoTasks()`; on the server it is `ExecTasks()`. A task
written for one runs on the other after a rename.

That is not a stylistic coincidence. Cooperative self-rearming tasks are
the only scheduling model that needs neither preemption nor locks, so it
works unchanged from 264KB to a 16GB server. And it gives both systems the
same emergent property: a task that stops re-arming costs nothing, so
"disabled" needs no special state. Everything goes quiet on its own.

The small one takes it a step further in a way worth stealing. Each sensor
arms itself at *its own* interval, read from the thing being polled - and
the intervals are 6004, 7003, 8002, 9001 rather than round numbers, so
they never bunch onto the same wake. Nobody picked a global tick. That is
exactly the argument for deleting the 1ms sleep cap in the big one's main
loop: cadence belongs to whatever knows it, not to a constant in the loop.

## The other three pieces

Both systems are built from the same four things.

**A node tree.** On the device it is a flat table with a parent column:

```c
{"w_temp_a", "sensor_card", "Temperature A", "temp_a", W_DIAL, "min:0,max:120"},
```

id, parent, name, what-it-shows, kind, and a passthrough props string the
framework parses but never interprets semantically. That is the same
containment hierarchy as root -> view -> widget, written as a table
because on a device you would rather spend flash than heap.

**A typed value store.** `float value` with `unit`, `min_val`, `max_val`,
`step` versus a DataObj that converts between string, integer and real on
every read. Both exist so a producer and a consumer never have to agree on
representation.

**A projector that renders and never owns.** The layout table references
registry items *by string id*, resolved to numeric indices once at boot.
The same registry item appears twice in that layout - once as a text
readout, once as a dial - and nothing had to be told. One value, two
views. That is the whole subscriber idea, arrived at from the other
direction.

## The registry is the message passing

Here is where the small one is doing something the big one should copy.

Two cores, so two copies of the registry, plus a dirty bitmap:

```c
void set_id(uint8_t id, float val) { setDirty(id); items()[id].value = val; }
```

`sendDirty()` walks a rotating cursor so no item starves another, packs a
four-byte message, and pushes it through the inter-core FIFO.
`recvUpdates()` applies it with `update_id()`, which deliberately does
**not** set dirty - the one asymmetry that stops two cores echoing a value
back and forth forever.

Three properties fall out of that, and the big framework has none of them:

- **Coalescing.** The dirty bit is idempotent, so ten writes between syncs
  cost one message carrying the latest value. Cost stops scaling with how
  fast something changes.
- **Backpressure that cannot fail.** `if (!fifo_send(msg)) return;` - a
  full queue just leaves the bit set for next time. Nothing lost, nothing
  blocked, no growth.
- **Fairness.** The rotating cursor means a fast-changing item cannot
  starve a slow one.

The big framework sends an envelope per write, dispatches each one, and
pushes a frame per event. A dragged slider pays all three for every
intermediate value, and the browser discards nearly all of them - it
cannot display more than one state per frame.

The fix is the same shape at the larger scale: a per-connection dirty set
in the bridge, flushed at ~60Hz. Sixteen milliseconds is a frame, and a
frame is the fastest a human can be shown anything, so it is simultaneously
the cheapest correct answer and sixty times less work than the loop does
today.

## What actually differs: cardinality and lifetime

The device knows its audience at compile time. There is exactly one
consumer of a value - the other core, and then the browser - so a single
dirty bit *is* the subscription list.

The big one discovers its audience at runtime and it changes while
running, so it needs real records: who subscribed, to what, with what
handler, torn down when either end dies. Every hard problem this week came
from that one difference. Stale records pointing at freed instances. Two
registry-wide walks per delete to find wires by search. A bridge keeping a
second copy of the subscription list in step by hand.

None of that is wrong. It is the price of dynamic wiring, and dynamic
wiring is the entire point of the canvas. But it is worth naming as a
*price*, because it gives you a test for any mechanism in the big system:
**what breaks if the audience were fixed?** If nothing breaks, the
mechanism is accidental complexity rather than the cost of the feature.

## What each teaches the other

Small to large:

- Resolve names once, run on integers afterwards. All the string work
  happens in one boot pass; the renderer touches only indices. The big one
  resolves paths at runtime, every time, and could do the same per view at
  open time.
- Ship only what is used. Each page scans its own subtree for which widget
  kinds it contains and emits only that CSS, and bakes only its own index
  list into its JS.
- Fixed-size everything, no runtime allocation. That is the shape a
  per-tenant arena would take if "hundreds of instances" ever needs to be
  predictable as well as cheap.

Large to small:

- The moment items talk to each other, the ladder appears. Today every
  value has one producer callback and the view pulls; a 1Hz control task
  stands in for wiring. The first time soil moisture drives irrigation,
  that task becomes a subscription - and the lesson learned expensively
  this week is to record the wire at **both** ends from the start, and to
  keep the value handler separate from message dispatch. Its `ItemType`
  enum is already the beginning of a class taxonomy.
- Keep the reverse map derived, not stored. Any cached name-to-index table
  is a silent mis-binding waiting for the first edit that reorders
  something.
- A counter that grows and never shrinks across a cycle is a leak, named
  by its type. The device already publishes free RAM as an ordinary
  registry item, which is the same instinct as a Stats object; the
  discipline to add is sampling every counter before publishing any, or
  the observer watches its own wake oscillate.
- Annotations the engine never reads are a feature. The props string is
  already exactly that. Keep it that way even when reading one would be
  convenient.

## Why keep both

The small one is a cheap test bed for the expensive design. No dynamic
loading, no bridge, sixty-four items. If the dispatch ladder is right, it
should express cleanly there in a few dozen lines - and if it cannot, that
is evidence about the design, not about the platform.

It runs the other way too. The flush-at-60Hz idea can be proven on the
device in an afternoon, on hardware where wasted work is visible
immediately rather than hidden under a fast machine.

Two implementations of one design, at scales three orders of magnitude
apart, and the scheduler did not have to change at all. That is the
strongest evidence available that the model is the right shape.
