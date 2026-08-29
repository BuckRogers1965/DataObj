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

**`InitPosition(instance)`** is the line that makes it a Control rather than a
plain object: X/Y/W/H, Container, Name, Deletable. And `PublishPosition(class)`
in `ClassStart` publishes them.

**`PublishProp`** declares your interface, one line per property, with the widget
type each presents as (`PROP_TEXTBOX`, `PROP_LED`, `PROP_CHECKBOX`,
`PROP_BUTTON`, `PROP_MENU`, ...). That published interface is what a panel's
dissection table and a client's rendering both read.

---

## The browser half - a control brings its own presentation

**A control ships the code that draws it.** It lives in `show/web/<name>.js`
as an ordinary editable file; `show.mk` turns it into a string literal
(`show_web.h`) at build time, and `ClassStart` hands it over:

```c
PublishShow(ClassSelf, PROP_TEXTBOX, show_web_js, show_web_css);
```

The client asks the engine what arrived and calls what the class published.
**There is no list of control names in the browser to add yourself to**, and
writing a new control needs no client change at all.

The file registers by class name and returns one element:

```js
register('Skeleton', {
  create(ctx) {
    const el = document.createElement('input');
    el.value = (ctx && ctx.defaultValue) || '';
    if (ctx && ctx.commit) el.onchange = () => ctx.commit(el.value);
    return el;
  },
});
```

Two obligations, and only two:

- **Define `.value`.** It is the ONE accessor the host reads and writes.
  Whatever your element's natural property is, `.value` hides it — the
  Checkbox defines it over `.checked` ('1' is ticked), the TextOut over
  `textContent`. An `<input>` already has one, so it defines nothing.
- **Call `ctx.commit(v)` when a PERSON changes it.** That is the gesture
  going back to the engine. A read-only control simply omits it, and then it
  can only ever be written *to*.

`ctx.defaultValue` is the starting value the class published.

Edit `show/web/<name>.js`. **Never edit `show_web.h`** — it is generated, and
your changes there are gone at the next build. The Makefile's
`include ../show.mk` must come *after* the `all:` target, or the default goal
stops being `all`.

There is no styling obligation: a look shared by several controls belongs in
the host stylesheet, and only a look that is this control's alone belongs in
`show/web/<name>.css`.

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
  interface, and the browser half published from `show_web.h`.
- `show/web/skeleton.js` - its presentation. Edit this; `show_web.h` is
  generated from it by `show.mk` and must never be edited or committed.
- `Makefile.copy` - becomes the new module's `Makefile`. It already has the
  `include ../show.mk` line, after `all:` where it belongs.

The shipped controls are the best reference: `objects/textbox` (input),
`objects/led` (display), `objects/mobutton` (momentary), `objects/dropdown`
(a list). See the [top-level README](../README.md) for what every module
declares.
