# Roadmap: from framework to web canvas

*"Connect the world" — the VNOS vision, Singlestep Technologies, ~2001.
This document is that vision's build order, resumed.*

The destination: a web app where people log into the server, see their
canvas, drag objects from a palette into dataflows, wire them together,
and watch/control them through skinned instrument panels — LEDs,
sliders, VU meters, text outputs, buttons. A finished flow can itself
become an object with inputs and outputs, back on the palette.

The reason this is a roadmap and not a rewrite: every layer leans on
structure that already exists. The node tree is the universal data
model, messages are the universal transport, and the registry already
publishes what objects exist. The browser is one more subscriber.

Why now: in 2000 the crushing complexity was the client — a text
editor, QuickTime, a skinning engine, a drawing layer, all welded
into the app and rebuilt per platform. HTML5 absorbed every one of
those (video tag, editable text, canvas/SVG, CSS, WebSocket) into a
universal client that ships pre-installed on every device. Handing
the view to the browser is the key move, and it only works because
the app was always an empty view: the objects are the functionality,
so the view was always replaceable.

Current state (July 2026): loading/registration, message routing with
fan-out, ports, the Enable control plane, scheduler-driven lifecycles
with emergent shutdown, and working objects: Reader, Writer, Out
(probe), Filter, Pulse, Queue/Stack, TCP server. See CLAUDE.md.

---

## Phase 1 — Foundation: the tree becomes a document

Everything the web app touches — palette, canvas, skins, saved flows,
wire protocol — is a node tree in text form. Serialize first;
everything after this is cheap.

1. **Node tree ⇄ text serialization** (JSON for the browser era; the
   old main.c comment says XML — JSON is the same shape, cheaper in
   the client). `NodeToText()` / `TextToNode()` in the core.
   *Already present: the tree, PrintNode as the debug ancestor.*
2. **Binary-safe payloads**: a `Length` property beside the data so
   messages can carry arbitrary bytes. Needed before WebSockets.
3. **Flow persistence**: save/load a container — instances, property
   values, connections — as a flow file. `CreateTestApp()` becomes
   `LoadFlow("default.flow")`. This file format *is* the canvas
   document.
   *Already present: containers, the registry tree, instance nodes.*
   *Shipped (July 2026) as ACTION REPLAY: save records the flow log —
   the command history that built the session — and load re-dispatches
   each command through the same path a live one uses (Bridge_LoadFlow,
   bridge.c). This re-binds function pointers for free (re-running
   InstanceStart) and doubles as a human-readable script, but see 3a.*
3a. **Serialize the node view directly, for scale.** Action-replay
   loads by RE-DOING the work: replaying every clone re-deep-copies,
   every connect re-wires, so load cost is proportional to everything
   the session ever did, not to the size of its final state — it does
   not survive a million nodes. The scalable model is the node tree's
   own nature: ask a node (a container/view) to serialize itself, write
   the stream to disk; load reads it back and the tree SELF-ASSEMBLES —
   the identical operation in reverse, O(state), no replay, no
   re-cloning. Because everything is a node this is one recursive
   emit/absorb pair on the node, not a per-class saver. Format is a
   pluggable decorator — XML, JSON, others — over the same walk, so the
   wire era's JSON and a compact archive format are the same code with a
   different skin. The one real constraint (the reason replay exists) is
   that a node carries process-specific pointers — OnMsg/Activate/
   InstanceStart, the malloc'd `local` — which must NOT serialize as
   their raw addresses; the load path re-binds them by class (the shell
   comes from InstanceStart, the data pours in), so serialization
   persists DATA + references and code is re-established on absorb. This
   supersedes action-replay as the persistence mechanism at scale; the
   flow log can remain as the editable-script/export view.
3b. **Mount a load at any point — import/export subtrees.** Because
   emit/absorb is one operation on ANY node, it is not just whole-session
   save: you serialize any container/view (a subtree) on its own, and
   absorb it at a chosen mount point in another tree — export from
   anywhere, import anywhere. Whole-session save is just the root
   subtree; there is no separate "document" concept, only subtrees
   grafted and pruned. The mount point re-bases the subtree: every path
   inside it swaps its old container prefix for the mount's — the exact
   subtree re-path already built for clone/move (Bridge_RepathSubtree,
   bridge.c, and the engine's CloneView walk), reused on absorb, with the
   same collision-safe renaming a clone uses when a name is already taken
   at the destination. This makes flows composable: a saved panel drops
   into any session; a library of subtrees is assembled from pieces; a
   huge tree loads lazily, one subtree absorbed per mount as its
   container is opened (the same visibility-scoped streaming
   list-instances already does), so "a million nodes deep" never has to
   materialize all at once.
4. **Interface publication**: at ClassStart each class registers a
   description of its ports and properties (name, direction, widget
   type, default value). This is the palette's data source and the
   default skin generator.
   *Already present: the "objects publish their own interface"
   improvement note in main.c; class nodes in the registry.*
5. **Addressing**: name/path lookup so anything can be found as
   `Main/Users/jim/Canvas1/Reader1`.
   *Already present: namespace.c (currently unused), node names,
   parent pointers.*

## Phase 2 — Objects grow skins

The structure is already in reader.c's `SetSubProp()`: each
user-facing property carries sub-properties — `graphics` (widget
type), `OnChange` (callback), `local`. Make that the standard.

No special categories, anywhere in this phase: not "ports vs
properties," not "watchable vs plain," not "connectable vs not." A
property is a property. Every one of them is a valid wire endpoint in
both directions and every object has connectivity by default, with no
per-property or per-object opt-in.

1. **Extend the widget vocabulary**: PROP_SLIDER, PROP_VUMETER,
   PROP_TEXTOUT, PROP_KNOB, PROP_LABEL alongside the existing
   PROP_TEXTBOX / PROP_LED / PROP_BUTTON / PROP_CHECKBOX.
   *Done: objects/widget (8 classes) and objects/button.*
2. **Every property is connectable by default — not an opt-in.**
   `WatchableProp()` today has to be called per property, by an
   object's own C code, before that property fans out to subscribers
   on change; a property nobody remembered to call it on just sits
   there silently. That gate should not exist: applying a write and
   fanning it out to subscribers should be what every property write
   does, unconditionally, the same behavior WatchableProp already
   proves works (`Connect(Pulse, "State", LED, "In")` runs live over
   the bridge today) — just without requiring the object author to
   have asked for it.
   *Done (July 2026): SetProp* fans out to a property's Subscriber
   children unconditionally on every write (FanOutSubscribers, node.c);
   WatchableProp() survives only as a no-op for old call sites.*
3. **`Connect()` reaches any property — retiring bind-property/
   bind-activate as a separate mechanism.** `ConnectToProperty`/
   `ConnectToActivate` (object.c) and the Bridge's bind-property/
   bind-activate commands exist purely because Connect() only works
   today when the target already has a compiled-in OnMsg handler on a
   named port — a plain data property has none, so wiring a widget's
   Value into an arbitrary property needed a bare adapter node
   standing in as a translator. Give every property a *default*
   handler ("store whatever arrives") and Connect() itself works
   uniformly against any property name on any instance. A port like
   Enable or In still installs its own handler when it needs real
   logic beyond storing a value, and that handler still wins — but
   that is an override on a universal default, not the only way
   anything gets wired. Plain Connect() was always the right verb; it
   just was not universal yet. The adapter node type and the two extra
   Bridge commands retire once this lands.
   *Done (July 2026): a Subscriber records {Instance, Port, Callback};
   delivery with no Callback applies the universal default - store what
   arrived (DeliverToSubscriber, node.c, shared by both fan-out
   walkers). Activate is an ordinary port (ActivateOnMsg stamped by
   RegisterInstance). The adapters are deleted; bind-property/
   bind-activate survive only as bridge dispatch synonyms for connect
   so recorded flows replay. Because the record names the REAL sink,
   list-connections, CloneConnections, Disconnect and the delete scrub
   all read the same graph - no adapter special-casing anywhere.
   Proven raw-first in testharness/connectiontest.py.*
4. **View: the container primitive.** Everything past this point
   depends on View existing as a real class, not a special case. A
   View is a first-class object on the palette exactly like LED or
   Button — the one thing that makes it a View is what it *holds*: a
   table of child slots, each recording `{instance, X, Y, Width,
   Height}` — the panel-builder shape the VNOS reference demo objects
   already used (`objects/demo/pulsegenerator/pulsepb.c`'s
   `ControlInfo[]`: control class, bound variable, X, Y, W, H per row)
   brought into this framework's own idiom. The slot table is
   properties on the View instance itself (the same pattern
   `GetPalette()` already uses for its class-name → instance lookup),
   not a parallel C array — and it is *not* real NodeObj tree
   reparenting: an instance's tree parent stays its class node
   (`Bridge_Subscribe` and everything like it depends on
   `GetParent(inst)` reaching the class), so containment is a second,
   independent relationship a View records about an instance, the same
   way Palette records "this instance is the Reader catalog entry"
   without becoming its tree parent. Placing an instance into a View —
   dragging it there fresh, or dragging it to a new spot within one it
   is already in — is one command that upserts a slot. This is real,
   persistent server state, not something a client re-derives or
   regenerates on reconnect (see Phase 4's opening paragraph).
   A View has two presentation states, and switching between them is
   purely visual — it touches nothing in the object graph: **icon**
   (collapsed — a small representation, contents not shown) and
   **open** (the full panel, children laid out per their slots).
   Clicking the icon opens it; "Lower" in the open panel's title bar
   collapses it back. Wires attached to a View's own In/Out (below)
   stay fully live in either state — a collapsed View is still a
   legitimate thing to wire to or from, same as anything else, because
   collapsing it never touched the graph, only the rendering of it.
   A View can additionally declare, through its own settings (the same
   settings-panel mechanism every object gets, Phase 2.5), which of
   its children's ports or properties are aliased as *its own* In and
   Out — this is what makes a fully-composed View (a Slider wired to a
   VU meter, say) usable as one black-box unit from outside, without
   whatever is wiring to it needing to know what's inside. This is
   Phase 5's "container ports" idea, concretely mechanized. It is a
   convenience, not a requirement — every property of every child
   inside a View stays individually wireable with or without this
   curation, because nothing needed an opt-in to be connectable in the
   first place (Phase 2.2).
5. **Skin every existing object — by composing a View, not
   generating one.** A Reader's control panel is a View instance
   holding a Textbox (wired to Filename), an LED (wired to State), and
   a Button (wired to Activate) — all through plain Connect() (Phase
   2.3), built once, the same way a user would build any other View.
   From then on it simply exists: opening it again is observation, not
   construction. Every other object gets the same treatment: Writer,
   Pulse (interval knob, count box, enable LED, out LED), Filter (mode
   textbox), Queue (depth VU meter), TCP (port box, connection LED),
   Out (text output).
6. **Every primitive owns its own presentation.** No shared generic
   fallback rendering across widget types — a VU meter must not
   degrade to the same plain text readout a Label uses. Each widget
   class gets code that genuinely renders what it is. Short term this
   is a real per-class function in the browser client (Phase 4); the
   longer-run version is Phase 6/7's federation idea turned inward — a
   widget class eventually ships (or points to) its own rendering, the
   same way it will eventually ship its own script-defined behavior.

## Phase 3 — The wire: protocol and server

1. **HTTP object** on top of the TCP object: enough to serve the
   static web app files (Reader → HTTP → browser: the cat flow grows
   up). Dogfood: the framework serves its own UI.
2. **WebSocket object**: handshake + framing over the TCP object.
   *Already present: TCP server, buff, the port pattern.*
3. **Bridge object / control protocol**: JSON commands in
   (create-instance, connect, set-property, activate, subscribe) and
   events out (property-changed, message-flowed, instance-created).
   Note the verbs are exactly the existing C API: CreateObject,
   Connect, SetProp*, ActivateInstance, plus a probe. The protocol is
   a veneer over functions that already work.
   *Done: create-instance, connect, disconnect, set-property, activate,
   subscribe, list-instances (replays the palette and the live session
   to a (re)connecting client — see Phase 2.4/4), list-connections
   (walks the live subscription graph — every wire, whatever made it).
   bind-property/bind-activate are retired (Phase 2.3, July 2026): they
   were a workaround for a gap that closed, and survive only as
   dispatch synonyms for connect so recorded flows replay.
   Still needed:*
   - *`Disconnect` — done (July 2026): `Disconnect()` in object.c is
     Connect()'s exact inverse (same alias resolution, removes the one
     matching {Instance, Port} record); the bridge's `disconnect` verb
     drives Connect mode's per-wire "×", and every wire made or removed
     is announced (`connected`/`disconnected` events, scoped to viewers
     of the endpoints' containers) — the client draws and erases only
     from those events.*
   - *`place`, to put an instance in a View's slot table (X/Y/W/H) —
     used both for a fresh placement and for Move mode repositioning an
     instance already there — and `unplace`, to remove just that one
     slot (Delete: removes the relationship, never the instance itself,
     see Phase 4).*
   - *A View-contents query — the same "describe what already exists"
     shape `list-instances` already has, scoped to one View's slots
     instead of the whole session.*
   - *`clone`, given a source instance, creates a new one of the same
     class and copies its current property values — one generic
     command, since nothing about cloning is per-class (Phase 2.2's
     "every property is uniformly gettable/settable" is what makes this
     possible without per-widget-type code).*
4. **Live taps**: "subscribe" attaches a JSON-emitting probe variant
   to any port or property, streaming over the socket. The instrument
   panel is a bundle of taps.
5. **Sessions and login**: users as nodes (`Main/Users/<name>`), each
   with canvas containers; token auth first, TLS later.
   *Already present: objects/network/testkey/ certs, SSL code paths
   in the VNOS reference TCPObject.c.*
   *Path scheme, once this lands: the current-path aliasing built for
   the Palette/View work (`/Root/...`, renamed live as an instance's
   Container changes - Bridge_Rename, bridge.c) extends the same way
   it already nests Views: `/User/Root/...`. A user is just another
   level a path can live under, with its own ordinary properties
   beside its own Root (connection state, whether currently connected,
   last-seen) - the same "everything is a node" uniformity, not a
   separate user subsystem bolted on. This is what makes "log in
   later just to check results or pull a report, without being live
   in the session" fall out for free: the user's Root and its
   contents are real, addressable, persistent state regardless of
   whether anyone is currently connected to view it. Not yet coded -
   noted here for when this phase starts.*

## Phase 4 — The browser client

The canvas is a View. Not "represented by" one — it *is* one: the same
class every user-composed panel is built from, rooted at a Canvas View
under the connecting user (`CreateUser`'s existing per-user Canvas
container, object.c, is exactly this slot, just not a View yet).
Connecting doesn't build anything: the client asks for a View's current
contents — instances, their classes, their slot positions, their
wiring — and renders exactly that. A client that creates or wires
anything just because it received a replayed instance is a bug, not a
feature. Creation only ever happens from an explicit user action (a
palette drag, a wire drawn between two dots), never as a side effect of
observing state that already exists. (This retires the `hidden`-flag
patch entirely — it existed only to hide duplicate plumbing that
shouldn't have been recreated in the first place.)

1. **Palette**: one real, inert instance of every registered class
   (`GetPalette()`/`BuildPalette()`, object.c) — including View itself,
   so "drag out an empty container" is an ordinary palette action, not
   a special button. *Done, modulo BuildPalette's Phase 8 promotion out
   of the core library.*
2. **Drag from palette → place**: dropping a class onto empty canvas
   space is create-instance + a `place` in the Canvas View at the drop
   coordinates; dropping it onto an *open* View places it in that View
   instead.
3. **Wiring**: Connect()/SndMsg, exactly the Reader.Out → Writer.In
   wiring already built, now reaching every property once Phase 2.3
   lands, not just compiled ports — driven by Connect mode's two-click
   source/destination gesture (Phase 4.6), not a dedicated port-dot
   drag.
4. **Opening a settings panel**: a composite object's control panel is
   just its associated View (Phase 2.5) — "open settings" means
   subscribe to and render that View's existing contents, the same code
   path as opening any other View, including one a user hand-built from
   scratch. There is no separate "default skin" mechanism running in
   parallel to maintain.
5. **Widgets render themselves distinctly** (Phase 2.6): LED, slider,
   VU meter, text output, button, checkbox, textbox — each its own
   rendering, never a shared fallback.
6. **Interaction modes.** Use mode (normal interaction — slider drags
   change its value, a button click presses it, a textbox click focuses
   it for typing) is the default and always the resting state. Every
   other mode is reached one of two ways, and both drive the same
   underlying verbs:
   - **Ctrl+click a control** pops up a radial "circle of modes"
     (Settings, Connect, Clone, Move) centered on it. Clicking a choice
     from the circle commits to a single, one-shot action scoped to the
     control you Ctrl+clicked:
     - *Connect*: a wire now follows the cursor from that control; the
       next click, on a different control, completes exactly one wire
       (`connect`, Output → Input, direction inferred from which side
       is a source vs a sink — the class Interface already knows this)
       and the interaction ends, back to Use mode.
     - *Move*: the control itself now follows the cursor; the next
       click drops it at that location (`place` with the new X/Y for
       its existing alias — the same instance, never recreated) and
       ends.
     - *Clone*: a copy follows the cursor; the next click places the
       new instance (`clone`, Phase 3.3 — same class, same starting
       property values as the source) and ends.
     - *Settings*: fires immediately, no second click — opens (or
       creates, if one doesn't exist yet) the control's associated
       settings View (Phase 2.5). There is no destination to pick.
     **Esc** cancels whatever is pending at any point and returns to
     Use mode with nothing committed.
   - **A persistent per-View mode toggle**, in the View's own chrome,
     for bulk work — laying out or wiring up a lot of things at once,
     where re-opening the circle before every single action would be
     friction. Setting a View to Connect mode, say, doesn't change the
     gesture itself: it is still a strict two-click pair, source then
     destination, one wire per pair — the toggle just means the View
     *stays* in Connect mode after each pair completes, instead of
     reverting to Use mode the way the Ctrl+click version does. Ten
     wires is still ten source/destination pairs either way; the
     toggle only saves you from re-inviting the circle-menu each time.
     There is no "arm once, fan out to many targets in one continuous
     sequence" shortcut anywhere in either version — connecting one
     thing to ten others is ten two-click pairs, always.
   Regardless of which way a wire was made, every visible wire (Connect
   mode rendering) carries a "×" that sends `disconnect`.
   Every open View's title bar carries **Lower** (collapse to its icon
   state, Phase 2.4 — purely visual, the wiring underneath is
   untouched) and **Delete**, which removes only the one relationship
   you're pointed at — a slot (this instance is no longer *in* this
   View) or a wire (these two are no longer connected) — never the
   underlying instance itself. Deleting an instance outright, if that
   is ever exposed as its own action, is a different, more destructive
   operation and should look different in the UI, not share Delete's
   button.

### Worked example: wiring a slider to a VU meter, live

1. Drag `Slider` from the palette onto the canvas — create-instance,
   placed in the Canvas View at the drop point. Drag `VUMeter` the same
   way.
2. Ctrl+click the Slider, pick Connect from the circle. A wire now
   follows the cursor. Click the VU meter: this sends `connect` — the
   same Connect() call that already wires Reader.Out to Writer.In, just
   aimed at the Slider's Value property directly (Phase 2.3, no
   adapter). The interaction ends there, back to Use mode.
3. Move the slider (an ordinary Use-mode drag): its own control sends
   set-property on itself, which (every property fans out by default,
   Phase 2.2) reaches the VU meter's In handler, which sets the VU
   meter's own Value, which itself fans out a property-changed the
   browser is subscribed to. Real messages, not a client-side
   simulation of what the wire does — the VU meter moves because the
   object graph actually moved.

### Worked example: opening a settings panel

1. A Reader's settings panel is a View instance already holding a
   Textbox (Filename), an LED (State), and a Button (Activate), built
   once — either auto-composed the first time a Reader without a panel
   is dragged out (a convenience default), or hand-built by a user the
   same way any View is built.
2. "Opening" it sends the View-contents query (Phase 3.3) and renders
   what comes back. No create-instance, no connect, get sent — those
   already happened, once, when the panel was built. Reconnecting, or a
   second viewer opening the same panel, gets the identical render from
   the identical query.

## Phase 5 — Flows become objects (composition)

1. **Container ports**: a container publishes named In/Out ports that
   alias ports of inner instances; SndMsg to a container port routes
   inward. One new subscription-record type.
   *Concretely: View's settings-based In/Out aliasing (Phase 2.4) — a
   composed View is already this, once that lands.*
2. **Composite classes**: register a saved flow file in the registry
   as a class — its InstanceStart loads and wires the inner flow.
   Composites appear on the palette beside the C classes,
   indistinguishable to the user.
3. **Nesting and versioning**: composites inside composites; every
   library already carries a UUID, composites get one too.

## Phase 6 — Federated palettes: web and MCP

The palette stops being "what is compiled in" and becomes "what is
reachable." External frameworks contribute objects the same way C
modules do: by publishing an interface into the registry. The
interface-publication format from Phase 1.4 is the import target —
an external schema translates into the same published-interface
nodes, so the palette and canvas treat a remote tool exactly like a
compiled class. Instances of imported classes are generic proxy
objects bound to their connector.

1. **HTTP client object**: request out, response in — the generic
   web primitive (the client half of the TCP object plus the Phase 3
   HTTP layer, meeting the async-dns module for resolution).
2. **Web API wrapper classes**: generated from OpenAPI/REST
   descriptions — each endpoint a palette object with typed input
   properties and a response Out port. Webhook receiver object for
   the inbound direction (a route on the HTTP server → an Out port).
3. **MCP client object**: connects to an MCP server (stdio or HTTP
   transport), calls tools/list, and registers each tool as a class:
   the tool's input schema becomes properties/ports with widgets,
   results flow out Out. Dragging "search" from the palette drops a
   live tool into a dataflow. MCP resources map to reader-like
   sources, prompts to template objects.
4. **The framework as an MCP server** (the symmetric direction):
   published flows become MCP tools, so LLM agents can invoke
   user-built dataflows — and, given create/connect commands from the
   Phase 3 bridge, build flows themselves. The canvas becomes
   something an agent and a human can edit together.
5. **Connector lifecycle**: connectors are ordinary objects with
   Enable lines and State LEDs, so a dead MCP server or API outage
   shows up on the panel like any other instrument, and a timer can
   retire a connector the same way one retires the TCP server.

## Phase 7 — Languages as extensions

Scripting is not a special subsystem: a language runtime is one more
loadable object, and scripts reach the fabric through the same API
everything else uses. The fabric already permits this — handlers are
function pointers stored as node properties, and nothing cares
whether a pointer targets compiled C or a trampoline into an
interpreter.

1. **The extension API**: formalize the surface scripts get — the
   five host calls plus CreateObject / Connect / SndMsg /
   SetProp*/GetProp* / RegisterLibrary / RegisterClass /
   RegisterInstance. It is already all opaque pointers and scalars,
   so bindings are mechanical in any language with an FFI.
2. **Language host objects**: a `.object` embedding an interpreter
   (Lua first — it fits the binary discipline; Python and JS after).
   Instances load a script; script functions become handlers via
   trampolines (the Callback long property points at the trampoline,
   a sibling property carries the script reference).
3. **Script-defined classes**: scripts register real classes on the
   palette — indistinguishable from C classes, same as composites
   (Phase 5) and federated tools (Phase 6). Third form of the same
   trick: classes defined by data.
4. **Overrides**: the revived Intercept path (Phase 2.2) doubles as
   the override hook — attach a script to any property or port of a
   *compiled* object to wrap, filter, or replace its behavior with no
   recompile. The emailed-fix support model at its finest grain: a
   fix can be a script in a flow file.
5. **Binding generation**: published interfaces (Phase 1.4) generate
   per-language stubs — the 2003 main.c comment verbatim: add an
   object and every language sees it; add a language and it receives
   definitions for every existing object.

## Phase 8 — Hardening as it starts to matter

- **Everything but the loader becomes an object**: `libframework.so`
  should contain only what bootstraps the loading mechanism itself —
  the node tree, the scheduler, dlopen/registration, message routing —
  nothing a loaded object could instead be. TCP already proves the
  discipline works: a "core" networking feature, built entirely as
  `objects/network/tcp.o` and linked against the library rather than
  baked into it, so `libframework.so` doesn't know TCP exists any more
  than it knows Reader does. The debt is everywhere the discipline got
  skipped instead of followed: `CreateTestApp()`'s six demo flows are
  still hand-built C calls in main.c rather than a loaded `default.flow`
  (Phase 1.3 already calls this out); `BuildPalette()` (object.c) —
  walking the registry to instantiate one of everything — is core-library
  logic that is itself a candidate to become an object rather than a
  function main.c calls at startup. The test for anything new proposed
  for the core: does this bootstrap the load mechanism, or could it be
  a loaded object like everything else?
- **Dependency-ordered loading**: `InstallObjects()`'s two-phase split —
  scan and load every `.object` first, only then call `loadClasses()` —
  already exists specifically so this could be added later (see main.c's
  own comments on the deferred second phase). Today, scan order is just
  directory order. Objects should be able to declare what they need; a
  collection step gathers every scanned `.object` before any `ClassStart`
  runs, topologically sorts by declared dependencies, and only then
  starts classes in that order. This is what makes "move things out of
  core" actually safe at scale: an object that needs another object's
  class to exist first (subclassing, a composite depending on its parts)
  declares it instead of relying on scan-order luck.
- TCP client mode and multi-connection (the ring pattern and
  connecting state machine in the VNOS TCPObject.c reference).
  Client mode brings `async-dns/` into the build: hostname resolution
  without ever blocking the fabric (worker thread + sentinel flag,
  results delivered as main-loop callbacks).
- Object self-tests registered through the registry so `-t`
  exercises every loaded object (the long-standing testing roadmap).
- Scheduler: implement RemoveTask; adaptive main-loop sleep ("get
  delay from next scheduled item" TODO in main.c).
- Memory: DelNode freeing DataObjs; property update-in-place instead
  of shadowing; ExecTasks runlist reuse.
- Multi-user isolation and quotas once strangers share a server.
- **Copy has no path of its own.** Every other rendering now follows
  "there is no creation path, only a current path" (aliases are
  renamed live as Container changes - Bridge_Rename, bridge.c) -
  Copy (web/app.js: renderCopy, and the isCopy branches of
  registerCard/registerWidgetAtom) still doesn't fit that model at
  all: it's an extra rendering of the source's alias with no path of
  its own, unconditionally dropped on the top-level canvas regardless
  of where the source currently lives, and never reparented if the
  source later moves. Needs a real decision, not a patch: does a Copy
  track the source's current Container, or does it earn its own
  independent current path (its own place a user can drag it, that
  survives the source moving)? Either answer is a real design choice,
  not a bug fix.
- **Hot reload**: the machinery is already half-built — `_fini` →
  `UnregisterLibrary` and `UnloadClasses` exist, so replacing one
  `.object` in a *running* system (dlclose, copy, dlopen) is a
  finish-the-plumbing job, not a design job. This revives the VNOS
  support model — a fix is one tiny emailed file — and upgrades it:
  no restart. The web palette makes it self-serve: publishing an
  object to a server *is* deployment.

---

## Sequencing logic

Serialization (1.1) unlocks persistence, palette, skins, and protocol
— it is the keystone and comes first. The Intercept revival (2.2) is
the second keystone: without it the browser can render but not
control. HTTP-before-WebSocket (3.1 → 3.2) keeps every step
demonstrable in a plain browser. Composition (Phase 5) comes before
federation (Phase 6) because both lean on the same trick — classes
whose instances are defined by data rather than compiled code — and
composites prove the trick locally before it reaches across the
network. Federation is where the palette becomes open-ended: local C
objects, user-built composites, web APIs, and MCP tools side by side,
indistinguishable on the canvas.

Each phase ends with something you can run and show: 1 — a flow saved
and reloaded; 2 — an object whose properties fire callbacks; 3 — the
framework serving a page that lists live instances; 4 — dragging a
Pulse onto a canvas and watching its LED blink; 5 — that canvas on
the palette as an object.
