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

## 4. Writing an object (no panel)

Copy `objects/udp` - `udp.h`, `udp.c`, a Makefile. `objects/network/tcp.c`
is the bigger example (connections, TLS, client and server).

**The interface is a HEADER, and the header is the whole of it.** Write it
FIRST, before any behaviour exists, because it is the thing that must not
change:

```c
#define MAX_MSG_SIZE 65535          /* limits */

enum {                              /* verbs, then vars - one id space */
    UDP_SEND_PACKET_MSG=USER_MESSAGE_BASE,
    UDP_START_MSG,
    UDP_STOP_MSG,

    UDP_REMOTE_HOST_VAR,
    UDP_REMOTE_PORT_VAR,
    UDP_LISTEN_PORT_VAR
};

#define UDPSendPacket(pUDP,size,message) ((void)(size), DeliverMsg(pUDP, "Msg", UDP_SEND_PACKET_MSG, (message)))
#define UDPStart(pUDP) DeliverMsg(pUDP, "Msg", UDP_START_MSG, 0L)
#define UDPStop(pUDP)  DeliverMsg(pUDP, "Msg", UDP_STOP_MSG, 0L)
```

`USER_MESSAGE_BASE` (callback.h) is where an object's own ids start, clear of
the framework's. A driver includes this header and can then say exactly these
things and nothing else.

**No struct in the header.** The instance data is defined in the `.c` only, so
a driver holding the node has a `long` it cannot cast: no layout, no size, no
fields. That is what lets the implementation change to anything at will.

**One entry, one message function.** The instance carries a single `"Msg"`
property whose `OnMsg` is the object's message function, and it switches on the
ids - the reference's `ObjectMessageFunc`:

```c
switch (message) {
case UDP_START_MSG:       return Udp_Start(instance, local);
case UDP_SEND_PACKET_MSG: return Udp_SendPacket(instance, local, data);
case UDP_REMOTE_HOST_VAR: ...      /* a var id is a message too */
}
```

**A var is set and read with the same id**: a data node carrying a value SETS
it, an empty node is FILLED IN with the current value. That is the reference's
SETVARIABLE/GETVARIABLE pair, and it needs no second call.

**State lives in the object's own struct, never in properties.** No
`LocalPort` property, no `Connected` readback, no `Enable`, no `Activate`
hook - `Start` and `Stop` are verbs in the header, and a second entrance to a
verb is just a way to get them out of step. `PublishProp` is called **zero**
times: the palette and the clients see nothing, because there is nothing they
may touch. An object's whole property list is `Msg`, plus the framework's own
`State` and `local`.

**Replies go where the creator said**, handed over at creation:
`InstanceStart(class, msg_initialize, data)` reads `Owner` (the creator's
node), `MsgBase` (the id the creator chose) and `Callback` (the name of the
creator's own port), and every callback is delivered as `MsgBase + ordinal`.
The object names no callback of its own. The base belongs to the OWNER, so
something holding a TCP and a UDP gives each a different one and tells their
replies apart in a single handler.

**Answer failures on the callback, never with a property.** `TCPStart`'s BOOL
does not survive the trip, so a start that fails reports itself
(`TCP_ERROR_CALLBACK`, or an EOF on the reply). A stop the driver asked for
says nothing - it already knows, and a queued answer would land after the next
start and undo it.

**Why this is not fussiness:** anything you expose *will* be used, and then it
is a contract you never meant to sign. `tcp.c` published `In`, `Out`,
`Connected`, `Secured`, `LocalAddr` and a `Conn` tag; within weeks `main.c`,
`router`, `http`, `websocket`, `bridge`, `tcpport`, `mcpsource` and `tplink`
all depended on them, which is why fixing it is a seven-file change instead of
a one-file change.

The rest of the shape: `Enable`-style gating belongs to widgets, not here; one
task for the instance's whole life, re-armed **from inside its own callback**;
`DeleteTask` in `InstanceEnd`; drain everything waiting each tick; `SndMsg`
takes ownership of its data, `DeliverMsg` does not (it is synchronous, so the
caller frees).

---

## 5. A widget that drives an object (the pair)

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
SetPropStr (args, "Callback", "Callback");       /* this panel's own port */
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
a flag - not in `Activate`, because a loaded or imported instance never gets
one. Setup clears the momentary commands, forces the resting presentation
UNCONDITIONALLY (whatever the file said), stops the object for real, and only
then honours the auto option by pressing the command a person would press.

**Commands are momentary**: write the command property back to `"0"` after
acting, or the press becomes saved state and a load re-presses it.

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
