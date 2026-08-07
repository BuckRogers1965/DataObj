# Deliveries carry their source

`DeliverToSubscriber` now takes the source property node and makes it available
to the handler through `MsgFromNode()`.

Both callers already had it and were dropping it one line before the call:
`FanOutSubscribers` has the property that changed, `DispatchMsg` has the
envelope's `outPort`. Saved and restored rather than assigned, because property
fan-out is synchronous and a handler that writes a property nests another
delivery inside the one already running. No handler signature changed and
nothing is allocated per message.

## What it closes

That gap is what forced the bridge to invent an identity carrier.

A subscription used to mint a **tap** - a bare `NewNode` with no container, no
path and no owner, kept alive only by the address stored in a `Subscriber`
record. When the watched property died the tap was stranded somewhere nothing
could reach it. Freeing them needed a hand-written reaper that exactly one call
site remembered to call, so every other route to `DeleteInstance` - a script's
`destroy`, an object dropping a sub-object, an internals member discarded -
leaked one. 258 allocations, 11.6 KB, per browser session.

Now the Bridge carries a `Taps` property and each watched property is one
record as a child of it, identified by `MsgFromNode()` rather than by a sink
object of its own. Owned, so `DelNode` frees them. Enumerable, so every live
subscription in a session can be listed. `Taps` is LONG-valued on purpose:
`IsPortableProp` skips LONG properties, so it stays out of clone, the options
panel, `GUI_` props and the serializer.

## Three test fixes, and what each was really looking at

**The gesture test wired to the icon body, which is not a wire endpoint.** A widget
renders as a view, and a view's wire endpoints are the stand-in dots -
`addStandInMark` / `onStandInClick`, resolved by `completeWire` into one
ordinary connect. Both icon clicks were no-ops, so the checkbox stayed armed
from the first click and the last click wired the checkbox straight to the
textbox, bypassing the script entirely. It also passed on `wires.length>=2`,
satisfied by leftovers from the two pulse tests that ran before it. It now
clicks the dots, in the only order that works (out starts a wire and refuses
to finish one; in finishes and refuses to start), and asserts the two specific
wires.

**`panel-control options` looked for `.prop-row` rows from `registerCard`**,
which was deleted - readmefirst repair #2, the card panel being a client-side
parallel answer to "what is an object's panel". `registerCard` and
`addMemberRow` have no definitions, `cardBodies` is never written, and
`.prop-row` survives only in the stylesheet. It now Options-clicks a real
control inside a Pulse's own panel, which is the same claim - a panel member is
an instance, not chrome - stated against the mechanism that actually exists.

**Three tests asserting "the Textbox shows 3" read `propertyValues`**, which is
a cache of `property-changed` events. `Textbox_OnIn` stores with `SetValueStr`
and re-announces with `SndMsg` out its own `Value`, deliberately - a repeated
write is still an event for a box that triggers something - so no
`property-changed` ever fires and the value only reaches the rendered control.
They now read what the box displays. My own instrumentation had the same fault:
it hooked `property-changed` on the sink, so it was blind to a value delivered
as a message, and reported an empty box that had the right answer in it all
along.

## The pattern under all four

Every one of these is the same shape, and it is not the shape I expected when
the evening started.

The information existed. `DispatchMsg` held the source. The dots were already
clickable and `completeWire` already resolved them. The Textbox already
announced its value. The card panel was already gone. In each case something
had been built to substitute for a fact the system was already carrying, and
**the substitute then hid the fact from the next person to look.** Nobody fixed
the dispatcher because the tap made it unnecessary. Nobody noticed the dots
because the test clicked the icon and the count-based guard said fine.

So the fix was not new capability. `MsgFromNode()` creates no information; it
gives a name to something that was already flowing past, one frame down the
stack. Naming it retired a species of object, a bespoke reaper, and a leak.

The other half of the lesson is about evidence. Two layers of my own
diagnostics were reading a cache instead of the thing, and both agreed with
each other, which is exactly how a wrong conclusion feels right. A count that
passes on other tests' leftovers, a property cache that is silent for values
delivered as messages, an instrumentation hook on the wrong event type - none
of those fail loudly. They just agree.
