# Building an object or a widget

This directory is a **template**, not a widget. It does not build (its
makefile is `Makefile.copy`, so the framework's `objects/*/Makefile` scan
skips it) and it never loads. Copy it to start a real widget; for a plain object with
no panel, copy `objects/udp` instead (see section 4).

A **widget** is an instrument-panel object: a composite View whose controls
(Checkbox, MoButton, LED, Textbox, Dropdown, TextOut, Markdown, …) are laid
out inside it by their X/Y and wired to the widget's own properties. The
object declares; the view renders. Examples that ship: `objects/tcpport`,
`objects/pulsegenerator`, `objects/stopwatch`, `objects/logicgate`.

---

## 0. Two kinds of thing, and why they come in pairs

- An **object** is *function*. It has no controls and no panel: its whole
  interface is the properties it carries and the named nodes you deliver
  messages to. `objects/udp` (a UDP socket), `objects/tcp`, `objects/filter`,
  `objects/queue`. Section 4.
- A **widget** is *presentation*. It is an instrument panel: a composite View
  whose controls are laid out by X/Y and wired to its own properties.
  `objects/pulsegenerator`, `objects/logicgate`, `objects/stopwatch`.
  Sections 1-3.
- **I/O gets both, as a pair.** `objects/udp` + `objects/udpport`, and
  `objects/tcp` + `objects/tcpport`: the panel holds no socket, it creates an
  engine instance and drives it by message. That split is the point - the
  engine knows datagrams and nothing about presentation, the panel knows
  controls and nothing about sockets, either can be replaced without touching
  the other, and anything else (a script, a Pulse, a flow) drives the same
  engine the same way. Section 5.

Which to write: no controls -> an object. An instrument panel over an existing
engine -> a widget. Something with real I/O that people also need to operate by
hand -> both, and keep them apart.

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

## 4. Writing a plain object (no panel)

Copy `objects/udp` - two files, `udp.c` and a `Makefile`, no `widget.h`. Its
interface is the header comment, and that is the whole documentation an object
needs: what you send it, what you set on it, what you subscribe to.

**The interface is nodes, and nothing else:**

| what | how | example |
|---|---|---|
| a verb | a property carrying an `OnMsg` handler - deliver a message to it and it acts | `Send` |
| configuration | an ordinary property, read when it matters | `LocalPort`, `RemoteAddr` |
| state to read | an ordinary property the object writes | `Listening` |
| something happened | `SndMsg(instance, "<node>", msg_send, data)` - whoever cares subscribes | `Received` |

**There is no `In`, no `Out`, and no direction.** A datagram, a chunk, a
reading is a *message*, not a value parked in a property, and the node it goes
out of is just the node the subscriber list lives on. Name nodes for what they
are (`Send`, `Received`, `Clock`, `Enable`), never for which way something
flows. The reply address rides on the message when it needs to: a UDP datagram
arrives carrying its sender's `RemoteAddr`/`RemotePort`, so
`Connect(Udp,"Received",Udp,"Send")` is a complete echo server.

**The rest of the shape:**

- **`Enable`** - `1`/`0`, and *every* handler honors it. If the thing can be
  restarted, make `Enable=1` restart it (the UDP engine reopens its socket);
  if activation is genuinely one-shot, say so in the header comment (TCP).
- **One task for the instance's whole life.** `CreateTask` once, re-arm with
  `AddTaskMilli` **from inside the task's own callback**, `DeleteTask` in
  `InstanceEnd` before freeing `local`. A task created per activation is the
  leak `testharness/leaktest.py` exists to catch.
- **Poll, drain, re-arm.** Take *everything* waiting each tick (a loop until
  `EAGAIN`), not one item - several arrive between ticks.
- **`SndMsg` takes ownership** of the data node; do not `DelNode` it after.
  Forwarding a message you received means sending a fresh copy.
- **Filter only `msg_eof`.** See lesson 12 - `message != msg_send` swallows
  every ordinary property write.
- **Binary-safe payloads**: `SetValueStrLen` plus a `Length` property, so
  embedded NULs survive.
- `ClassStart` publishes the interface with `PublishProp`; `_init`/`_fini`
  register the library node (Company, UUID, Version, Dependencies).

---

## 5. A widget that drives an engine (the pair)

`objects/udpport` over `objects/udp` is the worked example; read it beside
`objects/tcpport`. The panel is an ordinary widget (sections 1-3) plus these
rules, every one of which was a bug first.

**The engine is private state, not a member.**

```c
class  = <registry walk for the class by name>;        /* GetRegObjList -> libs -> classes */
start  = (msgobj) GetPropLong(class, "InstanceStart");
start(class, msg_initialize, NULL);
engine = (NodeObj) GetPropLong(class, "LastInstance");
Connect(engine, "Received", instance, "Callback");     /* your callback, handed over once */
```

Not `CreateObject` (it requires a location, and this thing has none) and not
`Widget_Create` (that makes it a path-registered member sitting on the canvas -
visible, addressable, wireable, and wrong). It lives on your `local` struct,
nothing else knows it exists, so **you** must `DeleteInstance` it in
`InstanceEnd`. Drive it by message: `SetOrDeliverProp(engine,"Enable","1")`,
`DeliverMsg(engine,"Send",msg_send,chunk)`. Read back what it **achieved**
(`Listening`), never what was asked for.

**Setup is armed at BIRTH, and it is the only thing that decides what the panel
says.** In `InstanceStart`: `CreateTask`, then arm a one-shot ~300ms out. Not in
`Activate` - a loaded or imported instance never gets an `Activate` (nothing
runs on a load), so those are exactly the instances that would come up with no
setup at all. What setup does:

1. clear the momentary commands (`Start`/`Stop` back to `"0"`);
2. force the resting presentation **unconditionally** - lamps to stopped,
   readouts dark - with no comparison against what the file said;
3. close the engine for real if it is somehow open, rather than declaring it
   shut;
4. *then* honor the auto option: `if (AutoStart && Enable)` press the command
   (`SetOrDeliverProp(instance,"Start","1")`), so one path does the starting.

**Watch your own state.** A load installs its values *after* your panel is up,
so subscribe to your own state property (`Connect(instance,"On",instance,
"StateWatch")`) and, when a claim disagrees with what the engine is doing,
**re-arm setup** rather than correcting inline - a correction made inside a
fan-out re-enters through the other lamp and crashes the load.

**Guard the arming with a flag** (`local->pending`, cleared when the callback
fires). Arming an already-armed task inserts it twice and corrupts the
scheduler's list - see lesson 14; this is the same rule with the flag spelled
out, because the callback can now be armed from birth, from `Activate`, and from
the state watch.

**Commands are momentary.** After acting, write the command property back to
`"0"`. Leaving `"1"` makes the press part of saved state, and on load it fans
out to the panel's own MoButton, which sees a 0->1 edge and re-emits it down its
wire as a genuine press - the widget starts itself with nobody asking.

**Trace every decision, with the values it read.** These paths fail silently
and invisibly; a one-line trace helper per decision (`UDPPort_Trace`) turns "it
does nothing" into a log that names the value that was wrong. Debug it while
you write it, not after someone reports it.

---

## 6. Lessons learned the hard way (read these)

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

18. **`Activate` is not a lifecycle hook you can rely on.** Dragged instances
    get it; loaded and imported ones do not. Anything that must happen for
    *every* instance however it was born belongs on a task armed from
    `InstanceStart`.

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

## 7. Build & test

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
and the source-enumeration primitive lets gates/comparators combine N inputs.
