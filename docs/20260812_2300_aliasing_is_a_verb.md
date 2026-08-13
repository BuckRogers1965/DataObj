# Aliasing is a verb

*2026-08-12. A plan for tomorrow. Nothing here is implemented, and nothing
here is a bug report — the system does the right thing today.*

## Start with what is not wrong

Aliasing works. You drop an alias of a slider's value into a panel and it
shows the slider's value; you drag it and the original moves; you save the
flow and it comes back. Options panels are built out of aliases and they are
correct. The engine resolves a link on every read and every write —
`ResolvePort` under `Bridge_Subscribe`, `ResolveOwned` under the plain
setters — and that part is exactly right and should not be touched.

This is not a repair. It is the removal of a duplicate, and the bar is
therefore **identical behaviour**, not better behaviour. If an options panel
looks or behaves any different afterwards, the change is wrong.

## What we found

`Alias` is a class. It has a directory, a `.object`, an `InstanceStart`, and a
hard dependency declared in the Bridge:

```c
AddDependency(temp, "alias.object", "Alias", "1", "0");
```

Four places create instances of it: the `create-alias` command
(`bridge.c:616`), the internals-view builder (`bridge.c:838` — **every options
panel row is one**), the import pass (`serializer.c:734`), and
`src/object.c:807`.

But there is nothing an alias IS. Look at what creating one actually does:

```c
inst = CreateObject(home, "Alias");
LinkPropertyAs(inst, "Value", target, prop);
```

An instance whose `Value` points somewhere else. That is a **control that
redirects internally** — and the redirect is invisible, because the engine
resolves it. What you see on screen is a slider.

Two tells, in the system itself:

- **`Alias` is not in the palette.** Forty-three classes seed it; Alias is not
  one of them, because you cannot drop one. A class you cannot instantiate
  like a class is not one.
- **Clone needs no `clone.object`.** Clone also makes something appear where
  you dropped it, and it answers "what class?" from the thing being cloned.
  Alias asks the same question about the property being pointed at, and
  answered it by inventing a species.

## Why it became a class

Because it needed an instance to exist. Move writes X/Y on something that is
already there; Connect adds a record to a property that is already there.
Alias is the one gesture that makes a NEW visible thing, so somebody had to
answer "what is it?" — and "instance" got read as "class", when the instance
it needed was a control.

## Being an object buys nothing here

An object has strictly LESS reach than the core, not more. `libframework.so`
holds the registry, the task list and the node tree; a `.object` reaches them
through the same exported API the core calls internally. Nothing a module can
do is unavailable to the core. Being an object buys exactly two things:
deployment (ships as a separate file) and timing (loaded at runtime).

So there was never a capability argument. Walking the classes, reading a
widget type, `CreateObject`, `LinkPropertyAs` — all core code calling core
functions.

Which suggests a sharper test than "where does this feel like it belongs":

> **Functionality or mechanism?** Functionality is what an app is made of —
> Reader, Filter, TCP, ScriptBox. Mechanism is what every app needs whatever
> it does — `Connect`, `CreateObject`, `DeleteInstance`, path resolution.

Aliasing is the second, and in the same category as `Connect`: a relationship
between two existing things, expressed once, used by every app. Nobody would
ship `connect.object`.

CLAUDE.md states the rule one way — *features never go in the host; if a
feature is tempting there, it is an object.* The converse was never written
down, and is what bit here: **if a mechanism is tempting as an object, it is
core.**

## Where it already lives

`src/object.c:767` `AliasProperty()` and `:795` `CreateAlias()` have sat there
with **no callers** since before any of this. They are not dead code — they
are the seam. The Bridge went around them and did the job itself, and that
detour is where the loadable class, the hard dependency, the `Widget` stamp in
saved flows, the `strcmp(className, "Alias")` in the serializer, and 156 lines
of client rendering all came from.

**One detour, five consequences.** Which is also why this is smaller than it
looks: not "write a core verb" but "delete three private copies of one".

## The verb

`CreateAlias(container, target, prop)`:

1. Ask the target what kind of property that is — its class's published
   interface gives the widget type.
2. Ask the registry which class declares `Renders` for that type. (This is why
   `Renders` moved onto the class node: the core must answer "what shows a
   property of this kind?" with no browser in sight.)
3. `CreateObject(container, thatClass)` — an ordinary control.
4. `LinkPropertyAs(inst, "Value", target, prop)`.

`AliasProperty(inst, target, prop)` is step 4 alone, for a control that already
exists. Both signatures are already in the file; the change is that step 3
stops saying `"Alias"`.

**Late binding falls out.** A property with no published entry — one that does
not exist yet, or one a script grew — has no declared widget type, and the
honest answer is a Textbox. You can alias a property that is not there yet,
and it is text until something says otherwise.

## The one thing to be careful about: do not store the link twice

An alias today carries `Target` and `TargetProp` as properties AND a
node-level link. That is two copies of one fact, which is the shape that
wedged the engine on 2026-08-11.

The live truth is the link. `Target`/`TargetProp` become **derived** —
computed at save time with `PathOfInstance`, rebuilt into a link at load time.
That is precisely how `Connect` already works: subscriptions hold live
pointers and serialise as paths. Aliasing stops being a special case and
starts matching the mechanism next to it.

The `Widget` stamp disappears entirely. The class IS the widget.

## What becomes the same thing

A panel row and a dropped alias become one construction:
`CreateAlias(panelView, object, propname)`, once per published property. The
internals-view builder stops being bespoke. Biggest simplification, and also
the biggest blast radius — every options panel in the system.

## Sharp edges, in the order they will hurt

1. **Saved flows.** Existing files contain `class: "Alias"` instances with
   `Target`/`TargetProp`/`Widget`. They must keep loading. That is one
   translation in the import pass — *class Alias + Widget N → create the class
   that renders N, link it* — which means the NAME `Alias` stays understood
   long after it stops being instantiable. **Write this first**, with a test
   that loads a pre-change flow, so nothing can silently orphan saved work.
2. **Dangling links.** If a target dies, the pointing control's link goes
   nowhere. `ResolvePort` returns NULL and `Connect` already refuses to
   overwrite a dangling link rather than clobber it, so the machinery exists —
   but the POLICY is undecided. Given this week: visibly dead beats silently
   blank.
3. **Clones.** Cloning an object clones its panel rows, and those rows must
   link to the CLONE's properties, not the original's. `CloneConnections`
   already does this relink for wires; aliases should ride that path rather
   than grow a second one.
4. **Chains.** Aliasing an alias collapses to the original at creation, which
   is what the code does today. A stored chain is a second copy of a
   relationship that already resolves.

## How to do it safely: two implementations, one forwarder

Implement the new behaviour in the core **under a different name**, beside the
existing one, and make `CreateAlias` a one-line forwarder to whichever. The
three callers keep calling `CreateAlias` and never know which they got.

- Flipping is one line. Reverting is the same line.
- **"Identical behaviour" becomes testable by construction.** Run the whole
  harness with the forwarder on old — that is the baseline. Flip, run again.
  Anything that differs names itself, and the failing run is the useful one:
  it says exactly which behaviour the old path had that the new one did not
  reproduce. No reasoning about equivalence, just two runs and a diff.
- **Nothing is deleted.** When the new path is trusted the old one stays in the
  file, unreferenced. Especially right here, because the thing being replaced
  is not broken.

Two conditions for the swap to be clean: the two paths must share no mutable
state (or a flip gives a hybrid rather than one or the other), and the
saved-flow shim must sit OUTSIDE the switch, landed and green before the fork
exists — otherwise the baseline is not a baseline.

There is precedent in the tree: `node.c` carries `SetPropLongOLD` and the
`old` setters from when the Intercept path was parked.

## Order

1. The saved-flow shim, with a test that loads a pre-change flow. Green.
2. The new implementation beside the old, forwarder pointed at OLD. Green —
   this proves the fork changed nothing.
3. Flip the forwarder. Run everything. Fix until identical.
4. Switch the callers one at a time — Bridge command, then panel builder, then
   import — green in between. The panel builder is the one to slow down for.
5. Delete the client's alias rendering (~156 lines) once nothing makes an
   `Alias` instance.
6. Deactivate `objects/alias/`. Deactivate, not delete.

## What it retires

`alias.object` and the Bridge's dependency on it. The `Widget` stamp carried
on instances and persisted into saved flows. `strcmp(className, "Alias")` in
`ImportPlace` and the deferred pass keyed on a class name — the deferral
stays, because restoring a link before its target exists is an ordering fact
about graphs, but it keys on links, not on a species.
`registerAliasAtom`, `renderAliasControl`, the `aliasAtoms` map, and the
`className === 'Alias'` branch in the client. And the options panel stops
being a second construction of the same idea.

## The abandon condition

The value here is removing a duplicate, not repairing a fault. There is no
urgency and nothing is at risk if it never happens. If the new path cannot be
made identical, the forwarder stays pointed at old, both implementations stay
in the tree, and this document gains the specific reason it could not — which
is worth more than a half-finished migration.

---

## What actually happened (appended 2026-08-13)

It took about an hour to move, and most of a day to find out what else had been
holding it up. The plan above was right about the shape and wrong about the
first step, and the way it was wrong is the most useful thing in here.

### How a gesture became an object

Start from what a user does. They drag a slider's Value into a panel and let
go. That is one gesture, and it is the same category as Move and Connect: a
relationship between two things that already exist, expressed once.

But Move writes X/Y on something already on screen, and Connect adds a record
to a property already there. Alias is the only one of the three that makes a
NEW visible thing — and somebody had to answer "what is it?"

**"It needs an instance" got read as "it needs a class."** That is the whole
mistake, in one substitution, and everything else was downstream:

- a class needs a module, so `objects/alias/` was born;
- a module you depend on needs declaring, so the Bridge grew
  `AddDependency(temp, "alias.object", "Alias", "1", "0")`;
- a class name is what a saved file records, so `strcmp(className, "Alias")`
  went into the serializer's import pass;
- a class the client cannot otherwise draw needs its own renderer, so
  `registerAliasAtom` / `renderAliasControl` / the `aliasAtoms` map went into
  `app.js` — 188 lines by the end;
- and because the class had to carry what it stood for, `Target`/`TargetProp`
  were persisted next to a node-level link, two copies of one fact.

Five consequences, one substitution. And the tell was there the whole time:
**Alias was not in the palette.** Forty-three classes seed it and Alias was not
one of them, because you cannot drop one — you can only make one by pointing at
something. A class you cannot instantiate like a class is not one.

The instance it actually needed was a control. A slider's Value shows as a
Slider. A pulse's Interval shows as a Knob. A property nobody has described
yet shows as text. There was never a species; there was a control that gets its
data from somewhere else.

### Turning it back into a verb

`CreateAlias(container, target, prop)` and `AliasProperty(inst, target, prop)`
had been sitting in `object.c` since before any of this, with no callers. Not
dead code — the seam. Three places had gone around them and done the job by
hand, and a fourth (`CloneAliasNode`) did it again inside the core.

The verb is four steps and no new mechanism:

1. resolve `(instance, prop)` through any link chain to the pair that really
   owns it — `ResolvePort`, already there;
2. ask what kind of property that is — the owner's class interface, else what
   the property itself declares, else text;
3. ask the registry which class says it renders that kind — `FindClassRendering`,
   the same walk `FindClass` does, asking *what* instead of *who*;
4. `CreateObject` that class, and link its `Value` to the target's property.

Step 3 is why `Renders` had to move onto the class node the day before. The
core has to answer "what shows a property of this kind?" with no browser in
sight, and a class states it rather than anyone inferring it.

Then the three callers became one line each — the Bridge's `create-alias`
command, the panel builder, the import pass — and the fourth (`CloneAliasNode`)
stopped naming a class at all: the copy of an alias is the same control the
source is, pointing at the copy.

**A panel row and a dropped alias are now one construction.** That was the
biggest simplification and the biggest blast radius, exactly as predicted.

### The step that turned out to be free

The plan makes a saved-flow translation shim step 1 — *class Alias + Widget N →
create the class that renders N* — the thing everything else depends on, to be
written first with a test so nothing silently orphans saved work.

It was never needed. A saved instance already carries `Target` and
`TargetProp` as ordinary properties, so `ImportPlace` recognising an alias by
what it CARRIES rather than what class it CLAIMS makes files written on either
side of the change identical to the serializer. An old file names a class
nobody registers any more and loads anyway, because the class name was never
load-bearing.

Generalised: **a compatibility shim is evidence you are still asking the wrong
question.** Only the belief that the class name mattered made the shim look
necessary. Worth testing against the next migration that seems to need one.

The same rule then retired every other place that asked "is it an Alias":
`IsAlias(inst)` in the core answers "does this stand for somebody else's
property" by looking at whether its `Value` is a link. Asking what class it is
was always answering a different question — it just happened to agree while
exactly one class did this.

### What the deletion cost, which was nothing

`web/app.js` went 2102 → 1914 lines. The alias map, both renderers, the
dispatch branch, the property-changed branch, the teardown and rename
bookkeeping, and two widget-type constants only that renderer read.

Then the suite ran identically to the run before it. That is the definition of
dead: 188 lines whose removal no test could detect, because nothing had been
class `Alias` since the morning.

One near-miss worth recording. `widgetClassForType` looked dead too — one
occurrence in `app.js`, its own definition. It is called three times by
`objects/control/show/web/control.js`. The controls' JS is concatenated into
`widgets.js` and shares one global scope, which is what made the whole
presentation migration work in the first place, and it means **"unused in this
file" is not evidence in this codebase.**

### What only broke once it was uniform

Two failures that had nothing to do with the refactor and could not have
surfaced before it.

**A node's address is not a unique key.** `Bridge_FindTap` identified a tap by
which node changed — safe only while one name ever reached one node. Links have
never guaranteed that, but exactly one class exercised it, so it held. The
moment aliasing became ordinary, two controls legitimately pointed at one
property, the second subscriber silently joined the first one's tap, and every
update went out under somebody else's name. Writes worked the whole time,
because those travel DOWN through the link; only updates coming back UP through
a tap were lost. The tap key now includes who asked, and one change is emitted
to every tap on the node.

The bonus in that fix is the better half: **one property, one node, one engine
fan-out, however many controls point at it.** The multiplication into
per-name events happens in the bridge, at the edge, where a client's addressing
is the only reason it exists. Watching a datum from ten panels costs ten JSON
events on the wire and nothing extra in the fabric.

**Two published types had nobody to render them.** `ReservedViewOpen` publishes
as `PROP_ICON` and nothing claimed it, so an Open doorway could not be aliased
and every options panel silently dropped that row. The answer was one word
long — *the view is the icon* — and the search had been backwards: the question
looked like "what should we build to draw an icon" when the thing that draws it
is the most obvious object in the system. A missing renderer for a published
type is a question about which EXISTING class owns that meaning.

And `X`, `Y`, `W`, `H`, `Container` all publish as `PROP_NULL`, which is the
enum's own way of saying *no particular control*. Asking which class renders
no-control is a category error; it now falls through to text, and five rows
came back to every panel.

### The harness had no opinion about any of it

The tap bug was found by hand, in a browser. Nothing in the suite exercised two
names on one node, so nothing failed.

So the suite gained a way to say what the engine SHOULD do when it does not do
it yet: `Report.expect(..., roadmap="…")`. Such a check is measured every run
and listed under NOT YET rather than counted as a failure. Two rules keep that
from decaying into "red is normal", both enforced in code — it must fail for
its stated reason, and **a not-yet that PASSES is a failure**, so a declaration
cannot outlive the work it names.

That second rule caught its own author four times in one afternoon. Every time,
the observed line was correct in every particular and the run was red purely
because a declaration had gone stale. Better than trusting anyone to remember.

The twelve protocol tests that broke were all one thing — `class == "Alias"` —
and they were never testing the design. They were testing an implementation
detail that leaked into an assertion. The eleven GUI ones reached for
`aliasAtoms`, which is the same mistake one layer up: a map of a gesture's
products, kept as though performing the gesture created a category. `guitest`
has both, three lines apart — `set_mode('Alias')` is the gesture and survives
everything; `aliasAtoms` is the class and never existed.

### Numbers

```
objects/alias/          deleted        (a class, a module, a dependency)
web/app.js              2102 -> 1914   (-188, dead the moment the class went)
four hand-written copies of the verb    -> one call each
five branches on a class name           -> IsAlias, one question about the thing
one planned compatibility shim          -> not needed
```

The core changes were minutes and worked first time. The day went to the places
where one thing had been pretending to be two — which is the same lesson as
every previous entry in this series, arriving from a new direction each time.
