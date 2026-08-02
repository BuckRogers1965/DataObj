# Building a widget

This directory is a **template**, not a widget. It does not build (its
makefile is `Makefile.copy`, so the framework's `objects/*/Makefile` scan
skips it) and it never loads. Copy it to start a real widget.

A **widget** is an instrument-panel object: a composite View whose controls
(Checkbox, MoButton, LED, Textbox, Dropdown, TextOut, Markdown, …) are laid
out inside it by their X/Y and wired to the widget's own properties. The
object declares; the view renders. Examples that ship: `objects/tcpport`,
`objects/pulsegenerator`, `objects/stopwatch`, `objects/logicgate`.

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

1. **Add your controls** to the `SkeletonPanel[]` table — one row per
   control: `{ class, property, x, y, w, h, panel }`. Panel `0` is the
   widget's own view; panel `1` is the Help sub-view.
2. **Publish** each property the outside world sees, in `ClassStart`
   (`PublishProp`). Add a `<Name>List` property for every `Dropdown`.
3. **Set them up** in `InstanceStart` (`SetPropStr` for data;
   `<prefix>_Handler(...)` for a property that acts on write).
4. **Write your logic** in the handlers and `_Emit`.
5. **Write the Help** in the widget's own `README.md` — it loads on open.

---

## 2. Anatomy (what each piece does)

- **`Handle_Message`** — required export; the loader dlsym's it to accept the
  module. Leave it.
- **`InstanceStart`** — build one instance: set its properties, register
  handlers, set the view's W/H, then arm the **deferred build task**.
- **`Skeleton_BuildTask` / `Skeleton_BuildPanel`** — one tick later, create
  the controls and the Help sub-view, wire Help-open, and run `Activate`.
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

## 3. Wiring: everything is a property

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

## 4. Lessons learned the hard way (read these)

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
   `In`/`Out` are just properties that happen to be named that. A property
   changes and whatever subscribed to it is told — that is the whole rule, the
   same for every property whatever it is called.

3. **Set the view's W/H in `InstanceStart`, before any client subscribes.** A
   size set later (in the deferred build) *shadows* the W/H node the client's
   tap is already on and never reaches it — the panel stays its default size.

4. **Build deferred, one tick after creation.** In `InstanceStart` the
   instance has no path yet, so controls can't be addressed. Arm a task at
   +1ms; by then the bridge has placed the instance.

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
    stand for its palette icon, running the same deferred build and
    `Activate` any placed instance gets. Anything your `Activate` does
    unconditionally - checking a status, dialing out, generating - runs on
    that palette seed too, at every boot, before anyone has dragged one out
    or set it up. `Activate` should build the panel and set resting state,
    nothing else; any real action belongs behind an explicit trigger (a
    button, a wired `In`), gated on `Enable` like everything else.

Two things the core now handles for you (don't re-solve them):
- **Two-way bindings are safe.** An unchanged data-property write no longer
  re-fans-out, so a control that both edits and reflects a property can't loop.
- **Large property values transmit.** The bridge sizes its event buffer to the
  value, so a multi-KB README reaches the client intact.

---

## 5. Build & test

```
make -C objects/<name>          # build just yours
make                            # or build everything
./framework.sh                  # run; drag <Name> from the palette
```

Drive it headless over the raw protocol like the suites in `testharness/`
(create-instance, set-property, subscribe). Open the Help panel and confirm
your `README.md` renders.

The long game (see `ROADMAP.md`): this per-widget boilerplate becomes a
**Widget base object** so a new widget declares only its controls and logic,
and the source-enumeration primitive lets gates/comparators combine N inputs.
