# Writing a control

**Copy this when you are writing one thing on screen.** A box, a light, a knob, a
button, a readout. A control is the smallest presented thing, and it is what
panels are built from.

It descends from `Control`: a name, a place, a size, and it is serialized.

---

## Why this pattern

A control holds **one** thing. That is the whole discipline, and it is what keeps
panels composable: a Widget lays controls out by their X/Y and wires them to its
own properties, so every control has to be independently placeable and
independently wireable. If your thing holds three values, that is three controls,
or it is a Widget.

Two consequences worth internalising:

**Its size is declared, and obeyed.** `W`/`H` are properties like anything else.
Content never resizes a control - long text scrolls inside it. There is no
content-driven sizing anywhere, and adding some breaks every panel that placed
you by hand.

**It renders as an atom with no client change.** The engine tells the client what
kind of thing arrived - the `classParent` off your class node - and anything
descending from `Control` draws as a control plus a label. This used to be a
hardcoded list of fifteen class names in `web/app.js`, and a control missing from
it drew as a panel. Write your control, declare its parent, and it looks right
the first time.

---

## The shape

**One value.** A property carrying an `OnMsg` handler, so a write to it runs your
code:

```c
SetPropStr(instance, "Value", "");
port = GetPropNode(instance, "Value");
SetPropLong(port, "OnMsg", (long)Skeleton_OnValue);
```

**`SetValueStr`, not `SetPropStr`, when a repeat must count.** `SetPropStr`
applies a change test and drops a write that matches what is already there, so
the same text entered twice reaches nothing subscribed. For a control that
*triggers* something - a string to send, a command to run - the second one has to
get through. Write the value, then announce it.

**Do not filter on message type in a value handler.** A plain property's fan-out
arrives as `msg_change`, a port's as `msg_send`. A write is a write. Filtering
for one of them is how a control ends up ignoring the GUI (or ignoring a wire),
which is a bug that takes an afternoon to find.

**`Enable` gates everything.** It is an ordinary property, so anything can drive
it through `Connect()` - a Pulse, a script, another control. Check it in *every*
handler, not just the obvious one. `msg_eof` on an enable line means nothing.

**`ReservedIn`/`ReservedOut`** name what a bare wire to you should hit in each
direction, so a client can wire your control without knowing your property names.

**`InitPosition(instance)`** is the line that makes it a Control rather than a
plain object: X/Y/W/H, Container, Name, Deletable. And `PublishPosition(class)`
in `ClassStart` publishes them.

**`PublishProp`** declares your interface, one line per property, with the widget
type each presents as (`PROP_TEXTBOX`, `PROP_LED`, `PROP_CHECKBOX`,
`PROP_BUTTON`, `PROP_MENU`, ...). That published interface is what a panel's
dissection table and a client's rendering both read.

---

## Reserved names

Never name a data property **`Mode`**. A View uses it to pin interaction mode,
and the collision produces a dead panel with no JavaScript error - which is a
miserable afternoon. Check the [reserved names](../README.md) before inventing a
property name that sounds generic.

---

## Where the code lives now

`InitPosition` and `PublishPosition` come from `control.object`, not the core -
the core has no business knowing that anything is ever presented. That is why
this skeleton includes `control.h`. If you get `undefined symbol:
PublishPosition` at load time (not at build time - see the top-level README on
`ld -shared`), that include is missing.

---

## Subclassing

Set `Parent` to `Control` and declare it:

```c
SetClassParent(ClassSelf, "Control");                        /* in ClassStart */
AddDependency(temp, "control.object", "Control", "1", "0");   /* in _init */
```

If you are extending an existing control rather than writing a new kind, name
*that* class as your parent instead, declare its file, and answer only what you
add - returning `rtrn_dropped` sends the rest up to it.

---

## Files here

- `skeleton.c` - the module. One value, an Enable, a position, a published
  interface.

The shipped controls are the best reference: `objects/textbox` (input),
`objects/led` (display), `objects/mobutton` (momentary), `objects/dropdown`
(a list). See the [top-level README](../README.md) for what every module
declares.
