# A name arrives late

*The last window in the core, 16 August 2026.*

Everything in this session is addressed by path. `ResolvePath` is how a bridge
command finds its target, how a script reaches a sibling, how a REST URL means
anything, how a flow file records a wire. The path index is the address space.

And an object is created without one.

`CreateObject` (`object.c:1220`) does three things. It refuses a container that
has no path of its own - loudly, with an ERROR and a node dump, because
creating something nowhere helps nobody. It calls the class's `InstanceStart`.
It sets `Container`. Then it returns, and the thing it returns is not
addressable. It has no `Name` property at all.

The name arrives later, from whoever called.

## Three files carry the same comment

What `InstanceStart` sets is `SetName(instance, "Slider")` - the node's own
name. `PathOfInstance` reads the `Name` **property**. Those are different
fields, and the difference is invisible until it isn't:

```
/* the node name is not the Name PROPERTY - PathOfInstance reads the ... */
```

That comment appears in `dns.c:751`, `udp.c:359` and `tcp.c:906`. Three
authors, three modules, one fact rediscovered three times. A comment written
three times independently is not documentation. It is a report that the design
is surprising people at the same spot.

## Every caller closes the window by hand

Between `CreateObject` returning and the caller naming it, there is a live,
placed, class-registered instance that nothing can find. Every caller shuts
that window with the same three lines:

```c
inst = CreateObject(container, cls);
SetPropStr(inst, "Name", name);
if (PathOfInstance(container, cpath, sizeof(cpath))) {
    snprintf(path, sizeof(path), "%s/%s", cpath, name);
    RegisterPath(path, inst);
}
```

`control.c:88`, `bridge.c:565`, `bridge.c:688`, `bridge.c:822`, `bridge.c:923`,
`serializer.c:283`, `serializer.c:780`, `script.c:441`. Nine sites, and each one
re-resolves the container's path that `CreateObject` resolved one call earlier
and threw away.

Nine copies of a thing is the same signal as three copies of a comment. Nobody
chose to write it nine times. They wrote it once each, because the create did
not do it.

## What the window actually costs

Not tidiness. Look at what a widget has to do to live with it:

```c
/* now the box has a path (deferred build / activation), build the panel and
   bring the inner host up - a sub-object needs the box's OWN path first, so
   this cannot happen in InstanceStart (the path is set after it returns) */
```

`scriptbox.c:354`. A widget cannot build its own panel in its constructor,
because its panel is made of objects created *in it*, and creating something in
it needs its path, and its path does not exist yet. So construction is split in
two: `InstanceStart` for the parts that need no path, `Activate` /
`Widget_BuildOnce` for the parts that do. Every widget in the tree is shaped
around that split.

That is not a lookup-hygiene problem. That is the naming order deforming the
object lifecycle. The two-phase constructor is not a design; it is a
workaround with thirty implementations.

## One return value, three facts

`PathOfInstance` returns 0, and 0 means:

1. **Not named yet.** The window. Always a bug.
2. **Never to be named.** The private handle - a language host inside a
   ScriptBox, a socket inside a port widget. `scriptbox.c:154`: "created through
   that class's own `InstanceStart`, never named, never path-registered, never
   wired." Correct, deliberate, permanent.
3. **Named, but the path resolves to somebody else.** The function ends
   `return ResolvePath(out) == inst;` - so a class-default name colliding with a
   registered sibling, or an instance caught mid-rename, returns 0 too. A bug,
   and one that looks exactly like case 2.

This is why the log on that path is `PLACE` at `-v 3` and nothing louder. The
comment above it records the history: it used to be an ERROR, it fired with a
full node dump on every registry-wide walk because of case 2, and it was
demoted. Demoting it was right. What was wrong was that three facts had one
return value, so the loudest thing you could say about any of them was the
quietest thing true of all of them.

Ambiguity sets the volume. You cannot shout when you might be wrong.

## Probe, request, assertion

Three questions are spelled with two function names.

**"Is this name free?"** NULL is the answer you *want*. `MintFreshName`,
`Widget_Create`'s adopt check, `Bridge_CreateAlias`'s uniqueness test,
`serializer.c:780`. Silence is the success path. These must never log, and they
never should have.

**"Find what this string names."** The caller is a translator holding a string
from outside - a bridge command, a REST URL, a script argument. NULL is a
*client* error: it belongs in a reply on the client's own channel, and in the
log at the translator, because the string came from outside and could be
anything. Bridge already does this.

**"Where is this thing I am holding?"** The caller has a live `NodeObj`. NULL
means something alive is unaddressable. This is the one that matters, and it is
the one that is silent everywhere:

```c
if (!PathOfInstance(inst, pbuf, sizeof(pbuf)))
    continue;
```

`serializer.c:936`, `951`, `989`, `1129`, `1413`; `bridge.c:1390`, `1458`,
`2397`, `2420`, `2470`. A save quietly omits a member. A repath quietly skips
one. No log, at any verbosity. The engine noticing its own inconsistency and
saying nothing about it is worse than not noticing.

## The distinction is already recorded

There is no flag to add and no new mechanism to invent. The fact that separates
"legitimately unnamed" from "should have a name and doesn't" was written into
the fabric last week, for another reason.

`RegisterPath` calls `AddMember`. An instance in its container's `Members` list
was named on purpose. A private handle never enters that list, because nothing
ever registered a path for it.

So "should this be addressable?" has a structural answer. A member with no path
is the engine disagreeing with its own index, which is an ERROR by definition -
class, container and name, said out loud. A non-member with no path is a
private handle minding its own business, and stays silent. Same derivation,
different contract, distinguished by a fact the engine already keeps.

The containment index was built to stop walking the registry. It turns out to
answer a question nobody asked it: which silences are correct.

## Naming is part of creation

`CreateObject` already holds everything it needs. It has the container. It has
the container's resolved path - it resolved it to check. It has the class name.
`MintFreshName` is right there, with the rule already agreed: strip a trailing
`_N`, take the lowest free `_k`. It stops one line short of registering.

Let it name and register, always. Then:

- The window closes. There is no moment when a live instance is placed and
  unaddressable, so nothing needs to close it by hand, and nine sites lose
  their triple and their redundant re-resolve.
- A constructor can build its own panel, because it has a path when it runs.
  The two-phase split becomes a choice rather than a requirement.
- Private handles are untouched. They never went through `CreateObject`; they
  go straight to `InstanceStart`. Which means afterwards, an unnamed instance
  from `CreateObject` is *impossible*, and unnamed means exactly one thing.
- Case 3 splits off from case 2 and can be shouted at, because it is no longer
  standing next to something innocent.

Renaming afterwards is the re-key that already exists - `UnregisterPath` then
`RegisterPath`, `bridge.c:1540` - so boot and the GUI rename through one path
instead of two.

## The one place it is not just cleanup

`Widget_Create` (`control.c:71`) adopts. If the named thing is already there, it
*is* the one - a load restores a widget's panel from the file, and the widget's
own build then runs over it to put back what a file cannot carry: compiled
handlers and wires, the LONG properties the serializer drops on purpose. Make a
second set instead and the restored panel is inert and the new one is empty,
which was the blank help panel.

Adopt depends on the caller choosing the name *before* the object exists. If
`CreateObject` mints, adopt has to happen ahead of the create rather than
inside it. That is a real ordering question, not a mechanical change, and it is
the part to get right first.

---

Everything here was known. The address space was designed, the index was built,
the private handle was deliberate, the assertion sites were written by people
who understood them. What was missing is that creation and naming were two
steps, so between them there was a state nobody designed - and every consequence
of it got absorbed locally by whoever hit it. Three comments, nine triples,
thirty two-phase constructors, ten silent skips.

A state nobody designed does not announce itself. It gets accommodated, once per
site, by people who each think they are handling a detail.
