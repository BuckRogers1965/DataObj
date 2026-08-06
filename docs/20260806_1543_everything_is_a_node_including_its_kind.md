# Everything is a node, including what kind of thing it is

*6 August 2026*

Three mechanisms went in today: a class tree, versioned dependencies, and a way
for one loadable module to call another without linking to it. None of them is
large. Together they change what the framework is capable of being, and this
post is about why.

---

## What was actually missing

The framework had objects and it had a registry, but it had no way to say **what
kind of thing** an object was. That sounds academic until you look at what
filled the gap.

Four hardcoded lists, in four different files, each answering the same question
by hand:

| the list | the question it was really answering |
|---|---|
| `KnownClasses[]` in object.c | which classes exist to test against |
| `Panel` flag on a class | is this a composite, or a bare control |
| `ScriptHost=1` on Lua and JSScript | which classes are language hosts |
| `WIDGET_CLASSES` in app.js | which classes render as an atom rather than a panel |

And a property *scan* — `GetMainView` walked an instance's properties looking for
one whose value happened to be a View, to decide whether the thing was placeable.

Every one of those is a type question. With no type system, each got written down
somewhere it could go stale, and each did. A new control was missing from the
client's list, so it drew as a panel until someone edited JavaScript. A pure
object like `Bridge` had to be named in an exclusion list to keep it out of the
palette.

All four are gone now. The question is asked of the class:

```js
if (classParent === 'Control') { ... }        // renders as an atom
```

```c
if (GetPropLong(class, "InstanceStart"))      // can be instantiated at all
```

That is the shape of the whole change. Not "add a type system" — make the thing
that was already there, the node, carry one more property.

---

## The three mechanisms

**A class node carries a `Parent`.** That is the entire inheritance mechanism. No
registration table, no vtable, no interface list — one property naming another
class, resolved at load. `Object` ends the chain and is the only class the core
provides. `Control` (a name, a place, a size) and `Widget` (a bag of controls
with the behaviours that drive them) are loadable modules like everything else.

**A dependency names a file, a class, and a version.** All three are necessary.
The file because that is what the loader can act on *now*; the class because a
class cannot be looked up before its own `ClassStart` has run, which is precisely
the thing being ordered; the version because otherwise a module built against a
different core loads and misbehaves rather than refusing.

**Entry points are function-pointer properties on class nodes.** A header turns
`Widget_Publish(...)` into a lookup on the Widget class node plus a call through
the pointer. No `.object` ever links against another. This was not invented
today — it is how `ClassStart` and `InstanceStart` have always worked. It was
just never used for anything but lifecycle.

---

## What that unlocks

### Behaviour can live at a level

Today, `widget.h` gives fifty-five modules a *copy* of the help-panel
boilerplate. One place in source; fifty-five places in binaries. Fixing how Help
behaves means rebuilding and shipping fifty-five files.

With a real Widget class, Help belongs to Widget. One handler, one module, one
file to ship. Same for the panel conventions — Enable in the top-right, the Help
icon bottom-left — which are currently conventions enforced by everyone
remembering them.

This is the difference between a shared header and a shared *class*. A header is
copy-paste with a compiler doing the copying.

### The deployment story became a deployment system

"Support means emailing one `.object`" has been true here for twenty years. What
was missing was any way for the receiving end to know whether the file fits.

Now every module declares `Object 1 0` against the core, and every class it uses
by version. Drop in a module built against an older core and it does not load:

```
Error  'UDP' needs 'Object' version 2.0, found 1.0
```

The class never registers, so it is absent from the palette, absent from
`CreateObject`, absent from everything — rather than half-present and misbehaving
in a way someone has to debug over the phone. The rule is major equal, minor at
least what was asked, so appending a message id is a minor bump that keeps every
dependent working, and reordering one is a major bump that stops them all until
they are rebuilt.

That is what makes shipping a single file to a customer *safe* rather than brave.

### The core became small enough to be honest about

`object.c` went from 4,013 lines to 2,010 today, and what left did not vanish —
it moved into modules:

- tests → `unit_test.c`
- skins → `skin.object`
- the flow interpreter → `flow.object`
- import/export, and the JSON parser with it → `serializer.object`
- presentation, the palette, the topbar → `control.object`

What remains is registration, addressing, messaging, lifecycle, allocation
counting, and interface publication. The core does not know what JSON is. It does
not know that anything is ever *presented*. It does not know what a palette is.

The claim "the executable is a hollow host" has been in the documentation for a
long time. It is now measurable: 21 KB of executable, a 2,000-line core, and 56
classes that all arrive as files.

### Threading stops being a rewrite

An object nobody can reach into has no shared state to protect. The class system
is what makes that enforceable rather than a matter of everyone behaving: a plain
object descends from `Object`, publishes nothing, and hands its callers a private
handle plus a header of message ids.

Move that object onto its own thread and **nothing changes for its drivers**,
because they never had anything but the header. The async DNS work waiting in the
tree is the first test of this, and it is now a small piece of work rather than a
negotiation with every caller.

### Classes discovered at runtime are ordinary classes

This is the one I think matters most in the long run.

A class here is a node with a name, a parent, a version and some function
pointers. Nothing about that requires it to have come from a `.c` file. A class
description arriving from an MCP server, or registered by a script through a
trampoline handler, is the *same kind of thing* as a compiled one — same
registry, same palette walk, same `CreateObject`, same versioning.

The roadmap has wanted federated palettes — web APIs and MCP servers imported as
palette classes — for a long time. What was missing was not the import code. It
was having a notion of "class" solid enough that a runtime-created one is
indistinguishable from a compiled one. That now exists.

### The app can become data

`CreateDefaultApp` is the last thing in the host composing a system with C calls.
With the flow interpreter loadable and the serializer owning both directions of
the format, the boot app becomes a flow file the core asks a module to load.

At that point the host is: initialise, install objects, load the boot flow, pump
the loop. The composition verbs stay in the engine, but every caller of them is a
translator — the flow loader, the bridge, a script, an agent. "An application is
objects plus wiring, never a different binary" stops being a design intention and
becomes a description of the startup sequence.

---

## What is declared but not yet delivered

Honesty about the gap, because the mechanisms above are load-time facts and some
of the payoff is runtime behaviour that does not exist yet.

**The chain is not walked.** `Parent` is set, resolved and enforced at load — a
class with a missing parent does not start. But nothing yet takes a message a
class did not handle and passes it up to its parent. `rtrn_dropped` means
"unhandled" and stops there. Until that walk exists, Widget cannot actually
answer Help on behalf of its children; the taxonomy is in place and the
inheritance is not.

**Hot replacement got harder before it got easier.** `UnRegisterClass` is still a
stub that unregisters nothing, and `RegisterClass` now refuses a duplicate name.
Together that means unloading and reloading a module leaves the old class
registered and the new one refused. Stricter than the old silent
double-registration, and wrong for the "drop in the new file" story. It needs
deciding.

**Nothing enforces a declaration at use time.** `CreateObject("Textbox")` works
whether or not the caller declared Textbox as a dependency. The gate governs load
order and version compatibility, not use.

**The parent link is a raw pointer.** It cannot dangle today because nothing frees
class nodes, but it is the same shape as a use-after-free we already fixed once
this week.

---

## The through-line

Every good move today was the same move: notice that the answer is already a
property on a node, and stop keeping it somewhere else.

What kind of thing is this? A property on the class. What does it need loaded
first? Properties on the library. What version? Two more properties. How do I
call into another module? A function pointer in a property, exactly like
`ClassStart` has been since the beginning.

The framework's founding claim is that one uniform structure can hold the
registry, the configuration, the wiring and the skins. Today it started holding
the type system too — and four hand-maintained lists, a property scan, and two
marker flags went away because of it.

That is the test of whether "everything is a node" is a real principle or a
slogan. Every time something new turns out to be expressible as a node with
properties, and the special-purpose thing standing in for it can be deleted, the
principle earns its keep again.
