# Punting up the chain

*The capability we already had, 15 August 2026.*

We went looking for a missing containment index and found an empty slot, a
verdict with nowhere to go, and a punt with its destination nailed down. None
of the three is a new mechanism. All three are the same mechanism, half
finished, because until last week there were no classes to finish it with.

## What we were actually looking for

The REST object needs to answer "what is in this view". It does it by walking
every instance in the session - library, class, instance - and string-comparing
each one's `Container` property. That is the third copy of that walk in the
tree (`bridge.c:2358`, `serializer.c:1265`, `rest.c:412`), and the third was
written yesterday without noticing the other two.

Seventeen registry-wide walks exist now. The obvious fix is a containment index
in the core. That is also the wrong fix, and the reason it looks right is
historical: before last week there was one kind of thing, everything was an
Object, and every mechanism piled into `object.c` because there was nowhere
else for it to go. 2701 lines of nowhere else.

There are 58 classes declaring a parent now - 28 under Widget, 18 under
Control, 9 under Object, 3 under Script. Containment has an owner it did not
have a month ago. Knowing your members is the containing class's business,
exactly the way `view.js` already says about layout: "members lay out by their
own X/Y stops being something the framework imposes and becomes THIS class's
opinion, which a Row, a Grid or a table-of-records can subclass and replace."

## The empty slot

Instances are children of their class:

    AddChild(class, Instance);          /* object.c:2536 */

Nothing anywhere walks an instance's children. Not the bridge, not the
serializer, not a single object. The slot has been empty since it existed.

So a View's members go there. The container keeps what it already gets told -
`RegisterPath` finds the container and writes `LastMember` on it (`object.c:60`),
described in its own comment as "the one place every creator meets: import,
clone, a bridge create, an object building its own panel." The announcement has
always been there. Nothing kept it, because `LastMember` is one string built to
be subscribed to, and a property that changes is an event, not a record.

We have been told about every member at the moment it arrived, and thrown it
away, and then walked the whole registry to work out what we had been told.

## The verdict with nowhere to go

`callback.h` has had three verdicts all along, and `node.c` spells out what
they mean at delivery:

    rtrn_handled     the handler took it
    rtrn_propagate   the handler watched it and did not consume it
    rtrn_dropped     nothing took it

`rtrn_dropped` already means *not mine*. Today it ends there. It is the punt
signal, fully specified, already returned by every handler that declines a
message, and it has no destination.

Give it one and the ladder writes itself:

- an **instance** that drops a message punts it to its class
- a **class** that drops it punts to its parent class
- **Object** is the end of the chain: handle it or drop it, and a drop there is
  a real drop

No new return code, no new call, no registration step. The thing that decides
whether a message keeps travelling is the value handlers have been returning
since the beginning.

## The punt that already exists

There is already a punt in the tree, and it works. `control.h`:

    static inline long ControlEntry(char *name)
    {
        static NodeObj cls;

        if (!cls)
            cls = FindClass("Control");
        ...
        return GetPropLong(cls, name);
    }

An object that wants `Widget_Create` does not implement it. It punts to the
Control class and calls what it finds there. `widget.h` does the same thing to
Widget. That is inheritance, working, in production, across sixty-odd modules.

What it does not do is walk. The destination is a literal - `FindClass("Control")`
- cached in a static. It punts to exactly one place, chosen at compile time by
which header you included. The pattern is proven; only the last step is
missing, which is asking the thing you are punting *from* who its parent is
instead of hard-coding the answer.

## What finishing classing means

`SetClassParent` stores the parent's **name**:

    SetClassParent(ClassSelf, "Widget");

A string, resolved by lookup, because when it was written there was nowhere
structural to put it. Make it a link - a class node becomes a child of its
superclass node - and `GetParent` walks the whole ladder with no lookup at all:

    instance -> its class -> Widget -> Control -> Object

`GetParent(instance)` already returns its class. The rest of the chain is the
same call, repeated, and the core already builds the top of it:
`RegisterCoreClasses` makes Object with no parent, Control under Object, Widget
under Control. The declarations are there. Only the edges are strings.

Two things move when they stop being strings.

**Provenance becomes a property.** The class child list currently encodes
"which .object provides this class" - that is what `RegObjList -> library ->
class` is, and `UnloadClasses` walks it. But a class comes from exactly one
library, so that relation is single-valued and belongs in a property. The rule
falls out: the walkable relation gets the tree, the single-valued relation gets
a property. It is currently backwards, and that is the whole reason subtyping
ended up as text.

**The child list is the hierarchy, at every level.** Not two lists, and not a
discriminator - one relation, which is "what is below me":

    Object
     +-- Control                  Control's children are the control classes
          +-- Button
          |    +-- instances of Button
          +-- Widget
               +-- TCPPort
                    +-- instances of TCPPort

A class with subclasses has them as its children. A leaf class has its
instances. An instance has its members. Walking down from Control gives every
control class; walking down from Button gives every Button in the session;
walking down from a View gives what is in it. It is one walk with one meaning,
and `RegObjList -> library -> class -> instance` was already that walk with
provenance wedged into the middle of it.

Which is where the class's library goes: onto the class as a property. A class
comes from exactly one file, so that relation is single-valued and does not
want a tree. `UnloadClasses` asks each class where it came from instead of
asking each library what it brought.

The general form is worth stating on its own, because it is what makes any of
this cheap: **a property is a node, and a node has children and siblings, so a
property can BE a tree.** That is not a trick, it is in production - `PublishProp`
does `AppendChild(interface, entry)`, and every class's `Interface` is a
property whose children are its published properties. So a new relation between
things is never a change to the core. It is a property, added when somebody
needs it, holding whatever shape that relation actually has. Which also means
none of this has to land in one move: the member list can go in before the
parent links do, or after, and neither waits on the other.

## What the tree does not mean

One walk with one meaning is only true if the tree is not asked to carry
relations that are not it. Two that are not, and both are already right.

**Composition is not inheritance.** TCPPort's parent is `Widget`. Not TCP - it
drives a socket it does not inherit from, created through that class's own
`InstanceStart` and reached only through `tcp.h`. The raw engines are side
loaded for exactly this reason: nine classes sit directly under `Object` and
they are all engines - tcp, udp, dns, tcpshim, script, flow, skin, skeleton,
and control, the root of presentation itself. An engine has no presentation and
nothing to inherit from a Control, so it stays out of that chain entirely and
is composed rather than subclassed. That is why DNS landed as an object last
week with zero lines changed in the core: it had nothing to join.

So a widget holds three different kinds of relationship at once, and only one
of them is the ladder:

    is-a       its parent chain      where a dropped message punts
    contains   its children          what is in the bag
    drives     a private handle      an opaque peer, in no list at all

Put the other way round: **DNS is a behaviour you can add.** It is not a kind
of thing, so it does not want a place in a hierarchy of kinds. Nothing becomes
a DNS by resolving a name. A class chain answers "what am I"; composition
answers "what can I do", and adding a capability must not change the first
answer. An engine that arrived as a superclass would drag its whole ancestry
into everything that needed one small thing from it.

The toolbox is the right picture, including the part people skip. You reach for
a hammer when you need to drive a nail - and you do not keep nails in the
toolbox. The tools live there; the material passes through and goes into the
work.

So a behaviour holds what it IS and never what went through it. DNS does not
keep the hostnames it resolved; the answer leaves as a message and the pending
record is torn down. A Reader's chunks are not retained on `Out` - that property
exists to hold the subscription list and give `Connect` an endpoint, and the
data flows across it. `SndMsg` takes ownership and `DispatchMsg` frees the
payload after the last subscriber, which is why `EnvelopeCount` reads zero at
rest. A property that quietly accumulates what an object processed is a box of
nails in the toolbox: it will be full, and nobody will know which ones matter.

The exception proves it rather than breaking it. A Queue does hold the
material - because holding it is what a Queue IS. Its retention is its
function, not a side effect of doing something else, and it hands the material
back on a Clock rather than hoarding it.

Which is also what makes a behaviour shippable. It is its own `.object` file,
it inherits nothing from the thing that will use it, and it hands out no nodes
- so installing a capability is copying a file, and using one is creating a
handle. That is the same property as the deployment story: a fix can be a
single object emailed to a customer precisely because objects are composed
rather than woven into each other.

**A container's children are heterogeneous on purpose.** A widget is a bag of
things with different parent classes - a TCPPort panel holds Textboxes,
MoButtons, LEDs, a Dropdown, sub-Views - and they have nothing in common except
being in it. That is the difference between the two uses of the child list, and
it is not a smell: subclasses under a class share a parent by definition,
members under an instance share nothing by definition. A walker standing on a
class is asking "what is a kind of me". A walker standing on an instance is
asking "what is inside me". They are different questions asked at different
levels, and neither can be mistaken for the other by anything that knows where
it is standing.

The third relation is the one to keep out of the tree deliberately. A handle is
opaque: no node of the engine's is ever handed out, and the widget holds it
through the header's contract and nothing else. Putting it in a child list
would make it walkable, and the whole point of `tcp.h`, `udp.h` and `dns.h` is
that it is not.

## The load order was for this

Here is the part that makes it buildable now rather than after another change.

A parent stored as a **string** does not care what order anything loaded in -
you resolve it later, by name, whenever someone asks. A parent stored as a
**link** requires the parent's class node to already exist at the instant the
subclass registers. That precondition is the whole reason dependency-ordered
loading was worth insisting on, and it was built last week.

`DependencyMet` (`object.c:372`) does not merely check that a file loaded. It
looks the depended-on **class** up in the class index and version-checks it:

    depClass = classname ? (NodeObj) NSSearch(ix->classes, classname) : NULL;
    if (!ClassVersionOk(depClass, GetPropStr(entry, "Major"),
                        GetPropStr(entry, "Minor")))
        return 0;

A dependency is met only when the class exists as a node. And every module
already declares its parent as one:

    AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
    AddDependency(temp, "widget.object", "Widget", "1", "0");

So at the moment `SetClassParent(ClassSelf, "Widget")` runs, the Widget class
node is guaranteed to be there. The link can be made in that same call - no
deferred resolution, no fix-up pass, no "resolve parents once everything is
loaded" phase that would need writing and would need testing and would be the
next thing to go wrong.

The dependency graph IS the class hierarchy. It is already declared per module,
already version-checked against the core's ABI anchor, and as of last week
already sorted so that nothing starts before what it inherits from. The
ordering work was not tidiness. It was the precondition, and it landed first.

And it was built to scale, on purpose. Not a repeated scan of the pending list
looking for something whose dependencies are satisfied - that is the shape that
turns a few thousand packages into a coffee break, and it is the shape people
reach for first. Two trie indexes, one for files and one for class names; a
`Met` stamp per dependency entry so a satisfied edge is never re-tested; rank
once, sort once; a circular queue that requeues what is not ready yet; and a
stall detector at twice the queue length that names what is actually missing
instead of hanging.

Fifty-five objects do not need that. Federated palettes do - web APIs and MCP
servers imported as classes, which is where the roadmap goes and where the
class list stops being something a person types. The headroom was the point.

Which throws the current state into relief. **Loading scales. Running does
not.** Every "what is in this container" question is a walk of every instance
in the session, and there are seventeen of those walks. We sorted the boot and
left the runtime doing linear scans for facts it was handed at the door. The
work below is the other half of the same property, and it is the half that runs
ten thousand times a second instead of once.

## What it retires

The three container scans stop reconstructing something the container already
knows. A Grid or a table-of-records inherits its member list for free and can
keep it in whatever order its own layout wants. Event handling gets a real
ladder instead of a fixed target. And `object.c` stops being where behaviour
goes when it has nowhere else to live, which is the actual disease those 2701
lines are a symptom of.

None of that needs a new core mechanism. The core keeps doing what it already
does - announce at the one choke point every creator passes - and the classes
decide what to keep and what to punt.

## Free now, required later

Nothing here is being fixed because it hurts. Fifty-five classes and a session
you can count by hand do not care how the answer is found.

It is worth doing now because right now it is free. The information already
exists: `RegisterPath` has already resolved the container and already written
to it, so keeping the member is an append at a choke point that is already
executing. The verdict already exists: handlers already return `rtrn_dropped`
when a message is not theirs. The chain already exists: every module already
declares its parent, and load order already guarantees the parent is there.
The slot already exists and is empty. Every part of this is built. What is
missing is the wiring between parts that were each built for their own reason.

Later it stops being free twice over.

The algorithm gets worse than it looks. A container listing is O(total
instances), so listing every container in a session is O(containers x
instances) - quadratic in session size, from an operation the GUI performs on
every view that opens and the REST surface performs on every request. At
hundreds of classes in trees and tens of thousands of nodes, that is not a
slower version of today. It is a different program.

And the change itself gets expensive. Seventeen walkers is a morning while the
walks are small and the tree fits in your head. It is a different job once
those walkers are load-bearing in sessions nobody wants to restart, and a much
worse one if the fix has to be retrofitted under a profile rather than designed
in while it is still one line at a choke point.

The right time to make a scan into a lookup is while nobody would notice
either way.

## Tomorrow

Finish classing. Object becomes a real class in the chain rather than a name at
the end of one, parents become links, the ladder becomes `GetParent`, and we
stop walking the whole tree for information we were handed at the door and
threw away.
