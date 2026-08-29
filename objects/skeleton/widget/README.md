# Writing a widget

**Copy this when you are writing a bag of controls with behaviours** - an
instrument panel. A widget descends from `Widget`, which descends from `Control`:
it has a name, a place and a size like any control, and it additionally CONTAINS
controls and drives them.

For the other two kinds see [`../object/`](../object/README.md) (plain function,
no panel) and [`../control/`](../control/README.md) (one thing on screen). The
[top-level README](../README.md) covers what every module declares, how
dependency ordering works, and how to reach another module's code.

A widget is a composite View whose controls (Checkbox, MoButton, LED, Textbox,
Dropdown, TextOut, Markdown, ...) are laid out inside it by their X/Y and wired
to the widget's own properties. The object declares; the view renders. Shipped
examples: `objects/tcpport`, `objects/pulsegenerator`, `objects/stopwatch`,
`objects/logicgate`.

---


## 0. What a widget must declare

Three lines beyond an ordinary module, and the third is the one people get wrong.

```c
/* ClassStart, right after RegisterClass */
SetClassVersion(ClassSelf, "1", "0");
SetClassParent(ClassSelf, "Widget");

/* _init - Widget, plus EVERY control class your layout table names */
AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");
AddDependency(temp, "widget.object", "Widget", "1", "0");
AddDependency(temp, "view.object", "View", "1", "0");
AddDependency(temp, "checkbox.object", "Checkbox", "1", "0");
AddDependency(temp, "led.object", "LED", "1", "0");
AddDependency(temp, "mobutton.object", "MoButton", "1", "0");
AddDependency(temp, "textbox.object", "Textbox", "1", "0");
```

The dependency list IS your layout table, read back. Every `{ "Textbox", ... }`
row is a class you cannot build without, so it gets a line - plus anything you
create in code (an engine of your own: `AddDependency(temp, "udp.object",
"UDP", "1", "0")`). `Help` is a table keyword, not a class; it needs no line.

Under-declare and your widget starts before a control class exists and builds
without it - a panel with a hole in it. Over-declare and your widget refuses to
start when something it never needed is absent. Both are silent except in the
log, which names exactly what was missing.

---


## 1. Stamp out a new one

```
objects/skeleton/newwidget.sh Counter
make -C objects/counter
```

`newwidget.sh Counter` creates `objects/counter/` with `counter.c`, a real
`Makefile`, and a starter `README.md`, rewriting every `Skeleton`/`skeleton`
token and minting a fresh UUID. Restart the framework and drag **Counter**
from the palette.

Do the rest by editing `counter.c`:

1. **Add your controls** to the `SkeletonPanel[]` table — one entry per
   control: `{ cls, prop, def, panel, x, y, w, h, label, handler }`. The
   handler is last so a plain control just omits it. Panel `0` is the
   widget's own view; panel `1` is the Help sub-view. Lay it out by the
   rules in section 2 below.
2. **Publish** each property the outside world sees, in `ClassStart`
   (`PublishProp`). Add a `<Name>List` property for every `Dropdown`.
3. **Set them up** in `InstanceStart` (`SetPropStr` for data;
   `<prefix>_Handler(...)` for a property that acts on write).
4. **Write your logic** in the handlers and `_Emit`.
5. **Write the Help** in the widget's own `README.md` — it loads on open.

---

## 2. Laying out the panel

**A panel is pixel layout.** Every control carries an `x`, `y`, `w` and `h`
in pixels and sits exactly there. There are no rows, no columns, no grid and
no flow — nothing reflows, nothing wraps, nothing is relative to anything
else. Two controls sharing an `x` are not "a column"; they are two controls
that happen to have the same `x`.

**What renders is the control plus its label.** `w`/`h` describe the control
alone, so the space an entry actually occupies is bigger than `w`/`h` on
whichever side its label is. That is the single most common way a layout goes
wrong: extents computed from `x`/`y`/`w`/`h` alone under-measure every
labelled control. The label's text is the property name — `widget.c` sets
`Label` from `prop`, so `{ "TextOut", "Envelopes", ... }` reads "Envelopes"
on screen.

### Where the label goes

Never `LABEL_NONE`. An unlabelled control cannot say what it is, and one
whose value is empty paints nothing at all — a `TextOut` with no label and
no text is an invisible control in a panel that looks broken.

- **`LABEL_LEFT` is the default.** The label reads before the control on one
  line. It costs nothing vertically, so it can never collide with whatever
  sits below. Use it unless you have a specific reason not to.
- **`LABEL_RIGHT`** for a small mark whose caption reads after it — a
  Checkbox or an LED sitting at a left-hand `x`.
- **`LABEL_BOTTOM`** for a caption under the control: several LEDs side by
  side, or stacked boxes where the caption belongs beneath each one.
- **`LABEL_TOP`** costs vertical space and pushes its control down into
  whatever follows it. Only use it where you have deliberately left the room.

### Where Enable goes

Enable never sits at the panel's edge. **Find the control with the largest
`x + w` — the right-most one — and end Enable at that right edge.** Enable is
a Checkbox with a left label, so its own footprint is its label plus the
box (about 55px for the word "Enable" and a 9px box); place its `x` at
`right_edge - 55` and it lines up with the content instead of floating out
in the margin.

### How big the panel is

Place every control first, then size the main view: **the content extent plus
50 pixels in each direction.** Nothing then sits against a rim. Padding only
ever adds — a panel that already exists is never made smaller, and a panel is
never resized to fit its content at runtime (see the size rules in
`CLAUDE.md`: a widget declares its size and that size is obeyed).

---

## 3. Anatomy (what each piece does)

- **`Handle_Message`** — required export; the loader dlsym's it to accept the
  module. Leave it.
- **`InstanceStart` — this IS the init message, and it is the whole
  beginning of a widget's life.** `CreateObject` calls it as
  `InstanceStart(class, msg_initialize, place)` (`object.c`), handing it the
  place and the name it was born with, so it sets its properties, registers
  its handlers, sizes its view and BUILDS ITS PANEL right there. There is no
  deferred build, no build task, no `PanelBuilt` flag and no second creation
  path for a clone - a widget is finished when `InstanceStart` returns.
- **There is no `Activate`.** A widget does not start; it answers what
  arrives. Anything it must do at birth - arming a sampling task, setting
  resting state - happens in `InstanceStart`, because that call is the init
  message. (The engine still has an `activate` verb, but that is a PERSON
  pressing a button on a card, not a lifecycle hook - see lesson 18.)
- **`Skeleton_Ctl`** — create one control, register its path, and wire it to
  a widget property by control kind (command / readout / edit / menu).
- **handlers (`_OnIn`, `_OnTrigger`, `_OnEnable`)** — a property with an
  `OnMsg` handler; a write to it runs the handler.
- **`Skeleton_OnHelpOpen`** — on Help-panel open, read `README.md` and set it
  into the Help box.
- **`ClassStart`** — publish the interface (what the palette/clients see).
- **`_init` / `_fini`** — register/unregister the library node (provenance:
  Company, UUID, Version).

---

## 4. Wiring: everything is a property

Everything is a property. A control drives, or reflects, one of the
widget's ordinary properties:

| control kind | wiring in `Skeleton_Ctl` |
|---|---|
| `MoButton` (command) | `Connect(control,"Out", widget, prop)` — a press writes `prop` |
| `LED`/`TextOut`/`Label` (readout) | `Skeleton_Reflect(widget, prop, control, "Value")` |
| `Dropdown` (menu) | `Connect(control,"Value",widget,prop)` + reflect `prop`List into `Items` |
| `Checkbox`/`Textbox` (edit) | `Connect(control,"Value",widget,prop)` + reflect back |
| `Markdown` (help) | nothing at build — loaded on open |

`Skeleton_Reflect` both `Connect`s *and* seeds the control with the property's
current value, so the GUI shows it immediately.

A property that should **act** when written carries an `OnMsg` handler
(`Skeleton_Handler`). `In`, `Trigger`, `Enable` here are just properties named
that — writing to them runs code. Downstream flows read/write these same
properties by name.

---

## 5. Writing an object (no panel)

Moved: see [`../object/README.md`](../object/README.md). Short version - the
header is the whole interface, the state struct lives in the `.c`, nothing is
published, and the driver hands over `{Owner, MsgBase, Port}` at creation and
catches answers as messages. `objects/udp` + `udp.h` is the worked example.

---


## 6. A widget that drives an object (the pair)

`objects/udpport` over `objects/udp`, and `objects/tcpport` over
`objects/network` - read those two.

**The widget includes the object's header and speaks only its ids.**

```c
#include "objects/udp/udp.h"
#define UDP_CALLBACK 0x5001        /* THIS panel's id for THIS object */
```

**The object it drives is private state on the widget's own struct.** Not a
member, not a child, not something with a name - and there is no such thing as
an "Inner". Make it through the class's own `InstanceStart`, which is the
framework's equivalent of `New(GetNamedClass("UDP"), UDP_CALLBACK, pDev)`:

```c
/* walk the registry for the class (GetRegObjList -> libraries -> classes) */
args = NewNode(INTEGER);
SetPropLong(args, "Owner",   (long) instance);   /* this panel */
SetPropLong(args, "MsgBase", UDP_CALLBACK);      /* this panel's id for it */
SetPropStr (args, "Callback", "Callback");       /* the property replies land on */
instanceStart(cls, msg_initialize, args);
DelNode(args);
local->udpInstance = (NodeObj) GetPropLong(cls, "LastInstance");
```

**Not `CreateObject`** (it requires a location, and this has none) and **not
`Widget_Create`** (that makes it a named, path-registered member sitting on the
canvas - visible, addressable, wireable, and wrong). It has no path, so nothing
in the session can reach it; the only reference is the one on your struct, so
**you** destroy it in `InstanceEnd`.

**The widget keeps its own state.** It has the boxes and the lamps, so it knows
what it asked for; it never reads state back out of the object, because there
is nothing to read. `pData->state` in the reference is exactly this. When a
start fails, the object says so on the callback and the widget corrects itself.

**One callback handler**, switching on `base + ordinal`:

```c
switch (message - TCPPORT_CALLBACK) {
case TCP_NEW_CONNECTION_CALLBACK:  ...
case TCP_RECEIVED_DATA_CALLBACK:   ...
case TCP_ERROR_CALLBACK:           ...
}
```

**Setup is armed at BIRTH**, in `InstanceStart`, on a one-shot task guarded by
a flag - never off the `activate` gesture, which a loaded or imported instance
never gets. Setup clears the momentary commands, forces the resting presentation
UNCONDITIONALLY (whatever the file said), stops the object for real, and only
then honours the auto option by pressing the command a person would press.

**Commands are momentary**: write the command property back to `"0"` after
acting, or the press becomes saved state and a load re-presses it.

---

## 7. Lessons learned the hard way (read these)

Every one of these cost real debugging time. The skeleton already does them
right — don't undo them.

1. **Never name a data property with a reserved VIEW name.** A widget renders
   as a View, and the View owns these: **`ReservedViewOpen`**,
   `ReservedViewPanelX`, `ReservedViewPanelY`, `ReservedViewResizeable`. They
   were once `Open`/… and a widget with a property of the same name collided
   with the view's own, which is why they are namespaced. Do not use the
   `ReservedView*` names, and know that the view's open state is
   `ReservedViewOpen` (that's what you hook for Help-on-open). Freely usable:
   `X`, `Y`, `W`, `H`, `Container`, `Name`, `State`.

2. **There are no ports and no directions.** Everything is a property, and
   `In`/`Out` are nothing but the NAMES somebody gave two controls. They are
   not a species, not a direction and not a kind of thing you can look up -
   a property named `Out` behaves exactly like one named `Colour`. A property
   changes and whatever subscribed to it is told; that is the whole rule, the
   same for every property whatever it is called.

   **`ReservedIn` / `ReservedOut` are how a wire dropped on the ICON knows
   where to land.** When an end of a `Connect` names no property, the engine
   fills it in (`Connect`, `object.c`): the source end falls back to this
   instance's `ReservedOut`, the sink end to its `ReservedIn`, and if neither
   is set, to `Value` - the one property a plain control speaks through. So
   a widget whose real traffic is on `Input` and `Output` says

   ```c
   SetPropStr(instance, "ReservedIn",  "Input");
   SetPropStr(instance, "ReservedOut", "Output");
   ```

   in `InstanceStart`, and from then on somebody can wire straight to the
   widget's icon without opening the panel to find the right box. They are an
   OVERRIDE and nothing else: they do not create a property, do not make one
   special, and do not make it a port. Most things carry neither. The rule
   lives in `Connect`, so a GUI dot, a script and a REST call all get the same
   answer, and the browser draws its stand-in dot in a different colour to say
   "this is where an unnamed wire would go", not "here is a wire".

3. **Set the view's W/H in `InstanceStart`, before any client subscribes.** A
   size set afterwards *shadows* the W/H node the client's tap is already on
   and never reaches it — the panel stays its default size.

4. **Build the panel in `InstanceStart`, in place.** The constructor is
   handed its place and its name, so the instance is addressable and its
   controls can be created right there. This used to be deferred a tick
   because the instance had no path yet; it is not any more, and the flag,
   the task and the clone's separate creation path went with it. A widget is
   finished when `InstanceStart` returns.

5. **Reflect-and-seed.** A plain `Connect` only fires on the *next* change, so
   a fresh readout reads blank. `Skeleton_Reflect` hands the control the
   current value at creation.

6. **A readout displays its `Value` — set that, not a fake `"In"`.** "Just set
   the text into the property with an update" (`SetPropStr`); the write fans
   out to whoever's watching.

7. **Help loads from `README.md` on open — no hardcoded help.** Hook the Help
   sub-view's `ReservedViewOpen`; on open, resolve the box **by path**
   (`ResolvePath`, same node the client subscribes to) and set its `Value`.
   Do **not** persist the open state.

8. **The object sets sizes, not CSS.** The Help panel and box sizes come from
   the shared `HELP_W`/`HELP_H`/`HELP_W_OFF`/`HELP_H_OFF` defines
   (`object.h`). Every control sizes by the pixel `W`/`H` its instance
   carries — a `Textbox` no differently from `Markdown`/`HTML`. Content scrolls inside a fixed size
   its content.

9. **Updating a property's own mirrored value from inside its handler** —
   use `SetValueStr(GetPropNode(inst,name), …)`, as `_OnEnable` does, so the
   write does not fan back out to whoever just wrote it.

10. **Stop your tasks in `InstanceEnd`.** A still-scheduled task fires later
    with a freed instance pointer. `RemoveTask`/`DeleteTask` before `free`.

11. **Copy a working widget.** This skeleton is that copy. When something
    misbehaves, diff against `pulsegenerator.c` — they share this structure on
    purpose.

12. **A property-driven control's write is `msg_change`, not `msg_send`.**
    `Enable` here has an `OnMsg` handler, but the panel's own Enable
    *checkbox* is a separate control instance — clicking it changes the
    checkbox's own `Value`, which fans out to its subscribers (this widget)
    as an ordinary property change, `msg_change`. `msg_send` only happens
    when something already carrying an `OnMsg` handler is written to
    directly (a raw `set-property` from outside, or another object's
    `Connect`'d property). A handler that filters `message != msg_send` silently
    swallows every click the real on-screen checkbox sends — it looked
    enabled, took the click, and never turned anything off. Filter only
    `message == msg_eof`, like every handler here now does.

13. **Never delete a contained instance from inside a callback IT triggered
    synchronously.** Property writes fan out inline, not queued (unlike
    `SndMsg`, which queues through the scheduler and never nests inside the
    sender's own call stack). If you contain another instance (a TCP engine,
    say) and `Connect` one of its plain properties (e.g. `Connected`) to a
    handler here, that handler runs *inside* the inner instance's own
    currently-executing scheduled task the moment the property changes.
    Calling `DeleteInstance` on it right there frees the task/instance data
    that task is still using and corrupts the scheduler's shared task list -
    every object on the whole session starves, not just this one. Only ever
    delete a contained instance from a top-level trigger that isn't nested
    inside its callback chain: the start of a fresh operation, Enable
    dropping to 0, or a dedicated timeout task. A message-driven callback
    (arriving via `SndMsg`, e.g. `Out` data) is safe to delete from; a
    plain-property callback (`Connect`'d to something like `Connected`) is
    not.

14. **Never re-arm a reused `TaskObj` from outside its own callback.** A
    recurring task is safe to re-arm with `AddTaskMilli` *from inside its own
    firing callback* - the scheduler has already unlinked it by the time
    that callback runs. Calling `AddTaskMilli` on the same `TaskObj` from
    anywhere else (a button handler, say) while it might still be linked
    from an earlier arm does not reposition it - it corrupts the doubly-
    linked task list. If you need a one-shot guard (a timeout) armed from
    multiple call sites, either `DeleteTask` any previous one before
    `CreateTask`-ing a fresh `TaskObj` every time, or don't reuse the task at
    all.

15. **Every loaded class gets a real instance at boot - the palette icon
    itself.** `BuildPalette` creates one live instance of every class to
    stand for its palette icon, and it is born exactly the way a dragged one
    is: `InstanceStart`, panel and all. So anything your `InstanceStart` does
    unconditionally - checking a status, dialing out, generating - runs on
    that palette seed too, at every boot, before anyone has dragged one out
    or set it up. Build the panel and set resting state there, nothing else;
    any real action belongs behind an explicit trigger (a button, a wired
    `In`), gated on `Enable` like everything else.

16. **Nothing runs on a load - the values are simply installed.** The loader
    writes saved properties with `SetPropStr`, never `SetOrDeliverProp`, so no
    handler runs. But the write still *fans out*, and a control wired both ways
    turns that fan-out into a real gesture. So never trust a restored value that
    describes what the process is *doing*: runtime state (running/idle lamps,
    "ready" flags, a received payload) is not saved state, however it got into
    the file. Force it at setup instead.

17. **A widget saved while operating must come up stopped.** Whatever the file
    says, the process just started: nothing is open, so the panel says nothing
    is open. The auto option is the *only* way it comes back live, and that goes
    through the same command press a person would make. A panel claiming a
    socket it does not have is worse than a dead one.

18. **`activate` is a gesture, not a lifecycle hook.** It is a person
    pressing a button, reaching the instance the same way any other command
    does - so it fires for some instances and never for others, and nothing
    that must happen for *every* instance however it was born may hang off
    it. That belongs in `InstanceStart`, which every instance gets.

19. **Test the combinations, running.** Save-while-running -> load, and
    export-while-running -> import, with the auto option both off and on: four
    cases, and they are not the same code path. Check the truth outside the GUI
    (`ss -uanp` for the port, a plain socket for the traffic), not just what the
    panel claims.

Two things the core now handles for you (don't re-solve them):
- **Two-way bindings are safe.** An unchanged data-property write no longer
  re-fans-out, so a control that both edits and reflects a property can't loop.
- **Large property values transmit.** The bridge sizes its event buffer to the
  value, so a multi-KB README reaches the client intact.

---

## 8. Build & test

```
make -C objects/<name>          # build just yours
make                            # or build everything
./framework.sh                  # run; drag <Name> from the palette
```

Drive it headless over the raw protocol like the suites in `testharness/`
(create-instance, set-property, subscribe) - and drive it the way the browser
does, by writing each **control's** own `Value`, not the widget's property: they
are different paths and only one of them is what a user's click does. Open the
Help panel and confirm your `README.md` renders.

For anything with real I/O, prove it against something outside the framework (a
plain socket, `ss -uanp`, `nc`), and run the four save/load/export/import cases
from lesson 19 before calling it done.

The long game (see `ROADMAP.md`): this per-widget boilerplate becomes a
**Widget base object** so a new widget declares only its controls and logic,
and the source-enumeration primitive lets gates/comparators combine N sources.
