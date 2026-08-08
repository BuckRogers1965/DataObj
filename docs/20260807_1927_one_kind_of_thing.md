# One kind of thing

## What a property is

A property is a node.

It lives in a container, and it **is** a container. It can hold properties of
its own, and that is where everything about it is kept: the subscriptions
recorded on it, the handler it carries, the presentation a client draws it
with, any annotation an object or a script stamps on it. One level down, same
mechanism, no new machinery.

An object has as many properties as it wants. Each one can carry a handler.
Every one of them is subscribable by existing — there is no opt-in step, no
declaration, no registration. Writing a property fans out to whatever
subscribed to it; that is not a feature of some properties, it is what a
property does.

`In`, `Out`, `Enable`, `Clock`, `Value`, `Source` are **names an object chose**.
They carry nothing to the engine. Naming a property `Out` tells you what its
author meant and nothing else.

**An object may choose a direction. The engine has none and enforces none.**
A Reader only produces; a Writer only consumes. That is a real design decision,
and the names those objects give their properties announce it to whoever wires
them — but nothing reads the names. You can subscribe to a property named `In`
and write values onto one named `Out`, and both behave exactly as they would on
any other property. `Connect` takes two properties and records the subscription
on the first one you handed it; that asymmetry belongs to that one call, not to
either property. Hand them over the other way round and you get the other wire,
with no complaint and no special case.

So a direction is a convention an object adopts for its own sake — useful,
documented by its property names, and unenforced. Confusing the convention for
a mechanism is what invents limits that do not exist.

## What follows from only that

The reason this is worth stating precisely is that most of the framework's
capabilities are consequences of it rather than features built on top:

- **A probe is free.** Subscribe a second thing to a property nobody planned to
  observe. Nothing had to be prepared, because being observable is not a
  capability a property can lack.
- **Anything can drive anything's `Enable`.** A timer shutting a server down is
  one wire, because `Enable` is a property and a wire is a subscription.
- **Any property can be gated, annotated, or presented differently** by hanging
  sub-properties on it. `Widget`, `W`, `H`, `GUI_Format` are all this.
- **Many sources into one property, with one handler, is normal.** The handler
  can ask which delivery it is in (`MsgFromNode`), so there is no need for a
  stand-in object per source and no limit on how many sources a property has.
- **A composite widget is a View whose members are ordinary instances**, laid
  out by their own X/Y, addressable by path. Nothing about being inside
  something else changes what a thing is.

Each of those is one sentence *because* there is one kind of thing. Every
species you add costs you the same explanation again for each species.

## Two demonstrations, not two claims

Both of the load-bearing statements above were exercised deliberately, and it is
worth reading them as evidence rather than as assertions.

**A property was injected into one instance from the GUI, and it changed
behaviour.** `GUI_Format` is not declared by the Textbox class, is not known to
the engine, and does not exist on any other Textbox. It was created by the act
of writing it - `SetPropStr` makes a property that is not there - onto exactly
one box, and the browser then masked that box's contents as a telephone number.
A second box got a different mask for social security numbers. Neither one is a
different kind of Textbox; they are the same class with different properties on
them. From that alone: a property set is not fixed, a class does not close its
instances, and the meaning of a name is not the engine's business. It saves with
the flow, clones, and exports, because it is an ordinary property.

**A script was injected into a view, and the view got new behaviour.** A
composite gained logic at runtime with nothing recompiled: a ScriptBox placed
inside a View like any other member, wired to the other members, driving them.
Change the source and the widget behaves differently - no build, no new class,
no restart. And the script reaches its neighbours by path and by wire, using the
same mechanisms a compiled object uses, because there is nothing else to use.

Put together they answer the question that keeps getting asked wrongly. An
object is not a closed thing with a fixed set of properties and a fixed set of
behaviours. It is a node in a tree, holding whatever properties it has been
given, each of which may carry a handler, and any of which may have been added
a minute ago by a client or a script. Anything reasoning about it as closed will
invent limits - "this object can only have one of those", "this behaviour has to
be compiled in" - and then build machinery to work around limits that were never
there.

## Delivery, in the same terms

One definition, in `DeliverToSubscriber` (node.c), reached two ways: a property
write fans out synchronously, and `SndMsg` queues an envelope that
`DispatchMsg` walks later. Both hand it the same thing.

A subscription is a record on the **source** property naming the target
instance, the target property's name, and a callback. If there is a callback it
runs. If there is not, the payload is stored onto that target property — and
that write fans out in turn, which is how chains hop without anything
special-casing them.

The delivery also carries where it came from, available to the handler for the
duration of the call (`MsgFromNode`). That is a fact about the delivery, not
about the data, which is why it is ambient and not stamped on the payload: the
same payload goes to every subscriber, and a forwarded copy belongs to whoever
forwarded it.

## The rename

The code does not currently say any of this. A dozen identifiers use a word
that names nothing in this system, and an identifier at a call site beats a
document every time — you read the code while you are working, and it is the
code that teaches you the model. Fifty-eight comments now sit next to those
identifiers saying they are named wrong, which is a stopgap. The fix is that
the names stop being wrong.

What each one should say:

| now | should be | because it holds |
|---|---|---|
| `env->outPort` | `env->from` | the source property node |
| `ResolvePort()` | `ResolveProp()` | resolves a property through links |
| `Widget_Port()` | `Widget_Handler()` | creates a property and stamps its handler |
| `fromPort` / `toPort` | `fromProp` / `toProp` | the two property nodes a wire joins |
| `char *port` (params, locals) | `char *prop` | a property's name |
| `"Port"` (subscription record field) | `"Prop"` | the target property's name |
| `onPortClick` (app.js) | `onPropClick` | a click on a property's rendering |
| `portDisplays` (app.js) | `propDisplays` | readouts painted from a property |
| `pendingPort` (app.js) | `pendingEnd` | the first **end** of a wire being drawn |
| `instances[x].ports` | `.props` | a rendering's properties |

### Phasing

The harness makes this a low-risk change rather than a nervous one: the
compiler finds every missed C identifier, and twelve suites across five
variants find anything the compiler cannot. Each phase is one commit, green
before and after.

**Phase 1 — C identifiers only.** Locals, parameters, struct fields, function
names. No stored data changes and no protocol changes, so the only possible
failure is a compile error. Mechanical and safe.

**Phase 2 — the stored record field.** `"Port"` becomes `"Prop"` in the
subscription record. This is data rather than code, written in one place
(`AddSubscription`) and read in several: delivery, the delete scrub, clone,
the serializer, the bridge's wire enumeration. They change together or not at
all. Worth confirming first that no saved flow contains the old key — wires
are recorded as connect actions rather than as subscription records, so this
should not touch the file format, and that is a two-second check rather than
an assumption.

**Phase 3 — the protocol field.** `{"cmd":"subscribe","instance":…,"port":…}`
is the widest blast radius: app.js, every suite, the manual. Every client is
in this repo, so a clean break is possible; if that feels tight, accept both
keys for one cycle and drop the old one after.

**Phase 4 — docs, and delete the fifty-eight comments.** They exist only
because the names lie. When the names tell the truth the comments are noise,
and leaving them would be its own kind of lie — a warning about a hazard that
is no longer there.

## Why it is worth the churn

A framework whose whole argument is that there is *one* kind of thing cannot
afford identifiers that imply a second one. The cost is not confusion in the
abstract; it is that a reader — human or otherwise — derives constraints from
the vocabulary and then builds machinery to satisfy constraints that were never
real. The document says one thing, the code says another, and the code wins.

The rename is how the code stops arguing with the design.
