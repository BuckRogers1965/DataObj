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

---

## What it took, 16 August

All of the above got built. Not in the order it was written, and one of the
claims above is wrong - that part is at the end.

### The two questions, spelled apart

`RequirePath` and `RequirePathOf` sit beside `ResolvePath` and
`PathOfInstance`. Same trie, same lookup, same answer; the difference is that
the Require forms report before returning it. They are macros over
`...At(..., __FILE__, __LINE__)`, so the ERROR names the caller rather than
object.c - which matters more than it sounds, because the whole point is that
the *site* knows what the function cannot.

`RequirePathOf` says which of the two faults it is: no `Name` at all, against a
`Name` whose path is not in the index. Those had been indistinguishable for as
long as both were `0`.

The rule for choosing turned out to be structural, and it is in the header:
**walking `FirstMember` means every one of them was named on purpose, so
assert; walking `FirstInstance` reaches private handles that are unnamed
deliberately, so probe.** Which iterator you are standing in tells you which
question you are asking. That is why five sites stayed exactly as they were and
were not oversights after all.

The worst case was the one this started from. `script.c`'s `pathset` did

    NodeObj inst = ResolvePath(Arg(argv, 0));
    if (inst) SetOrDeliverProp(inst, ...);

so a mistyped path in a script did nothing, silently, forever. Every path verb
in that file now asserts. `exists` is the one probe left - the API had been
telling us the two questions were different all along.

### What it found first: the basename, discarded

Every one of the nine create sites had the same shape: work out a full path,
`RegisterPath(alias, inst)`, then set `Name` from that path's last segment.
`Bridge_SetNameFromAlias` four times, open-coded in import twice, on the line
before in script and control.

`RegisterPath` already splits the path on its last slash - it needs the
container. It had the basename in hand and threw it away, which is the same
shape as the container announcement it throws away in the piece before this
one. And because the two were separate calls they COULD disagree, which is
exactly the fault `RequirePathOf` reports: the reverse lookup derives from the
`Name`, so a `Name` that drifted from its registered path leaves the thing
unreachable while sitting in the index the whole time.

So `RegisterPath` sets the `Name`, before it tells the container, and six
companion calls went away. The two `Bridge_SetNameFromAlias` calls that restore
a name after a REFUSED rename stayed - no register follows those, so they are
not this.

### The bug I put in, and the thing that found it

That change shipped with a use-after-free in the log line:

    char *had = GetPropStr(inst, "Name");   /* into the Name DataObj */
    ...
    SetPropStr(inst, "Name", base);          /* replaces it          */
    snprintf(dbg, ..., had[0] ? ", was " : ..., had);

Reading the old name AFTER the write that freed it, to say what the name used
to be.

Four of five builds passed. `free()` leaves the bytes where they are, so
`snprintf` read the old name back intact and printed a correct log line;
`-O0`, `-O3` and gcov were all reading freed memory and getting away with it,
and ubsan does not check heap lifetime. ASan poisons the block and quarantines
it, so the same read aborts the process on the spot. The variant that failed is
the one that worked.

Worth being blunt about the review value here: the block is six lines, it was
written deliberately, and reading it twice did not surface it. On paper `had` is
just a string you print. Nothing but an allocator that refuses to let a freed
byte look normal was ever going to catch it.

### The snag: `CreateObject` was already the private-handle route

The plan said `CreateObject` should name and register. It cannot, as written,
because three widgets create their inner TCP sockets with it - mcpsource twice,
tplink once - and depend on them staying unaddressable. Naming them would drop
three widgets' private sockets onto the canvas and into every save.

Which is the same finding again, one level up. **The difference between "a
member" and "a private part" was expressed by omission** - create it and then
just do not name it - so the engine could not tell one from the other, exactly
as it could not tell an expected missing name from a broken one.

`CreatePrivate` is that state said out loud. The body is the old `CreateObject`
verbatim; `CreateObject` is now `CreatePrivate` plus a name. The three sites say
which one they mean. Nothing about what happens changed - only about what is
stated, which is the whole theme.

### Naming is part of creation

`CreateObject` mints in the container with `MintFreshName` and registers, so it
returns something addressable. `RegisterPath` treats a second name as a move and
retires the first, which is what lets a caller rename it on the next line
without leaking the minted one.

Five sites had to stop writing `Name` themselves: `Widget_Create`,
`BuildChrome` twice, `script.c`'s create, and `CreateAlias` - which had been
writing `Name` and never registering at all, leaving the bridge to register it
afterwards. A `Name` write between the mint and the register defeats the
re-key, because `PathOfInstance` would then derive a path that is not in the
index and the minted one could not be found to retire.

**And one contract consequence.** With the engine minting, a caller that also
mints steps over the name the engine just took - the first Slider dropped on an
empty canvas would come back `Slider_2`. `Bridge_Create` now keeps the name the
instance arrived with. One minting authority instead of two, which is what
`CloneInstance`'s comment has said all along: *the engine names it, this is the
core's job, not the caller's.*

The `Widget_Create` adopt problem - the one place the piece above called out as
not mechanical - turned out not to exist. The adopt check already ran BEFORE
the create, so nothing had to move.

### The claim above that is wrong

This piece says the flip means a constructor can build its own panel, and that
the two-phase widget construction becomes a choice.

It does not. `CreateObject` calls `InstanceStart` and mints AFTER it returns -
the instance does not exist until the constructor makes it, so inside
`InstanceStart` there is still no path. `scriptbox.c:354` stays exactly as it
is, and stays correct. Closing that window means deciding the name before the
constructor runs and handing it in, which is a different change with a
different shape.

The window that closed is the one between `CreateObject` returning and the
caller getting round to naming - which is the one the nine triples existed for,
and the one where a save could find a placed thing with no name. That is worth
having. It is just not the same window, and I wrote it as though it were.

---

The pattern under all of it, three times in one file: **the engine knew the
answer and threw it away.** The container was told about every arrival and kept
nothing. `RegisterPath` held the basename and dropped it. `CreateObject`
resolved the container path and discarded it so nine callers could resolve it
again. Each one got absorbed locally by whoever hit it, which is why none of
them ever looked like a bug.

