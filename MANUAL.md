# GrokThink Canvas — User Manual

## What you are looking at

A running GrokThink session is a set of live objects in the engine —
sliders, pulses, readers, views, wires. The browser window is only a
viewport onto that session. Nothing runs in the page: every gesture you
make becomes one command to the engine, and everything you see is the
engine reporting back. Open two browsers on the same server and you are
both looking at, and working in, the same one session.

Start the server and browse to it:

    ./framework          # serves the canvas on 0.0.0.0:8083
    http://<server>:8083

The **Palette** opens on the left: one specimen of every loadable object
class. It is not a special widget — it is an ordinary View that happens
to be seeded at boot and to start open. Everything in this manual that
works anywhere works inside it too.

## Icons, panels, and views

Every thing on the canvas is an **icon**. The icon is the thing's
permanent presence: it never disappears, and it lives somewhere in the
containment hierarchy — the root canvas, or inside a view, or inside a
view inside a view.

Most things can **open** into a **panel** — a floating window holding
the thing's contents or controls. Panels are all peers: whatever view an
icon nests in, its panel opens at the top level. The panel has its own
position, independent of the icon — drag it by its titlebar, close it
with the **−** in the corner. The icon does not move when the panel
does; they are two placements of one thing.

Panel position is shared (everyone sees a panel where it was last put).
Whether a panel is currently open is **your window's own business** —
like scroll position, opening a panel doesn't fling it onto your
teammate's screen.

**Views** are the containers. A view's icon can live inside another
view, its panel holds its member icons, and a closed view's contents
aren't even sent to your browser — they stream in the first time you
open it. Root is just the outermost view; the palette is just a view;
the control panels described below are views too. There is only one
kind of container.

## The Mode menu

The mode decides what your mouse means. It is shared session state —
switching modes switches for every window (it's just a property, like
everything else).

| Mode    | Gesture | Effect |
|---------|---------|--------|
| Operate | click / drag controls | use the objects: move sliders, toggle checkboxes, open panels |
| Clone   | click, then click | pick up a copy, place an independent snapshot |
| Alias   | click, then click | pick up a doorway, place a live link |
| Move    | press-drag-release | reposition; crossing into a view moves it there |
| Connect | click port, click port | wire two ports together |
| Delete  | click | remove a thing (red outline warns you first) |
| Options | click | open the thing's internals — the dissection table |

## Clone — independent copies

In Clone mode, **click a thing to pick it up** (a ghost follows your
pointer), then **click where it should live** — inside any open view
panel, or on the root canvas. **Esc cancels** a pick-up. Where you
release is exactly where it lands.

A clone is a **new, independent instance** carrying a snapshot of the
source's data at that moment. Change the original afterward and the
clone doesn't move. Three consequences of uniformity:

- **Cloning an alias clones the thing it points at** — a snapshot of the
  target, not a copy of the doorway.
- **Cloning a view clones everything in it**, recursively. Aliases inside
  the view that pointed at things inside the view are remapped to point
  at the clones — a self-contained panel stays self-contained.
- The palette is how you make new things: clone a specimen out of it.
  And clone your own things *into* the palette to grow it.

## Alias — doorways

In Alias mode, the same pick-then-place gesture makes an **alias**: a
real object that *stands for* one property of another. Its value, its
subscribers, and anything wired to it all live on the original — an
alias adds no copies and nothing to keep in sync. Twelve aliases of one
slider are twelve handles on the same one value; drag any of them and
all of them (and the slider) move together.

What you can pick up in Alias mode:

- **A property row inside a panel** — "copying a control out of a
  widget." Drop it in your own view and you've started an instrument
  panel.
- **A widget atom** (a standalone slider, LED, ...) — aliases its
  primary control.
- **An icon** (a view or a composite object) — the alias is another
  icon that opens the *same* panel.
- **Another alias** — chains collapse; you always get a doorway to the
  original.

An alias's **presentation is its own**: its position, its Label, and its
Widget (render the same value as a knob here and a textbox there) can be
changed without touching the original. If the original is deleted, the
alias survives as a dead control.

## Move — where things live

In Move mode, press-drag-release. Dropping inside a view's open panel
moves the thing **into** that view; dropping on the root canvas moves it
out to the top level. Where a thing lives is part of its identity: its
full path changes when it moves (`/Root/Slider_1` becoming
`/Root/MyPanel/Slider_1`). Release over the panel's inner area — a drop
on the titlebar or off the panel just repositions at the root.

## Connect — wiring

In Connect mode, port dots appear. Click one port, then another (either
order) and they are wired: messages flow from source to sink through the
engine. Any property is a valid endpoint — a compiled port (`In`,
`Enable`), a plain data property (`Value`, `Filename`), or `Activate`
(a Button wired there presses the target). Wiring **to or from an
alias wires the original** — that's what doorways are for. Existing
connections draw when you enter the mode; a wire between two members
of a view draws inside that view and travels with it. The line only
appears when the engine confirms the wire, so every open window shows
the same connections. Each wire carries an **× at its midpoint — click
it to disconnect** (the raw verb is `disconnect`, the exact inverse of
`connect`).

## Delete

In Delete mode, click a thing to remove it. The red outline shows what's
about to go. Things marked `Deletable=0` refuse. Everything in the
palette is deletable — a restart reseeds it, nothing there is precious.

## Options — the dissection table

In Options mode, click **anything** and its **internals view** opens:
the whole of the object's state laid out like a frog on a dissection
table — one live control per published property. Not a curated subset:
Value, State, Name, X, Y, W, H, Container, Deletable, Open, PanelX,
PanelY, ports — all of it.

Each row is a real **Alias** bound to that property, and the table is a
real **View** — which means everything above applies *to the table
itself*: drag its rows around, delete rows you don't care about, clone a
row (snapshotting the whole object), alias a row out into your own
panel, wire a row's control to something. The table is built once, the
first time anyone asks, and every later ask — from any window, from any
alias of the thing — opens that same one view.

Because `X` is on the table, dragging the X control moves the icon.
Because `Name` is on the table, typing there **renames the thing**
(labels follow, the path re-keys, state rides along). Because `Enable`
is on the table, you can wire a pulse to it without ever opening
anything else.

## Names and paths

Every thing shows its **name** on its label; hover for the full **path**
(`/Root/MyPanel/Slider_1`) — the path is where it lives plus what it's
called, and it is the identity every script command uses. `Name` is an
ordinary property: edit it on the dissection table to rename. Names are
unique within a container; slashes aren't allowed.

## Saving and loading

The **File** menu (top bar) opens a file dialog for Save, Load, and
Import. Flows live as named files in the **saved/** directory next to
the framework (created automatically); the dialog lists what's there —
click a name or type a new one. What's saved is the command history
that built the session — replaying it rebuilds your objects, wiring,
views, and panels — and edits made through a panel's controls are
recorded against the object itself, so they replay no matter which
doorway made them. A load becomes part of the session's own history:
Load then Save keeps everything.

Loading adds to the canvas; it doesn't clear it. If a loaded name is
still in use, the copy is named fresh and everything the flow said
about it follows to the new name — so a loaded view's controls drive
that view's own objects, never the ones it was saved from.

Scripts get the same three verbs, plus the listing:

    {"cmd":"save-flow","file":"myapp"}
    {"cmd":"load-flow","file":"myapp"}     // import-flow is the same for now
    {"cmd":"list-flows"}                   // one flow-file event per saved flow

## Scripting

Everything the canvas does travels as one-line JSON commands over the
web port's WebSocket — and the same protocol can stand up its own raw
TCP surface, because a transport is just objects plus wiring. Send
these over the WebSocket and port **8091** starts answering raw JSON
(this is exactly how the test harness bootstraps itself —
`ensure_raw_bridge` in testharness/rawtest.py):

    {"cmd":"create-instance","class":"TCP","as":"/Root/RawTcp","hidden":"1"}
    {"cmd":"set-property","instance":"/Root/RawTcp","prop":"LocalPort","value":"8091"}
    {"cmd":"create-instance","class":"Bridge","as":"/Root/RawBridge","hidden":"1"}
    {"cmd":"connect","from":"/Root/RawTcp","fromPort":"Out","to":"/Root/RawBridge","toPort":"In"}
    {"cmd":"connect","from":"/Root/RawBridge","fromPort":"Out","to":"/Root/RawTcp","toPort":"In"}
    {"cmd":"activate","instance":"/Root/RawBridge"}
    {"cmd":"activate","instance":"/Root/RawTcp"}

The command vocabulary, on either transport:

    {"cmd":"create-instance","class":"Slider","container":"","x":"510","y":"40"}
    {"cmd":"clone-instance","of":"/Root/Slider_1","container":"/Root/MyPanel","x":"20","y":"20"}
    {"cmd":"create-alias","of":"/Root/Slider_1","prop":"Value","container":"","x":"300","y":"60"}
    {"cmd":"move-instance","of":"/Root/Slider_1","container":"/Root/MyPanel","x":"25","y":"35"}
    {"cmd":"set-property","instance":"/Root/Slider_1","prop":"Value","value":"42"}
    {"cmd":"connect","from":"/Root/Slider_1","fromPort":"Value","to":"/Root/LED_1","toPort":"In"}
    {"cmd":"disconnect","from":"/Root/Slider_1","fromPort":"Value","to":"/Root/LED_1","toPort":"In"}
    {"cmd":"list-connections"}          // every live wire, as connected events
    {"cmd":"subscribe","instance":"/Root/Slider_1","port":"Value"}
    {"cmd":"internals","instance":"/Root/Slider_1"}
    {"cmd":"list-instances"}            // root; add "container" for a view's members
    {"cmd":"delete-instance","instance":"/Root/Slider_1"}

Birth and placement are one command: `create-instance` (like
`clone-instance` and `create-alias`) carries `container`/`x`/`y`, and the
**server names the result** — the `instance-created` event teaches you the
name (`/Root/Slider_1`). Supplying your own name with `as` still works.
`move-instance` is the whole drop gesture in one verb: re-container,
reposition, rename; the engine refuses to move a view into itself or a
descendant. Aliases are stamped at birth with the target property's
published `Widget` type (and `Direction`) — a client renders what the
alias says, it never has to look the type up. `connect` reaches **any**
property (compiled port, plain data property, or `Activate`) — one verb
for every wire — and `disconnect` is its exact inverse; both are
announced to every connection viewing the endpoints (`connected` /
`disconnected` events), which is the only thing a client draws from.

Events come back as JSON too — and only for what you're looking at: you
receive updates for things you subscribed to and containers you listed,
nothing else. A window (or script) that never opened a view never hears
about its insides. The GUI has no powers beyond this protocol; anything
it can do, your script can do in the same one command.

## Script objects (Lua)

The **Script** class in the palette is a Lua interpreter as an ordinary
dataflow object. Its `Source` property holds the script (edit it on the
dissection table); **Activate runs it** — and re-activating re-runs the
current Source, which is the whole development loop. A script has real
`In`/`Out` ports, so it sits in a flow like any compiled object.

Subscribing to updates is just wiring: connect anything's `Out` to the
script's `In`, and the function you register with `oninput` is called on
every message — directly, inside the engine. A pulse counter:

    local count = 0
    oninput(function(value, kind)
      if kind == "eof" then log("done, counted " .. count) return end
      if value == "1" then
        count = count + 1
        send(tostring(count))
      end
    end)

Wire `Pulse.Out -> Script.In`, activate both, and the count flows out
the script's `Out` port for anything downstream — a Label, a wire, a
subscriber. The script's API: `oninput(fn)`, `send(value)`,
`getprop(name)`, `setprop(name, value)`, `log(text)`. Callbacks run
inside message delivery like any compiled handler — keep them short and
never busy-wait; the fabric is single-threaded.

## Testing

    ./testharness/run.sh

kills any running server, rebuilds from clean, starts a fresh server on
the default port, and drives it with real mouse input through a headless
browser — you can watch live from your own browser while it runs. Every
check prints what was *expected* and what was *observed*. Each test
stages its leftovers in its own labelled view (CloneTest, AliasTest,
OptionsTest, ...), the session is put back in Operate mode at the end,
and the server is left running so you can dissect the results.

## Rough edges worth knowing

- The mode is shared — a second person switching to Delete changes what
  *your* clicks do. (Per-user modes are a future refinement.)
- Renaming or moving a **view** does not yet cascade into its members'
  recorded paths; renaming leaf objects is solid.
- A Move drop must land on a panel's inner area to enter the view;
  dropping *on* a panel's frame repositions at root, possibly underneath
  the panel.
- `session.flow` files from before mid-July 2026 contain accumulated
  helper-widget debris; re-save once to clean them.
