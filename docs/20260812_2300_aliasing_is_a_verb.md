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
