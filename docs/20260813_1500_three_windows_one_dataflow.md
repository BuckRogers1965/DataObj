# Three windows, one running thing

*2026-08-13. A design conversation, written down before any of it is built.
Nothing here is implemented. The concrete example is REST, because it is the
next one and the simplest to be concrete about — MCP is the same argument with
tool descriptions instead of routes.*

## The sentence that reframes it

> The GUI is incidental to the dataflow.

Not modesty about the GUI. It is the strongest claim in the design, and it has
a sharp consequence: **anything that only works through the browser is a hole,
not a client feature.**

`alias.object` was a hole — you could only alias with a mouse. `Bridge_Internals`'
layout is a smaller one. Every gesture still living in `app.js` is a capability
an agent does not have, and the test for the remaining work is not "is `app.js`
tidy" but **"can something with no eyes do this?"**

## The object graph is the program

The thing that runs is a tree of nodes with properties and subscriptions. It was
running before any window opened, it does not change when one closes, and a
second window does not fork it. A translator is a window: it turns some wire
format into the same core verbs, and turns core events back into that format.

There is exactly one window today, which means the claim above is currently
**untested**. That is the argument for building the second one early rather than
last: a second translator is not the reward for finishing the cleanup, it is the
instrument that tells you what the cleanup was for. It finds holes as *failures
to express something*, which is unambiguous, and it finds them faster than
reading code.

## What a REST translator needs, and what it does not

**It needs the verbs, which already exist.** `create-instance`, `connect`,
`disconnect`, `set-property`, `subscribe`, `list-instances`, `list-connections`,
`clone-instance`, `create-alias`, `move-instance`, `bind-port`, `delete-instance`,
`export-flow`, `import-flow`, `internals`. Every one is a core call the Bridge
translates to. A REST translator is those same calls behind URLs.

**It needs addressing, which is the gate.** A REST route IS a path.
`GET /Root/Filter/Mode` is not a feature anybody designs — it is the namespace
answering the question it already answers, over a different transport. But the
namespace stops at the instance: `RegisterPath`/`ResolvePath` know
`/Root/Filter`, and `Mode` has no address at all. Today the only way to read a
property is `subscribe`, which goes through `Connect`, which CREATES the property
if it is missing. So the one available read is destructive, and the one obvious
route cannot be formed.

**Everything else it needs, it already has.** The published Interface — property
names, widget types, defaults, walked out of the registry — is a schema. Nobody
writes it. Drop a `.object` in the scan path and you have added a browser
control, a REST resource and an agent tool in one copy operation.

**A prediction, written down so being wrong about it is informative: a REST
translator should need no per-class code at all.** No `show/rest` beside
`show/web`. `show/web` exists because a browser needs *presentation*; REST needs
none. If REST turns out to want a per-class half, the Interface is not as
complete a description as we think, and that is worth learning in week one.

## What `app.js` has to become

Two files wearing one name.

**The client** — `connectSocket`, `send`, `handleEvent`, `nodeProp`,
`parseInterface`, `baseName`, `cur`, and the caches of what the engine has said
(`classes`, `instances`, `propertyValues`). No `document` anywhere in it. Around
350 lines. A REST or MCP front end loads this and nothing else.

**The GUI host** — mount, modes, canvas, status line, and the routing from
protocol events into renderings. Its entire vocabulary is the classes' own
`show/web`, so a front end with no eyes never loads `widgets.js` and registers
no renderers. `rendererFor` returning nothing becomes a normal state rather than
an error, which the "a missing presentation is loud" rule already anticipated.

The seam runs through the event handlers, which currently do both jobs at once.
`onPropertyChanged` updates `propertyValues` — every client needs that — and
then pushes into `liveControls` and `menuButtons`, which is rendering. Those are
functions to split, not rows to move.

## What the Bridge has to become

Close to what it already is. create/connect/subscribe/list/save/load/clone/alias
are protocol verbs any translator wants unchanged.

The one lump that does not belong is `Bridge_Internals`: the row stacking, the
`y += mh + 14`, the 14px inset, the `PROP_TEXTBOX` floor. A caller asking for a
thing's internals wants THE MEMBERS; where they sit in pixels is a layout
opinion a translator should not hold. The members are real instances with real
positions, so the data is right — it is the algorithm that is misplaced.

Which the next section dissolves rather than relocates.

## Showing a thing is serializing that one thing

The realisation that ties it together, and the codebase already found half of
it. `IsPortableProp` exists because two walks disagreed: clone walked the class
Interface while the serializer walked the instance's real properties, so an
agent's generated `Source` survived export and vanished on clone. The fix was a
shared RULE for what counts. But there are still three separate traversals over
that rule — `CloneObject`, export/import, and `Bridge_Internals`.

They are one walk with four sinks:

| sink | what comes out |
|---|---|
| clone | new nodes, values **copied** |
| export | text, values **copied** |
| internals panel | controls, values **linked** |
| Interface / schema | names and types, **no values** |

Which is the distinction the whole system already turns on. Clone copies, alias
links — at the level of one property, export copies and showing links. **The
dissection panel is not like a serialization, it IS one**, rendered as live cells
instead of dead text.

Three things follow:

- **The layout problem dissolves.** The Bridge's y-cursor is a formatter's line
  spacing that leaked into the walk. Separate the walk from the sink and there
  is nothing left in the Bridge to be wrong: W, H and widget type are already
  per-property facts from the Interface, and where they stack is the browser's
  business and the REST sink's non-business.
- **REST's instance representation already exists.** `GET /Root/Filter` is
  `NodeToJson` restricted to one instance — a narrower path, not a new one. And
  copy-versus-link becomes a caller's choice: a GET is a snapshot, a
  subscription is a link.
- **"Show me this" and "give me this" are one call with two sinks.** The
  three-interfaces argument arriving from underneath rather than from the
  network side.

## Composition writes APIs

A View with bind-ported properties is indistinguishable from a primitive class
to a caller: published properties, inputs, outputs, a name. So somebody drags
five objects onto a canvas, wires them, binds three ports — and has published a
REST resource and an agent tool. No build, no deploy, no schema file, no code.
"An application is a set of objects plus their wiring" reaches all the way to
the network boundary, and the composite never learns it was published.

Set a few more properties and a view is a widget.

## The part that only exists because all three are windows

Connect a browser and watch what an agent is building in `/Root/mcp`.

The agent creates instances there; a browser subscribed to that container sees
them appear, wires draw as they are connected, values move as data flows. There
is no "visualise what the agent is doing" feature to write — `instance-created`
from an MCP call is the same event as `instance-created` from a palette drop,
and the canvas cannot tell which happened.

When an agent builds with code you get a diff and a log: an account of what it
says it did. Here you get the artifact, running, while it runs. You can watch it
wire something wrong, drag the wire yourself, and its next query sees your
correction, because there is one truth and neither of you owns it. Not
agent-with-human-review — two peers standing next to the same running thing.

And it is inspectable with the ordinary tools, which follows from the design
rather than from effort. Drop an `Out` probe on any wire the agent made: one
subscription, holds nothing open, and the agent never knows. Open the internals
panel on anything it built and every property is laid out. A probe was always
just another subscriber; a panel was always just a view of real instances.

This is also where the control-owns-nothing rule earns its keep past the GUI. A
control is a view onto a datum, never a holder of one — so a second observer is
free and no observer is authoritative. The tap bug fixed this morning was that
principle failing at n=2: two watchers on one property, and the second silently
inherited the first's identity. Fixing it for two controls fixed it for a
browser and an agent and a REST subscriber, because it was never about controls.

## How we will know it is true

**Cross-translator equivalence.** Build the same flow three ways — raw JSON,
REST, MCP — and compare the resulting node trees. Identical trees are the
mechanical proof that translators are syntax-only, rather than an argument that
they are. It also catches the exact class of bug that has bitten twice this
week: a translator quietly doing something the core cannot.

That is a new category of test. The existing suites prove that one translator
works; this one proves that a translator is *interchangeable*, which is the
actual claim.

## Order

1. **Properties get paths.** The gate. A REST route is an address; an MCP tool
   name is a name. Nothing else is buildable well before this, and it is the
   same sentence as the older note about naming things at too low a level.
2. **One walk, four sinks.** Unify the traversal the shared rule already
   governs. The Bridge's layout opinion dissolves rather than moving.
3. **The REST translator.** The instrument. Expect it to need no per-class code.
4. **The equivalence suite.**
5. **The `app.js` split** continues underneath — each thing pulled out of the
   GUI host becomes immediately checkable by a window with no eyes.

The GUI relocation stops being the headline and becomes maintenance, which is
the right demotion. It was never the point; it was the residue of the point.

## The same evening, in the one window we have

Everything above is about building a second window. What follows is what the
first one cost us the same night, and it reads as an argument for the rest of
this post: four bugs, three of them visible only to a pair of eyes, and the one
question the browser could not settle went to the harness instead.

All four are the tail of the morning's work - deleting the Alias class - and
every one is the same shape: a rule that had been living inside the species.

The class was gone by lunch and the suite was green across five build variants.
The evening was spent finding what the deletion had quietly taken with it, and
every one of them was the same shape: a rule that had been living inside the
species.

### Alias_1 through Alias_14

Open any object's Options panel and every row was called `Alias_4`. Not the
property it stands for — the gesture that made it, plus a counter. One literal,
`Bridge_Internals`:

```c
Bridge_FreshAlias(local, viewAlias, "Alias", memberAlias, sizeof(memberAlias));
```

It had never mattered, because the client's alias rendering captioned each row
from `TargetProp`. Delete the species and the rows fall back to the rule every
control follows — a control wears its own name — and the panel becomes a column
of anonymous numbers.

Naming a thing after the gesture that made it is the same error the class was.
Nothing there is "an alias": it is a Textbox, a Checkbox, an LED, a View, each
pointing at one property. A cloned slider isn't called `Clone_3`.

So the row is named for the property it stands for. The caption comes from the
existing rule with no client change, and the row gets an address that says what
it is — `/Root/V/VPanel_1/W`, not `/…/Alias_5`.

### The label rule that should not come back

There was a roadmap gap saying a doorway must wear the name of what it opens —
written when the deleted rendering did exactly that. It's wrong, and the correct
version is duller: a control wears its own name, like everything else, and if
you want the caption to read something different you rename the control.

What made the panels unreadable was never the label rule. It was the names. Fix
the names and the labelling rule you were about to write disappears. The gap got
struck to its remaining half — a doorway must OPEN what it points at, which is
still unasserted and still worth a test.

### One clone path, because there was only ever one kind of thing

Then: cloning a compiled widget produced its sub-views and no controls at all.
Fifty `FAILED` lines in the log, and the clone verb reporting success.

`Widget_Ctl` builds a panel control by LINKING its `Value` to the property it
shows. `IsAlias` — the new, honest test, "is this thing's slot a link" — is
therefore true for every control in every widget panel. The clone walk sent them
all down its alias branch, and that branch required a `TargetProp` string only
the alias gesture writes. Every one of them returned NULL.

The class-name test had hidden this. `strcmp(classname, "Alias")` caught exactly
the instances of one class; `IsAlias` catches everything that IS one. Making the
question honest is what exposed that the branch had two implementations behind
it — and that they had drifted:

```
CloneObject      walks every portable property (IsPortableProp)
CloneAliasNode   carried { "Widget", "Label", "X", "Y" }
```

No `W`, no `H`, no `LabelPos`. So a cloned control arrived default-sized with
its caption back on, and anything added to a control later would have gone
missing the same silent way.

The fix wasn't to repair the second copier. In a widget's panel every member is
a control pointing at data, so the branch decided nothing — it picked which
implementation ran, and every member picked the same one. There is one kind of
thing, so there is one path: pass 0 clones every member, pass 1 re-makes the
links every member carried, pass 2 wires. `CloneAliasNode` is deleted.

Two things came free. `CloneObject` now skips link slots instead of copying the
string a link currently reads — the hazard its own comment had warned about for
weeks. And relinking walks every property rather than just `Value`, so a
composite whose own property is bound to an inner member's comes across bound.

### A dropdown with a value and no options

The MCP agent's Language menu came up blank while the agent cheerfully ran Lua.
The value was right; the OPTIONS were missing. The deleted rendering had known
the convention:

```js
Items: { instance: rec.target, prop: rec.targetProp + 'List' },
```

`Widget_Ctl` implements the same convention for widget panels. So one of the two
makers of controls honoured `<prop>List` and the other didn't, and which one you
got depended on how the control came to exist. It moved into `AliasProperty` —
the one place an alias is made — as a link rather than a copy, so the options
stay live, survive a rename, and need nothing re-made on a clone.

### What the harness said, and what it couldn't

The suite stayed green through the widget-clone bug because nothing in it clones
a compiled widget: the clone tests clone Views full of plain Sliders, which only
ever exercise the concrete path. A new test clones a palette TCPPort and checks
the copy holds every member at every depth. It fails on the morning's commit.

The failure had also been loud and not fatal — fifty log lines, a verb reporting
success, and nothing in the harness that fails a run for what is in the log.
That is the general repair, and it is now written down: a relink failure is an
`ERROR`, and a variant fails when an `ERROR` line appears.

Then the harness returned the favour. We had commented out the engine's `Target`
write to see what needed it; the browser showed no difference, because the
bridge computes the same string on its own create paths. Six failures later:
import and load go through neither of those paths, so an imported alias worked
as a link and could no longer say what it pointed at. Which settles the
ownership question the right way round — a serializer has to export and import
with no bridge loaded at all, so `Target` is the engine's, and it is the
BRIDGE's writes that now need justifying.

One of the six failures was the new test asserting something the design
deliberately doesn't do: that a clone's members are announced to a client that
never looked at them. A connection is told about creations in containers it is
viewing, and nothing else. Asking is the announcement.

### The shape of the day

Every bug tonight was a rule that had been living inside a species: the caption,
the menu's options, the copier, the recorded target. Deleting the species is
what made them visible, one at a time, in the order someone happened to look.
None of them were regressions in the usual sense — they were the bill for having
had two of something, arriving after the second one was thrown away.
