# Wires that know both ends

## The widget we cannot build

Consider a Sum. An in box, an out box, and a button. You wire five things -
or fifty - into the input, press the button, and it walks the list of what is
connected to it, reads each one's *current* value, and adds them up. Messages
in flight are ignored on purpose: it is a snapshot, not a stream.

We cannot build that today, and the reason is one line:

```c
AddSubscription(fromPort, toOwner, GetNameStr(toPort), handler);
```

`Connect()` records the wire on the **source** property, and only there. A
source can answer "who do I send to?" by walking its own sub-nodes. A sink
cannot answer "who sends to me?" at all - except by walking every library,
every class, every instance and every property in the registry looking for a
`Subscriber` record that happens to name it.

So the framework is push-only, not by design but by bookkeeping. Everything
downstream of that is a consequence.

## Pull is a legitimate second mode

Worth saying why "read everyone's current value" is a coherent request here
and not a hack.

Every property is a node with a resting value. There are no ports, so there is
no such thing as an edge with nothing to read - "what is the value at the other
end of this wire" is always a well-defined question. That is exactly the
property that makes a snapshot meaningful.

And pull does not interact with push. A gather runs synchronously inside a
handler: no envelopes, no queued dispatch, no ownership transfer. "Ignoring
messages in flight" is therefore not a race to be managed, it is a statement
that the two mechanisms do not touch. A message already queued will arrive
later and do what it always does.

## Deletion is the real prize

The Sum widget is the visible motivation. It is not the important one.

`DeleteInstance` today calls `ScrubRegistrySubscriptions`, which walks every
library, every class, every instance, every property, recursively through
sub-properties, stripping records that name the dying node. That is O(the whole
session) on every delete.

With the wire recorded on both ends it becomes: walk my own two lists, tell
each peer to drop me, done. O(my edges).

And it is not only faster, it is **safer for the same reason**. An exhaustive
walk has to actually be exhaustive - miss one corner of the tree and a
Subscriber is left pointing at freed memory, which is the ASan-confirmed
use-after-free the scrub exists to prevent. A local list cannot miss a corner
because there is nowhere else to look. Correctness stops depending on the
walker being complete.

The same argument retires the inbound half of every introspection question -
`list-connections`, `CloneConnections`, the diagnostics pages, the class and
dependency graphs - all of which currently walk because they have no choice.

## The tree holds the truth, the index gives the speed

This arrangement already exists here, which is the best argument that it is the
right one. The namespace trie is an O(path-length) lookup sitting *beside* the
tree, while the truth is the `Name` and `Container` each instance carries, and
`PathOfInstance` verifies by resolving back. The index is derived and can be
rebuilt from the tree at any time, which is precisely why the index can never
be the thing that is wrong.

Wires want the same shape:

- **Truth**: records on both nodes. Durable, serialisable, cloneable, visible
  in a dump.
- **Speed**: a lookup structure beside the tree, built from those records, for
  questions that span the session.
- **Rule**: the index is always rebuildable from the tree. If they disagree,
  the tree wins.

The discipline that makes two records safe instead of twice as dangerous is
**one writer**: `Connect` and `Disconnect` are the only things that may create
or remove either half, and serialisation emits a wire once - from the source
end - and reconstructs the mirror on load. Two records that can drift are worse
than one record and a walk.

## Implementation plan

Wing walking: never let go of one thing until you have hold of the next. Every
phase below leaves the system working, is one commit, and is revertible on its
own. No phase removes an old path until the new one has been proven to produce
identical answers.

**Naming is not decided.** "Subscriber" is the existing forward record; the
mirror needs a name and that is the author's call, not mine. It is written
below as `<mirror>` so nothing accidental gets entrenched.

### Phase 0 - build the oracle first

Before anything changes, write the harness test that enumerates every wire in
a session the slow way (the registry walk) and asserts a canonical, sorted set.
This is the hand on the rail: every later phase must produce exactly this set.

- [ ] `testharness/wiretest.py`: build a known graph, enumerate all wires, assert
      the set
- [ ] assert the same set survives clone, export/import and save/load
- [ ] record allocation counters (`NodeCount`, `DataCount`) across a
      create/wire/delete cycle - must net zero, as leaktest already demands

Done when: it passes on today's code, unchanged.

### Phase 1 - write the mirror, read nothing

`Connect` writes the back-edge, `Disconnect` removes it, `DeleteInstance`
removes it. **Nothing reads it.** Behaviour is identical by construction.

- [ ] `AddSubscription` gains a mirror write on the sink property
- [ ] `Disconnect` removes both halves
- [ ] confirm `IsPortableProp` excludes the mirror, so save/export are
      byte-identical before and after this phase
- [ ] confirm `CloneObject`/`CloneConnections` do not duplicate wires
- [ ] Phase 0 oracle still passes; counters still net zero

Done when: exporting the same flow before and after Phase 1 produces identical
files. That is the acceptance test, and it is a strong one.

### Phase 2 - a checker, before any reader trusts it

- [ ] `VerifyWireSymmetry()`: every forward record has exactly one mirror and
      vice versa
- [ ] called after Connect / Disconnect / DeleteInstance under `-v 3` (a new
      `WIRE`-category trace, which already exists for wires made and removed)
- [ ] run the whole harness with it on

Done when: the full matrix runs clean with symmetry checking enabled. Any drift
is found here, while the mirror still has no consumers.

### Phase 3 - the first reader, purely additive

- [ ] engine call: sources of a given `(instance, property)`, from the mirror
- [ ] bridge: answer inbound as well as outbound (extend `list-connections`)
- [ ] test it against the Phase 0 oracle - the two enumerations must agree

Done when: inbound and outbound enumeration agree with the slow walk on every
graph the harness builds.

### Phase 4 - Sum

Build the widget. Purely additive, nothing existing changes, and it is the
proof the capability is real rather than theoretical.

- [ ] `objects/sum`: `In`, `Out`, a button; on press, gather and add
- [ ] ignores in-flight messages by construction (it reads resting values)
- [ ] a test that wires five boxes in, presses, and checks the total
- [ ] a test that changes one box and presses again

Done when: the widget from the top of this document exists.

### Phase 5 - switch the delete scrub

The first phase that removes anything, so it keeps both hands on:

- [ ] `DeleteInstance` uses the local lists
- [ ] the old registry walk still runs immediately after, as an assertion:
      if it finds anything left, that is a bug, and it says so loudly
- [ ] run the full matrix, ASan included, with the assertion on
- [ ] only then delete the walk

Done when: the assertion has found nothing across a full matrix run, and the
delete path is O(my edges).

### Phase 6 - switch the remaining walkers

Same pattern, one at a time, new path with the old one as an assertion behind
it, then remove:

- [ ] `list-connections`
- [ ] `CloneConnections`
- [ ] the bridge's inbound queries

### Phase 7 - the index beside the tree (only if measured)

Not before. The local lists may well be enough for everything except
session-wide questions, and an index that nothing needs is a second source of
truth for free.

- [ ] measure first: which remaining question is actually slow?
- [ ] build it derived, rebuildable from the tree, never authoritative

### Phase 8 - reduce as a standard handler

Once a node can enumerate its inbound edges, Sum stops being a class and
becomes a configuration: gather, then reduce. Sum, min, max, count, average,
concat - the standard-handler set from
`20260807_1250_every_property_has_an_audience.md`.

- [ ] `Handler="Sum"` on a property, gather-and-reduce on poke
- [ ] the Sum widget becomes a face on the mechanism rather than its own class

## Invariants, at every phase

1. The tree is the truth. The index, if it ever exists, is derived.
2. One writer: only `Connect`/`Disconnect` touch either half of a wire.
3. A wire serialises once. The mirror is reconstructed on load, never saved.
4. No phase removes an old path until the new one has been proven to give the
   same answer on a full matrix run.
5. Every phase is one commit and reverts cleanly on its own.
