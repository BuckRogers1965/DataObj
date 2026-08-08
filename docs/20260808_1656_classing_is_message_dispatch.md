# Classing Is Message Dispatch

A crash sent me looking for where a message goes when it arrives, and the
answer turned out to be: nowhere, mostly. There is one door, it belongs to the
value of a property, and I spent a day trying to push everything else through
it.

## What broke

An instance destroyed by a route the bridge never sees - a script's
`destroy()`, a widget tearing down its own children - left the bridge holding
tap records with dangling pointers. Later the allocator handed a recycled
property-node address to something new, the bridge matched a stale record
against it, and dereferenced a freed instance.

The diagnosis was solid, from a core dump. The fix was not.

## What I did wrong

I added a message id - "the connection ended" - and announced it down every
wire before the frees, so subscribers could let go. Which sounds right, and
then:

- It reached `ActivateOnMsg`, which activates on any id except `msg_eof`. A
  wire coming up ran the sink's Activate. At boot that is every widget building
  its panel, cascading, and the framework never finished starting.
- I made the message payload-free so it would be harmless. Now handlers that
  read the payload without checking the id read *nothing* as *empty*:
  `TPLink_OnEnable` does `local->enabled = GetValueInt(data) ? 1 : 0`, so a
  wire coming up **disabled the object and tore it down**. The panel came up
  unchecked and refused every button.
- I guarded eleven widget handlers. Then a proper scan found **81 handlers
  across 40 files** that read the payload without requiring one.
- So I moved the guard into the core: drop any delivery with no payload. One
  place, protects all 81 - and now the message reaches nobody, including the
  single subscriber the whole exercise existed to notify.

Four attempts, each wider than the last: two ids, eleven handlers, eighty-one
handlers, the core's delivery path. A fix that keeps growing is not converging.
It is a missing distinction being routed around, and the right move was to stop
at the second attempt and go find it.

The missing distinction: **a value handler is a leaf.** It receives one value
for one property. It has no business knowing any message id, and I was trying
to make it the discriminator for the entire id space.

## The id space is supposed to be large

`USER_MESSAGE_BASE 100` exists so every object declares its own verbs and vars
as an enum from there. One object alone has send/start/stop plus three variable
ids. Forty-six objects doing that is hundreds of ids, and any of them can
arrive at any instance.

That only works if the object that *declared* the ids is the only thing that
has to know them. Which means one door per instance, a switch behind it, and a
way to say "not mine" for everything else. Making 81 leaf handlers each
correctly ignore every id every other object ever invents is not a design; it
is a liability shared by everyone.

## The ladder

Dispatch is a chain, and classing is what the chain walks:

```
value  ->  property  ->  instance  ->  class  ->  superclass  ->  Object
```

At each level, three answers:

- **handled** - stop; nothing above needs to see it.
- **punt** (`rtrn_dropped`) - not my problem, go up.
- **propagate** - I looked, and it continues to the subscribers (what a probe
  does: watches without consuming).

Object is the end. Its default never punts; a message that reaches it is over.

That is what a class *is* here: not a bag of fields, a **place a message lands
when the level below it didn't claim it**. Subclassing is a parent link and a
punt, which is why the return codes already read the way they do -
`rtrn_dropped` has always meant "pass it up", even though `callback.h` still
describes it as "do not propagate".

The cost of a new message type on this shape is zero. It reaches the levels
that name it and is punted by everything else. On the shape I built, a new id
was a hazard to all 81 handlers.

## What must not break: the widgets only do value updates

Right now, essentially every widget handler in the tree is a value handler.
`Textbox_OnIn`, `Slider_OnIn`, `Checkbox_OnIn`, `LED_OnIn` - each takes what
arrived and displays it. That is correct and it must keep working exactly as it
does, untouched.

Note what they disagree about, though, because it shows the door is overloaded:

- `Out_OnEnable`, `TCPPort_OnEnable`: `message != msg_send` -> drop
- `TPLink_OnEnable`: anything but `msg_eof` acts
- `Textbox_OnIn`: no filter at all, deliberately - "a write is a write"

Three conventions for one question, none of them wrong on its own terms,
because the question they are all trying to answer - *is this data?* - has no
answer available to them. `msg_change` and `msg_send` name how a message
**arrived** (a property's synchronous fan-out versus a queued send), not what
it **is**.

The property level itself is already right: a handler if one is attached,
otherwise set the value. That code works. Nothing above it exists.

## Subscription is a different level, and it has two ends

A connection is not a value, so it does not belong anywhere near the value
handler. It belongs to the subscription.

Today a property's subscribers are records sitting directly among its
properties, mixed in with `OnMsg`, `W`, `H`, `graphics`. So there is no handle
for "the connections on this property": every walker sifts the property list by
name, and there is nothing to attach a handler to.

Give the property a subscription node and both problems go at once. The
connections become that node's children - siblings of each other, one child
chain, no sifting. The node itself is a handle, and the handler you hang on it
is the **connection-level** handler. A connection event goes *there*, a
different slot entirely, so it can never reach a value handler by
construction.

The web bridge already invented this for itself: a `Taps` node whose children
are records, with a handler on it. It works - but it is a second copy of the
subscription list, kept in step with the real one by hand. That hand-syncing
*is* the crash. Find-tap, free-taps, drop-all-taps, the stale target pointer,
the recycled-address match: every line of it exists because the bridge could
not attach to the real list. When the subscription node exists, the bridge's
tap *is* the subscription. Nothing to keep in sync, nothing to dangle.

And connections have two ends, which is the part that has been missing. Today
only the source property records anything, so deleting the *sink* leaves the
source pointing at a dead instance with nothing on the sink to have told it.
That is exactly why `DeleteInstance` runs two registry-wide walks - one for
subscriptions, one for links - each descending library, class, instance and
recursing every property, to find wires by search. With an entry at both ends,
the wires are simply known, and both walks disappear.

## Dispatching connection events properly

With two ends, "the connection ended" splits by **who initiated it**:

- **The property is going away** - it sends break-connection down its own list,
  and each far end just *removes the entry*. No event: the far end caused this,
  and telling it what it already did is noise.
- **A third party drops the wire** - a hand unwiring on the canvas, a script
  calling disconnect. Neither end asked, so both are told: an end-of-connection
  event on teardown, a start-of-connection event when one is made.

So the event is not "a connection changed". It is "a connection changed **and
it was not your doing**" - narrower, and actually useful. The version I built
fired on every wire in the system at boot, which is the same information as
silence.

## Order of work

1. A subscription node on the property; connection records become its children.
2. The connection-level handler slot on that node.
3. The entry exists at both ends.
4. Break-connection on teardown; events only for third-party changes.
5. The punts above the property: instance -> class -> superclass -> Object,
   `rtrn_dropped` meaning "up", Object terminal. Two pointers already exist and
   are never called - the per-library message entry, dlsym'd and discarded by
   the loader, and the class-level default, commented out in the object layer.
6. Then the bridge's tap becomes a subscription, and the crash has nowhere left
   to live.

The value handlers are not in that list. That is the point.
