# Roadmap: from framework to web canvas

*"Connect the world." This document is that goal's build order.*

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
fan-out, the Enable control plane, scheduler-driven lifecycles
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
   *Landed July 2026, but in the WRONG LAYER — recorded here as debt.
   `ExportView` (object.c) is right: it composes a Serializer → Writer
   flow and lets objects do the work, exactly as this phase intends.
   The inverse did not follow suit — `ImportView`/`LoadViewAsync` put a
   hand-rolled JSON parser (`IJ_Ws`, `IJ_Str`, `ImportNode`), file I/O,
   and import policy INSIDE object.c: ~700 lines with nothing to do
   with the object layer's actual job, which is registration and
   composition (CreateObject, Connect, SndMsg, DeleteInstance). It went
   there because that is where `CreateObject` was in reach, not because
   it belonged. The symmetric answer is a DESERIALIZER OBJECT — Reader
   → Deserializer, the mirror of Serializer → Writer — with object.c
   keeping only the verbs it already owns. Same rule main.c states for
   the host: if a feature is tempting here, it's an object.*

   *The shape to build (July 2026, user's direction), which is this
   phase's own "format is a pluggable decorator" finally taken
   literally: `Serializer` today does TWO jobs — it walks the tree AND
   formats the JSON text. Split them. A **JSON object** and an **XML
   object** each own one format, both directions, and nothing else;
   the walk and the absorb stay format-blind. Export becomes
   `Serializer → JSON → Writer`, import becomes
   `Reader → JSON → Deserializer`, and swapping JSON for XML changes
   the format with no other code touched — which is the decorator this
   phase asked for, made real as objects instead of as a flag. A
   **File object** belongs in this set too; its scope is still open,
   pending the demo objects that define it (do not guess at it — note
   that CLAUDE.md's "reading and writing are two separate classes, not one
   File object with a Mode" documents a DIFFERENT, older question, so
   check before assuming they conflict).*

   *And the XML object is QUERYABLE BY PATH — which is what makes it
   more than a format decorator, and why it is not merely JSON's twin.
   The engine already has exactly one addressing primitive: a path
   resolved against the namespace trie (`ResolvePath`). XPath over a
   document is that same operation against a different backing store,
   so a real XML object is ANOTHER RESOLVER for the vocabulary that
   already exists — joining the local trie and Phase 3's `data://`
   connector as three resolvers under one addressing model, not three
   subsystems. Nothing new is introduced; one more thing simply
   answers "resolve this path."
   The payoff is on import. A text blob has no addressable interior,
   so parsing it is necessarily monolithic — read everything, walk
   everything, construct everything, in one pass (which is exactly the
   shape of the parser now sitting wrongly in object.c). A document
   that answers path queries lets the Deserializer ASK for what it
   needs, which is the only way 3b's lazy mount above is actually
   buildable: "a huge tree loads lazily, one subtree absorbed per
   mount as its container is opened" cannot be built on something you
   must fully materialize first. JSON is a wire format with weak
   interior addressing; XML with XPath is a queryable document store.
   They are not symmetric — treating them as symmetric is how this
   ends up a format FLAG instead of two objects that genuinely differ
   in what they can do.*

   *Layering, same direction: widget creation calls do not belong in
   object.c. Three sites are wrong today — `BuildPalette` (object.c
   500, 538) and `ExportView` (782, 783) both call `Widget_Create`.
   widget.c is already a full layer (17 exports); the object layer's
   job is registration and composition, so those calls move to
   widget.c's side of the line.*

   *The reason both of those landed wrongly, named: **object.c IS the
   de-facto superclass** — the place every object inherits from — and
   with no superclass concept spelled out anywhere, it was the only
   file that looked general enough to hold anything general. So a JSON
   parser and widget construction landed in it alongside create,
   connect, and register.*

   *The test for what belongs: **would a subclass plausibly override
   it?** Create, destroy, connect, disconnect, send, register,
   position, path — yes, those are superclass behavior, and
   `DefaultMessage(superclass, …)` chaining (item 8) is exactly how a
   subclass would reach them. "Go parse this document", "go build that
   widget", "go read that file" — no. Nothing subclasses a service, so
   a service is an instance you send a message to. Applying the test
   is what turns the debt items above from a cleanup list into one
   rule, and it is what keeps the next general-looking thing out.*
4. **Interface publication**: at ClassStart each class registers a
   description of its properties (name, widget
   type, default value). This is the palette's data source and the
   default skin generator.
   *Already present: the "objects publish their own interface"
   improvement note in main.c; class nodes in the registry.*
5. **Addressing**: name/path lookup so anything can be found as
   `Main/Users/jim/Canvas1/Reader1`.
   *Done (July 2026): the ENGINE owns path -> instance on the namespace
   trie (namespace.c, written for exactly this decades early) -
   RegisterPath/UnregisterPath/ResolvePath in O(path length) at any
   session size, PathOfInstance derived from Name + Container and
   verified by resolving back (object.c). The web bridge is re-based on
   it: no per-bridge alias tables, so every translator - bridge, script
   hosts, the future MCP server - resolves the same names against the
   same index, and instances created over one wire are addressable from
   every other (the raw TCP surface and the GUI now genuinely share one
   namespace). Retired names actually reclaim their keys (NSDelete
   rewritten: the old prune freed chains still shared by sibling keys).
   Still to come on top of it: MoveBranch - the engine-level prefix
   re-base (today's Bridge_RepathSubtree pushed down), which 3b's
   subtree mount then reuses.*

## Phase 2 — Objects grow skins

The structure is already in reader.c's `SetSubProp()`: each
user-facing property carries sub-properties — `graphics` (widget
type), `OnChange` (callback), `local`. Make that the standard.

No special categories, anywhere in this phase: not "handlers vs
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
   *Finished (2026-08-11): that fan-out is now QUEUED - FanOutSubscribers
   envelopes the write onto the scheduler (SndMsgNode) and DispatchMsg
   delivers from ExecTasks, so no subscriber's handler runs inside the
   setter's call stack. And the last condition on it is gone: SetProp*
   no longer compares the new value to the old one, so a write is an
   event and a repeated value is a repeated event. Between them these
   retire the reason a Button had to manufacture a falling edge. This
   is also what the Phase 2 keystone (the "Intercept revival", see
   Sequencing logic) turned out to mean - Intercept was never revived;
   OnMsg plus unconditional fan-out replaced it, and queuing finished
   the job. See "Found 2026-08-11" below.*
3. **`Connect()` reaches any property — retiring bind-property/
   bind-activate as a separate mechanism.** `ConnectToProperty`/
   `ConnectToActivate` (object.c) and the Bridge's bind-property/
   bind-activate commands exist purely because Connect() only works
   today when the target already has a compiled-in OnMsg handler on a
   named property — a plain data property has none, so wiring a widget's
   Value into an arbitrary property needed a bare adapter node
   standing in as a translator. Give every property a *default*
   handler ("store whatever arrives") and Connect() itself works
   uniformly against any property name on any instance. A property like
   Enable or In still installs its own handler when it needs real
   logic beyond storing a value, and that handler still wins — but
   that is an override on a universal default, not the only way
   anything gets wired. Plain Connect() was always the right verb; it
   just was not universal yet. The adapter node type and the two extra
   Bridge commands retire once this lands.
   *Done (July 2026): a Subscriber records {Instance, Port, Callback};
   delivery with no Callback applies the universal default - store what
   arrived (DeliverToSubscriber, node.c, shared by both fan-out
   walkers). Activate is an ordinary property (ActivateOnMsg stamped by
   RegisterInstance). The adapters are deleted; bind-property/
   bind-activate survive only as bridge dispatch synonyms for connect
   so recorded flows replay. Because the record names the REAL target,
   list-connections, CloneConnections, Disconnect and the delete scrub
   all read the same graph - no adapter special-casing anywhere.
   Proven raw-first in testharness/connectiontest.py.*
4. **View: the container primitive.** Everything past this point
   depends on View existing as a real class, not a special case. A
   View is a first-class object on the palette exactly like LED or
   Button — the one thing that makes it a View is what it *holds*: a
   table of child slots, each recording `{instance, X, Y, Width,
   Height}` — the panel-builder shape the demo objects
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
   its children's properties are aliased as *its own* In and
   Out — this is what makes a fully-composed View (a Slider wired to a
   VU meter, say) usable as one black-box unit from outside, without
   whatever is wiring to it needing to know what's inside. This is
   Phase 5's "container properties" idea, concretely mechanized. It is a
   convenience, not a requirement — every property of every child
   inside a View stays individually wireable with or without this
   curation, because nothing needed an opt-in to be connectable in the
   first place (Phase 2.2).
   *Done: `objects/view` is an ordinary palette class; Views nest
   (`Widget_SubPanel`); containment is a `Container` property, not tree
   reparenting, exactly as described. Icon/open is `ReservedViewOpen`,
   with `ReservedViewPanelX/Y` carrying the panel's own position.
   Aliasing a child's property as the View's own In/Out shipped as
   `LinkPropertyAs` / bind-port, and `testharness/widgettest.py` builds
   a composed View and drives it from outside as one unit. The root
   itself became a real View (`CreateRoot`) - no fabricated /Root
   prefix, no chrome category.*
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
   *Done: every object carries a `WidgetItem` table and builds its panel
   by composing a View (`Widget_BuildTable`, objects/widget) - 56 modules
   at the last count. Built once, then it simply exists; opening it is
   observation. The same table publishes the class Interface
   (`Widget_Publish`), so one declaration serves both.*
6. **Every primitive owns its own presentation.** No shared generic
   fallback rendering across widget types — a VU meter must not
   degrade to the same plain text readout a Label uses. Each widget
   class gets code that genuinely renders what it is. Short term this
   is a real per-class function in the browser client (Phase 4); the
   longer-run version is Phase 6/7's federation idea turned inward — a
   widget class eventually ships (or points to) its own rendering, the
   same way it will eventually ship its own script-defined behavior.
   *STILL OPEN - and now the main thing holding Phase 2 back. The short
   term half landed: each class does render as itself. But it landed as
   per-class code IN the client, which is the two-place problem - a
   control is defined in its `.c` and again in `web/app.js`, and
   forgetting the second half renders a textbox with no error anywhere.
   The longer-run version is written up as "Presentation belongs to the
   control" below, with delivery decided (each translator builds its own
   blob at instance start) and the update contract identified as the
   part that decides whether the ladder comes back.*
7. **Every object ships its README, and Help renders it.** Each
   object module directory carries a README.md — what the object is,
   its properties, how to wire it — loaded at class
   registration onto the class node (documentation is engine state
   riding the class, exactly like UUID/Company provenance, and it
   travels wherever the class does). Alongside it, a **Markdown
   presentation control**: one more widget class that renders markdown
   handed to its In/Value — useful for any rich text in a panel, not
   just help. Then a **Help button on every widget panel** is just
   composition: the button opens a View holding a Markdown control fed
   the class's README — no dedicated help subsystem, no client-side
   help text, same one-panel mechanism as everything else.
   **Dual use: the same help feeds tooltips.** The per-property lines
   of that documentation land as annotations on the published
   Interface entries (properties are nodes, so annotating one is the
   existing mechanism), and every panel row and control renders its entry's line as
   its tooltip — hover any control and its one-line doc is right
   there, click Help and the full README opens. One source on the
   class, two presentations; the tooltip text is never written a
   second time, and a class whose README documents its properties has
   tooltips everywhere its controls appear — rows, atoms, aliases —
   with zero extra authoring.
   *Partly done: every object directory carries a README, `Widget_AddHelp`
   puts a Help sub-view bottom-left on every panel, and a Markdown
   control renders it. The TOOLTIP half is not done - per-property doc
   lines annotating the published Interface entries. Tooltips today show
   the alias path, not the property's documentation.*
8. **A Widget base class — subclassing starts where the sameness
   is.** Every widget panel now carries the same growing set of things
   — Enable, the State LED, Activate, position/PanelX/PanelY, and now
   Help — and each class re-declares them by hand in its own
   ClassStart/InstanceStart. That repetition is the signal to mechanize
   object subclassing (message chaining to a superclass, long promised):
   a Widget base class publishes and
   handles the common face once, individual widget classes subclass it
   and add only what makes them themselves, and every widget panel
   gets the common controls in consistent places for free — consistent
   because they are literally the same inherited declarations, not
   because each author copied them carefully. Function pointers live
   in node properties, so a subclass is a class node whose unhandled
   messages and unpublished properties resolve through its parent
   class — and dependency-ordered class loading (Phase 8's note, and
   main.c's original two-phase intent) is what lets a subclass load
   after the base it names.
   *Done: a class names its parent (`SetClassParent`), an unhandled
   message falls through to it on `rtrn_dropped`, and `Object` ends the
   chain. `Widget` owns Help and the panel conventions, and `Control` is
   what a control descends from - the client asks an instance's
   `classParent` rather than consulting a hardcoded list of the fifteen
   control class names, so a control written tomorrow renders today.
   The dependency-ordered LOADING half is still open (Phase 8): a
   subclass whose base has not loaded yet is not handled.*

## Phase 3 — The wire: protocol and server

1. **HTTP object** on top of the TCP object: enough to serve the
   static web app files (Reader → HTTP → browser: the cat flow grows
   up). Dogfood: the framework serves its own UI.
   **Next (July 2026, user's own idea): prove this as a general,
   standalone TWO-WIDGET composition, not just internal web-serving
   plumbing** - a TCP server widget wired to an HTTP protocol widget,
   the two of them coordinating around a specific connection rather
   than the framework's own boot flow gluing them together invisibly.
   The `Conn` tagging is already real (`objects/http/http.c`: every
   request arrives tagged with a connection id, every response carries
   the SAME tag back out) - the concrete next step is demonstrating and
   testing it as its own thing: drop a TCP (server mode) and an HTTP
   object on a canvas, wire them to each other, and prove multiple
   simultaneous connections each get correctly isolated per-connection
   state (partial-request buffering/reassembly keyed by `Conn`, not
   just single-message tagging) - "both mutexing internally around
   that connection," so one slow or partial connection can never bleed
   into another's parsing state.
2. **WebSocket object**: handshake + framing over the TCP object.
   *Already present: TCP server, buff, the subscription pattern.*
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
   to any property, streaming over the socket. The instrument
   panel is a bundle of taps.
5. **Sessions and login**: users as nodes (`Main/Users/<name>`), each
   with canvas containers; token auth first, TLS later.
   *Already present: objects/network/testkey/ certs.*
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

   *Combine with a `data://` scheme (July 2026 idea) - the same
   nesting move, one more hop outward. `/User/Root/...` answers "which
   user's tree, on this server"; `data://host:port/path/instance/
   property` answers "which server's tree at all," using the exact
   same path vocabulary underneath, just prefixed with where to find
   the tree instead of assuming it's this process's own. A property
   address doesn't stop being a property address because a network
   sits under it - it's the same "resolve a path" primitive both
   times, only the resolver differs (the local trie vs. a connector).
   This is also the natural convergence point for Phase 6's federated
   connectors (MCP client View, web API wrapper classes, the eventual
   MCP server View): each currently has to be its own bespoke
   translator because there's no single uniform way to NAME a remote
   property; a `data://` address would make "where a value lives" the
   same kind of fact whether local or remote, and every Phase 6
   connector a resolver for one scheme instead of a separate
   subsystem. Not yet coded, no design commitment yet on the wire
   format for the resolver hop - noted here as where the user-nesting
   work and the federation work are really the same idea at different
   radii, so they should be designed together, not twice.*
6. **Lifecycle events: the engine announces, translators project**
   (July 2026 — designed, not built). Today `instance-created` is
   hand-written into six bridge command handlers, each sitting a line
   or two after that handler's own `RegisterPath` call (`Bridge_Create`:
   bridge.c 479 → 481). But `RegisterPath` is called from THIRTEEN
   sites across four files — `Widget_Create`, `ImportCreate`,
   `ImportAliasesPass`, `BuildPalette`, `BuildChrome`, `CreateRoot`,
   main.c — so any instance born by a route other than a bridge command
   is born silently and no connected client ever hears about it. That
   is why load and import left the GUI stale, papered over with a page
   reload in app.js's `flow-loaded` handler — which works only because
   a reload re-runs `list-instances` and re-derives everything from the
   engine, i.e. the engine was right and only the notification was
   missing.
   The fix is one emission inside `RegisterPath` and one inside
   `UnregisterPath`. That is the true funnel — it catches even clone,
   which bypasses `CreateObject` entirely and calls `instanceStart`
   directly — and it is exactly the moment an instance becomes
   addressable, which is the only moment the outside world can
   meaningfully be told about it. The core cannot call into a loadable
   object, so the emission rides the message fabric rather than a
   direct call: a well-known node carries `Created`/`Removed` properties,
   and the Bridge `Connect()`s itself to them at InstanceStart like any
   other object. Scoping is unchanged — `Bridge_SendEventScoped`
   already filters by container against `connViews`.
   What this buys: `Bridge_Create` stops announcing by hand;
   import, load, palette build, clone, and a script creating an object
   all announce for free; the reload hack and any post-hoc subtree walk
   are deleted rather than fixed. It also closes a real gap — the
   bridge frees its taps before deleting (`Bridge_FreeTaps`) but the
   engine's own `DestroyContents` does not and cannot, so a `Removed`
   event is how the bridge cleans up its own bookkeeping without the
   engine needing to know bridge internals.
   *One open design point: rename currently re-keys as `UnregisterPath`
   + `RegisterPath` (bridge.c 1362, 1448), which would read as
   remove+create. It wants its own engine verb (`RepathInstance`)
   emitting `Renamed` — one verb per gesture, same as everywhere else.*

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
   *Done: `create-instance` and `clone-instance` both carry
   class/container/x/y, so birth is atomic - named in its container at
   its position, never born at root and moved. Dropping onto an open
   View places it in that View. Exercised by testharness/viewclonetest.py.*
3. **Wiring**: Connect()/SndMsg, exactly the Reader.Out → Writer.In
   wiring already built, now reaching every property once Phase 2.3
   lands, not just ones with handlers — driven by Connect mode's two-click
   source/destination gesture (Phase 4.6), not a dedicated dot
   drag.
   *Done: connect/disconnect over the protocol, reaching every property
   (Phase 2.3), with the two-click source/destination gesture in Connect
   mode. `list-connections` reads the same records the graph walkers do.
   Exercised by testharness/connectiontest.py.*
4. **Opening a settings panel**: a composite object's control panel is
   just its associated View (Phase 2.5) — "open settings" means
   subscribe to and render that View's existing contents, the same code
   path as opening any other View, including one a user hand-built from
   scratch. There is no separate "default skin" mechanism running in
   parallel to maintain.
   *Done: a panel is its View, opened by `ReservedViewOpen` with
   `ReservedViewPanelX/Y` for its own position - the same path as opening
   any hand-built View. No parallel default-skin mechanism was ever
   written.*
5. **Widgets render themselves distinctly** (Phase 2.6): LED, slider,
   VU meter, text output, button, checkbox, textbox — each its own
   rendering, never a shared fallback.
   *Done, with the Phase 2.6 caveat: each class does render as itself,
   but the rendering is per-class code IN the client rather than shipped
   by the class. See Phase 2.6.*
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
       is read vs written — the class Interface already knows this)
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
   *Done: Operate is the resting mode, with the other modes reached the
   way described. `effectiveMode()` and the `mode-*` body classes drive
   it, and the Operate panel and the Options panel stay distinct.*
7. **Dynamic per-control styling: the bridge translates properties
   into CSS.** A control's look is engine state, not stylesheet code:
   an object (or an alias — presentation is the alias's own) declares
   its appearance as ordinary properties, and the BRIDGE dynamically
   generates each control's custom CSS by translating those properties
   — the client applies what it's handed and decides nothing (the same
   law as engine-stamped `Widget`). The pixel `W`/`H` a widget's own
   controls carry are the
   seed of this, currently translated client-side; the general
   mechanism moves that translation into the bridge and opens it to
   every styling fact — size, color, font, geometry, background
   image. Because properties are nodes, any control can carry any
   styling property without new record types, a styling write restyles
   the control live through the ordinary property-changed fan-out, and
   skins ride flows: save/clone/alias a panel and its looks travel
   with it, because they were never anywhere but on the objects. The
   hand-maintained per-widget stylesheet shrinks to a base theme;
   custom looks stop being client code entirely.

   *Chunk 1 (the read-only audit) has a concrete answer as of
   2026-08-12, measured in a live browser: of 22 rendered controls, 14
   took a declared pixel size and 8 took none. The reason is a single
   gate - `bindLiveControl` registers the `W`/`H` subscription only when
   `widgetClass === 'Textbox'`, so a Textbox is a pixel box and nothing
   else is. Every other control falls back to whatever the browser
   chooses, which is invisible for as long as a control has a sensible
   intrinsic size and becomes a hairline the moment one does not (an
   empty `<button>`). So the visual fact IS property-backed already -
   the `W`/`H` are on the instances, correct, and unread. Widening that
   gate is the smallest possible first chunk, and worth doing before the
   translation moves into the bridge, because it says whether the
   declared sizes in the widget tables were ever tuned.*

   **Broken into small, sequential chunks (July 2026) - aim at this
   over time, not in one sweep, and look for chunks that ride on other
   work already happening rather than needing a dedicated push:**
   1. *Audit, read-only, zero risk*: inventory which current visual
      facts already have SOME property backing (LabelPos -> the
      `atom-label-*` class swap; a Textbox's W/H -> its own box
      sizing) versus which are still 100% CSS-class-driven with no
      property behind them at all (color, font, border, background).
      Answers "how much of this is already true" before changing
      anything.
   2. *Move a widget's own W/H translation into the bridge* - already
      explicitly the seed case above, already client-side today, so
      this is the smallest real step: one existing client-side
      translation relocated, not a new mechanism invented.
      Proves the bridge-side translation path once, on something that
      already works end to end.
   3. *One new visual fact, on whatever widget is already being built
      for other reasons* - the natural combination point: a
      dynamically-generated widget (the MCPSource agent-view pattern -
      already sets per-instance W/H/X/Y at runtime with zero compile-
      time table) is the cheapest possible testbed for "one more
      per-instance property, honored by the bridge as real CSS,"
      because the runtime-property-setting path is already exercised
      code, not new plumbing. Prove ONE fact (a single color property,
      say) rides that same path before widening to more.
   4. *Widen fact-by-fact* (font, border, background) as normal widget
      work touches each one, not as a dedicated sweep across every
      existing widget at once.
   5. *Retire the hand-maintained per-widget stylesheet last*, once
      enough facts have moved over that what's left is genuinely just
      a base theme, not per-widget appearance - a natural end state to
      notice, not a deadline to force.

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

1. **Container properties**: a container publishes named properties that
   alias properties of inner instances; a write to a container property routes
   inward. One new subscription-record type.
   *Done (July 2026): NOT a new record type - the existing link/alias
   mechanism IS container properties. `bind-port` (bridge command ->
   LinkPropertyAs, object.c) makes a container's OWN property a transparent
   link to a child's property, so wiring to/from it resolves
   through to the child (ResolvePort). A View.In bound to an inner input
   control's In, a View.Out bound to an inner output control's Value, and
   the View is a black-box widget the outside wires to without knowing
   its insides. Combined with a script inside puppeting the controls
   (the language-host-as-bridge-client, addressing siblings by path),
   this is a SCRIPTED COMPOSITE WIDGET - a View that behaves like a
   compiled widget but whose logic is editable script.
   testharness/widgettest.py builds and proves one over the protocol
   (input -> container property -> inner control -> script -> output control
   -> container property -> outside). This is the concrete Phase 5 + Phase 7
   convergence.*
   **Next (July 2026, from the MCPSource agent-widget work): a real test
   suite around save, load, export, and import of scripted composite
   widgets, then work through whatever it finds.** Confirmed working so
   far: build, Submit round-trip, and clone (see
   `objects/mcpsource/mcpsource.c` and its own `MCPAgent` class + Lua
   `sibget`/`sibset` fix) - but session save/load and single-view
   export/import of a View containing a script instance that reaches
   siblings by path have not been exercised at all yet. Concrete risks
   worth a test each: does a saved-and-reloaded script's `Source`
   survive and re-activate; does `sibget`/`sibset`'s relative path
   resolution still find the right sibling after export (single-view
   export stores internal links relative, no `/Root` prefix) and after
   import (a clone-drop into the target view); does a script-containing
   View round-trip through save/load with its Runner correctly NOT
   re-activating twice.
   *Status 2026-08-12: THE SUITE EXISTS and it is red.
   `testharness/scriptedwidgettest.py` covers exactly the four risks
   named above - builds-and-runs, clone, export/import, save/load - plus
   an alias case, and `testharness/widgettest.py` covers the composed
   View driving an outside sink. All eight fail identically across every
   build variant (debug/release/asan/ubsan/gcov), with 0 crashes, 0
   leaks and 0 sanitizer findings, so this is a logic failure and a
   deterministic one. Every observation is the same shape: the script's
   output never arrives - `OutBox=None`, `Output=`, `sink=`, `Out
   values: []`. That is one fault, not eight, and it sits at whichever
   end of a script crosses the reads/writes that changed on 2026-08-11.
   The suite was the right thing to write; it is now the thing to make
   green, and it is the honest gate on calling Phase 5.1 finished.*

**Also found during the MCPSource work, not yet fixed - a general
notification gap:** an instance created or moved server-side (not in
direct response to a client's own `create-instance`/`clone-instance`/
`move-instance` command - e.g. MCPSource building agent widgets in
reaction to a TCP reply) never gets announced to an already-connected
browser. Every call to `Bridge_InstanceEvent` (bridge.c) traces back to
an explicit client command; there is no general "something was placed,
tell whoever's watching its container" hook. A full page reload works
around it today only because reload re-runs `list-instances` from
scratch. The right fix is general, not MCPSource-specific: hook it at
`PlaceInstance` (object.c) - already the one shared choke point behind
create, clone, AND move - so it covers all three at once. Shape:
`PlaceInstance` calls an optional, registered notify callback (a global
function-pointer slot in the shared library, the same pattern
`ObjSetRegObjList`/`ObjSetTaskList` already use) that bridge.c sets
once at load time; bridge.c's own implementation walks its live
connections' viewing records and sends the instance-created event to
whichever ones are watching the affected container. Touches both
object.c and bridge.c - a real core change, deliberately deferred
rather than rushed.
2. **Composite classes**: register a saved flow file in the registry
   as a class — its InstanceStart loads and wires the inner flow.
   Composites appear on the palette beside the C classes,
   indistinguishable to the user.
   *Open. The pieces exist - a View can be a black-box unit (5.1), a
   flow serializes and reloads (1.3a), and `import-flow` drops one into
   a container - but nothing registers a flow file AS a class, so a
   composite cannot appear on the palette beside the C classes. This is
   the item that turns "a thing you built" into "a thing you can use",
   and it is the largest single piece of Phase 5 still missing.*
3. **Nesting and versioning**: composites inside composites; every
   library already carries a UUID, composites get one too.
4. **Kiosk save: a flow that ships as the whole app.** Arrange the
   session the way it should ship - one panel at the front, no
   palette, no menus, everything wired to it - and save. Loading
   that flow gives you exactly that and nothing else: the app runs,
   there is no chrome to wander into, and every input goes to the
   panel.

   *There is nothing to build. The mechanism is already exact:* the
   menus and the palette are ordinary instances in `/Root`
   (`BuildPalette`/`BuildChrome` only bootstrap them on a first run
   with nothing saved), a save records `/Root` including or excluding
   them according to what is there, and a load destroys `/Root`'s
   contents and restores the file. Delete the chrome, save, and the
   file IS the kiosk. This is "the app is an empty view" arriving:
   shipping a different product is a different flow, never a
   different binary.

   *Corollary - do not protect anything from the load's destroy.* A
   guard that keeps the menus alive (say, honouring `Deletable="0"`
   in DestroyContentsAsync) makes a kiosk impossible, and the
   "you can no longer reach File -> Load" worry is answered
   out-of-band instead: the raw bridge lives in `/Main`, outside the
   canvas, so a load of `/Root` never touches it and a kiosked
   session is still drivable over the protocol.

   *Worth having, not as a guard but as a fact:* when a load's
   destroy removes an instance carrying `Deletable="0"`, log it. An
   intentional kiosk and an accidental one look identical on screen
   (an empty canvas), and only one of them is a surprise.

5. **Clone becomes serialize + deserialize - one path, not two.**
   Clone and import are the same operation: take a subtree, reproduce
   it elsewhere, rewire its internal links onto the copies and leave
   its external ones alone. Export already solves relative addressing,
   wires and alias targets; `CloneObject` / `CloneConnections` /
   `CloneAliasNode` / `CloneMintName` / `CloneGroupPass` solve them a
   second time, separately.

   *This is not hypothetical divergence - the two paths HAD diverged.*
   Every save/load/clone bug chased on 2026-08-01 existed twice for
   this reason (the alias binding to its own slider, the wire landing
   on the copy's members, names minting fresh), and each was fixed on
   one side without the other. Two copies of one thing always drift;
   the harness's own `Report` class, copy-pasted into guitest.py and
   rawtest.py, quietly stopped printing progress dots in one of them.
   **If two paths must agree, they have to be one path** - not kept in
   sync, not documented as parallel.

   *What it needs, both already listed rather than new:* a
   **Deserializer** object so the reading half stops being a hand-rolled
   parser in object.c (the mirror of Serializer -> Writer), and a
   memory sink instead of a Writer so a clone serializes to a buffer
   rather than a file - a wiring change, not a mechanism.

   *Prerequisite, made concrete by the same day's testing:*
   `CloneInstance` hands the bridge a map of source -> copy, which is
   how a clone announces what it made. `ImportView` returns only the
   top, which is exactly why import announces nothing and every test
   waiting on `instance-created` after an import saw nothing. If clone
   becomes import, import must announce - item 1 above blocks this one.

   *What it retires:* most of the clone machinery, several hundred
   lines whose only job is doing what the serializer already does.

6. **The widget table becomes a .flow file.** A `WidgetItem[]` table is
   a flow written in C: class, name, position, size, and which handler
   each control's writes reach. Every row is a create-instance plus a
   connect plus a few property writes - which is a flow file, and
   `Widget_Build`/`Widget_Ctl`/`Widget_Publish` become "load this file"
   (`ImportView`, already written).

   *The one thing the table can express that a file cannot is a
   COMPILED handler* - and that is what the language hosts are for. A
   behaviour that is a script inside the flow needs no `make` at all,
   which is the scripted composite widget already proven in
   widgettest.py, applied to the widget's own panel instead of to a
   View built by hand. So the split is: C for what genuinely must be
   compiled - sockets, the fabric, a codec - and a flow file for
   arrangement plus scripted behaviour.

   *Why it is worth doing rather than merely elegant:* a bug in a
   widget's layout or logic stops being rebuild-and-restart and becomes
   editing a file - or editing the widget in the GUI and saving it.
   Support also gains a second form of "email someone the fix": not
   only a 15KB .object, but a widget as a file.

7. **Push to definition - re-skinning, and ONLY re-skinning.** Rearrange
   a widget instance in the GUI, then push that back to the class's flow
   file so every future instance is built that way. Same verb again:
   export the instance's view, write it as the class's definition.

   *The cut that makes this safe: layout only.* `X`, `Y`, `W`, `H` and
   the panel's own coordinates are visual; values, wires and handlers
   are not, and are never pushed. Nothing a re-skin can do will break an
   instance, so **live instances can follow immediately** - no version
   negotiation, no reload, no data migration, no "does this instance
   still work" question. It is the same as dragging a control inside a
   panel today, which already has no consequence.

   *This only works because size is pixels on the instance.* Layout is
   entirely ordinary properties, so "just the visual" is a nameable
   subset rather than a judgement call, and the whole operation is three
   property writes per control with no lifecycle involved. Had size
   stayed baked into the C table, "push only the layout" would not even
   be expressible - you would be pushing a recompile.

   *Decide deliberately, cheap now and expensive after ten widgets are
   files:* whether a gesture is editing THIS instance or the class it
   came from should be visible in the GUI, not implied - you may have
   moved one control because this one instance needed it.

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
   *Partly: there is no generic HTTP CLIENT object. What exists is HTTP
   client behaviour built into the objects that needed it - Ollama,
   ComfyUI, StableDiffusion, TPLink each speak their own protocol over
   TCP. That works and is how the capability got proven, but it is the
   thing this item exists to stop: the fifth wrapper should compose a
   client, not re-implement one. The async-dns module is written and
   still unwired, which is the other half of this.*
2. **Web API wrapper classes**: generated from OpenAPI/REST
   descriptions — each endpoint a palette object with typed input
   properties and a response Out property. Webhook receiver object for
   the inbound direction (a route on the HTTP server → an Out property).
   *Open, and the distinction is worth keeping sharp: several
   hand-written wrappers exist (Ollama, ComfyUI, StableDiffusion), but
   nothing GENERATES a class from a description. Hand-written wrappers
   are objects; generated ones are the federation idea. No webhook
   receiver object either.*
3. **The MCP client View**: a View that, pointed at an MCP server
   (stdio transport first — an Exec object holding a subprocess with
   its stdio as In/Out properties is Reader/Writer-shaped; HTTP transport
   once 6.1 lands), calls tools/list and fills ITSELF with the served
   interface: each tool registers as a class (the tool's input schema
   becomes properties with stamped widget types, results flow
   out Out), and the View holds a live, wireable instance of each.
   You get the widgets of THEIR served interfaces on YOUR canvas -
   rendered, paneled and connectable with zero client changes, since
   a class was always just published-interface data. Dragging
   "search" out of that View drops a live remote tool into a
   dataflow. MCP resources map to reader-like sources, prompts to
   template objects. The translator is bridge.c's shape: syntax
   only, over the same engine calls.
   *Done, and it is the proof the federation idea works.
   `objects/mcpsource` points at an MCP server, and each agent it
   discovers becomes a generated View with controls for the agent's
   inputs, a Submit, and an output - rendered, paneled and connectable
   with no client changes, because a class was always published-interface
   data. Build, Submit round-trip, and clone are confirmed; save/load and
   export/import are covered by the tests that are currently red (see
   5.1). The generated views are driven by a script reaching its
   siblings by path, which is the Phase 5 + Phase 7 convergence arriving
   here too. Transport is TCP to a local bridge rather than stdio; an
   Exec object holding a subprocess is still unwritten.*
4. **The MCP server View** (the symmetric direction): a View that
   SERVES whatever is dropped into it. Its members are known (the
   Container relationship), their interfaces are already published
   nodes (GetClassInterface - what the bridge ships inline today),
   so tools/list is a member walk translated to schemas and
   tools/call translates onto SetOrDeliverProp / ActivateInstance /
   SndMsg - the same verbs every translator uses. The View boundary
   is the security model for free: you serve exactly what you
   dropped in, nothing else - the same "you only hear about
   containers you opened" rule the bridge already enforces. Published
   flows become MCP tools, so LLM agents can invoke user-built
   dataflows - and, given create/connect commands from the Phase 3
   bridge, build flows themselves. The canvas becomes something an
   agent and a human can edit together. Two frameworks each serving
   a View and consuming the other's is federation whole: a wire
   whose middle hop is a tool call - the same cross-instance IPC the
   multi-user architecture uses, wearing a standard protocol.
   *The STRUCTURE already exists - MCPSource builds it. Pointing it at a
   server fills a View with the agents it found, each a member with a
   published interface, controls and a Submit. That is exactly the thing
   `tools/list` enumerates: 6.3 and 6.4 are not two mechanisms, they are
   the same View read in opposite directions. Consuming walks a remote
   list and makes members; serving walks members and makes a list.
   What is missing is only the answering half - something that responds
   to tools/list with a member walk and to tools/call with
   SetOrDeliverProp / ActivateInstance / SndMsg, the same three verbs
   every other translator already uses. Per the surfaces work below that
   is a translator object reading `presentation/mcp` and speaking
   get/set plus verbs, not a subsystem. And because the container
   boundary is the security model, you serve exactly what you dropped
   in - which MCPSource's own View demonstrates from the other side.*
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
   *Done (July 2026): TWO hosts — Lua (`objects/script`) and QuickJS/
   JavaScript (`objects/jsscript`, QuickJS vendored with libbf) — built
   on the "language host is a BRIDGE CLIENT" shape (the user's framing:
   the web bridge is the pattern). Each has In/Out/Print properties plus
   Cmd/Evt: wire Cmd→Bridge.In and Bridge.Out→Evt and a script speaks
   the full JSON protocol, a peer of the browser, no diminished API.
   ScriptHost=1 marks a class as a language for runtime discovery; a
   mandatory runaway guard (QuickJS interrupt budget) keeps a script
   from freezing the single-threaded fabric (Lua's guard is still TODO).
   THREE hosts as of 2026-08-11: Forth on atlast joined them
   (`objects/forth`), which is what proved the shape is not
   Lua-and-things-like-Lua - a stack language with no expression syntax
   binds through the same verb table. Its own conventions are recorded
   in the reference notes: flag-byte plus upper-case primitive names,
   C-style string literals, compile once and exec the entry word, and
   verbs push nothing when they return nothing.
   The ScriptBox shell (`objects/scriptbox`) is the script WIDGET —
   a Language dropdown (discovered hosts), a Source box and an Output
   box (PROP_TEXTAREA, `objects/textarea`), Run=Activate; it contains
   and drives an inner host, swapping language by CreateObject+Connect
   (Lua works as an inner language with ZERO changes to script.c). Next:
   formalize the verb table (7.1) so every host binds ONE language-
   neutral surface, and Python via an out-of-process Exec object rather
   than embedding its weight.*
3. **Script-defined classes**: scripts register real classes on the
   palette — indistinguishable from C classes, same as composites
   (Phase 5) and federated tools (Phase 6). Third form of the same
   trick: classes defined by data.
4. **Overrides**: the revived Intercept path (Phase 2.2) doubles as
   the override hook — attach a script to any property of a
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
- **Source enumeration — reading all of a property's inputs.**
  Today `Connect` records the subscription on the **source** property (a
  `Subscriber` sub-node naming `{Instance, Port, Callback}`): a forward,
  forward index that answers "who do I feed?" but not the reverse.
  A handler is delivered one value at a time and cannot ask "who is
  wired into my In, and what does each hold right now?" An object needs
  exactly that to *combine* its inputs. Without it a whole family of objects
  can't be built honestly: **any N-input combinational object** — LogicGate
  (OR/AND/XOR over N wires), Comparator, Summer/adder, Mux, averager,
  majority-voter. Each only ever sees a single arriving value with no way
  to hold the combined picture, so it either tries to track sources itself
  (impossible — a delivery doesn't identify which source it came from) or
  degrades to single-input. `objects/logicgate` degrades exactly this way
  on purpose today, and its header comment is the placeholder for this
  entry: as a single-input OR+Invert it is a working NOT gate (it inverts a
  Pulse for the duty-cycle bench — a PulseGenerator feeding two Stopwatches,
  one through the gate, reads the on and off phases at once), but true
  multi-input OR/AND/XOR waits on this primitive.
  **The machinery is already half-present.** `ScrubRegistrySubscriptions`
  (object.c) already walks every `Subscriber` record registry-wide,
  recursively through sub-properties, looking for ones that point at a
  given dying instance — source enumeration is that *same traversal*,
  collecting instead of deleting and keyed on a target `{instance, port}`
  rather than a whole instance. So the primitive is a `SourcesOf(instance,
  port)` enumerator in object.c that yields the wired source `{instance,
  port}` list (read each source's current value with `GetProp*`), backed
  either by that on-demand registry walk or, if it gets hot, a live reverse
  index maintained by `Connect`/`Disconnect` alongside the forward one.
  This closes the last gap between "a wire is a subscription" and "an object
  can reason about all its wires," and it is what turns the combinational
  objects from single-input stand-ins into the real thing.
- TCP client mode and multi-connection (the ring pattern and the
  connecting state machine).
  Client mode brings `async-dns/` into the build: hostname resolution
  without ever blocking the fabric (worker thread + sentinel flag,
  results delivered as main-loop callbacks).
- Object self-tests registered through the registry so `-t`
  exercises every loaded object (the long-standing testing roadmap).
- Scheduler: implement RemoveTask; adaptive main-loop sleep ("get
  delay from next scheduled item" TODO in main.c).
- Memory: DelNode freeing DataObjs; property update-in-place instead
  of shadowing; ExecTasks runlist reuse.
  *Accounting landed (July 2026): the core counts every allocation
  behind getters (NodeCount & co.), the Stats object publishes them as
  ordinary watchable properties, and testharness/leaktest.py asserts
  create/destroy and message-burst cycles net zero - which found and
  fixed the per-re-activation task_entry leak (five objects) and wired
  in Bridge_CompactFlow (the flow log's never-called GC). The message
  path itself proved leak-free: SndMsg's ownership contract holds.*
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
  finish-the-plumbing job, not a design job. That makes the support
  model a fix in one tiny emailed file, and upgrades it:
  no restart. The web palette makes it self-serve: publishing an
  object to a server *is* deployment.
- **The client is told once per frame, not once per write.** Every
  property write currently becomes its own event: an envelope, a
  dispatch, a WebSocket frame. A dragged slider therefore costs a
  round of all three per intermediate value, and the browser throws
  nearly all of them away — it cannot show more than one state per
  frame. The fix is a per-connection **dirty set** in the bridge
  (property → latest value) flushed by the bridge's own task at ~60Hz.
  Coalescing falls out: ten writes between flushes cost one send
  carrying the last value, and the cost stops scaling with how fast
  something changes. So does visibility — a property on a closed
  panel, an unopened view or a collapsed sub-view simply never enters
  the set, so nothing is spent describing what nobody can see. The
  core is untouched: `SndMsg` keeps its semantics, the bridge stops
  forwarding everything it hears. This is also what retires the 1ms
  sleep cap in `MainLoop`: that cap exists because polled work never
  declares its own cadence, and the flush task is exactly such a
  declaration — 16ms while a session is attached, nothing at all when
  the last client detaches, so an idle instance stops burning a
  thousand wakeups a second and a process-per-tenant deployment costs
  what its footprint suggests.

- **A widget's client half ships with its class.** Today a Textbox is
  defined twice: `objects/textbox/textbox.c` and a branch in
  `web/app.js`. So adding a widget means editing the host, which is
  the one remaining place where shipping a new object is not enough —
  the same debt as `BuildPalette()` above, on the browser side. The
  renderer belongs to the class that needs it, carried as an ordinary
  property on the class node so a single `.object` file stays
  self-contained, and served by the bridge on request like any other
  property. Then a view is served only the renderers for the classes
  actually in it (and only the CSS those widget types need), derived
  from the view's contents at serve time — the bridge already knows
  both. Two consequences worth the work: `app.js` stops being a switch
  over every widget type and becomes a loader, and the emailed-fix
  deployment story finally covers the whole widget, GUI included —
  drop the file in the scan path and its browser half arrives with it.
  The costs are real but small: the client loads renderers on demand
  and caches them per class, and a class node carries a few KB of text
  the engine never reads.

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

---

## Found 2026-08-11 — the day writes became messages

The session that queued property fan-out through the scheduler and took the
on-change gate out of `SetProp*`. Both landed; the engine runs quiet with them.
What follows is what that work turned up and did not fix.

### The rule that came out of it

**A control never owns data. It points at the property it shows.** A Checkbox
in a panel and a Checkbox on a canvas are the same thing pointing at different
properties. "Alias" is that RELATIONSHIP, not a species — `class=Alias` vs
`class=Button` is not a distinction anything should branch on. Pointing is
invisible on the wire: a control arrives as its real class and answers about
itself, under its own name. Resolving is how the data is FOUND, never a reason
to re-address the answer.

Everything below is either a place that rule is not yet true, or damage from
the years when it wasn't.

### Next steps of this work

- **Reads resolve like writes, everywhere.** Done in the two translators
  (`Bridge_Subscribe`; `ScriptReadProp` behind `get`/`sibget`/`pathget`) and in
  the plain setters (`ResolveOwned`, node.c). Not done for `GetPropStr` /
  `GetPropNode` / `GetValueStr` generally, so any new reader can still stop at
  the link and see nothing.
- **Should a handler-backed property announce?** A write to a property carrying
  `OnMsg` is delivered to the handler, and a handler that stores with
  `SetValueStr` fans nothing out — so nobody hears. That is why an `Enable`
  checkbox shows the right value at creation and then never tracks a change
  made elsewhere. Pre-existing, unresolved.
- **Alias creation is bridge-only.** `Bridge_CreateAlias` holds the recipe;
  object.c has a second copy inside `CloneAliasNode`. A headless host, a script
  host, or any other translator cannot make one. (Engine verbs `AliasProperty`
  / `CreateAlias` were added for this and have no callers — remove them or use
  them, but the gap is real either way.)
- **`Widget_Ctl`'s remaining reflect.** The Dropdown still copies `<prop>List`
  into its `Items` with `Widget_Reflect`. Last copy in the panel builder.
- **Two constructors for a panel.** A widget brings its own, laid out by a
  table compiled into its `.c`; everything else gets one synthesized by
  `bridge.c`'s `internals` command from the class Interface. Same product, two
  builders, so each must independently remember to stamp `Widget`, apply
  `W`/`H`, honour `LabelPos`. Every rendering bug that evening was one of them
  forgetting something the other did.
- **Panels build eagerly.** `Widget_DeferBuild` arms a 1ms task, so every widget
  materialises its controls whether or not anyone opens it. Lazy panels become
  possible once nothing subscribes to a control — nothing is lost by destroying
  one.

### Bugs found, not fixed

- **A queued message can be delivered to a deleted private instance.**
  `DeleteInstance` guards with `ScrubRegistrySubscriptions`, which is a REGISTRY
  walk; a ScriptBox's inner host is made through `InstanceStart` and never
  registered, so records aimed at it survive the delete. Observed as a QuickJS
  abort (`quickjs.c:1991 JS_FreeRuntime`, core dumped) during a ScriptBox
  teardown; the mechanism is inferred, not proven — build the asan variant and
  run a create/delete to confirm. **Queued fan-out widens this window**: writes
  used to deliver before the setter returned, so a write-then-delete left
  nothing in flight. Related to "registry walks need categories" — a private
  instance is exactly where "walk the registry" and "everything that can
  receive a message" stop being the same question.
- **`Widget_Create` adopts on PATH alone**, no class check (control.c:
  `ResolvePath` → return existing). A saved flow whose control changed class
  hands back the old class.
- **`Bridge_Subscribe` re-keyed the instance but not the property name** — fixed
  by resolving for the value instead, but the same shape may exist elsewhere:
  resolve for the node, never for the address.
- **Root `widget.c`** is tracked in git (last touched `fd5eb91`) and appears
  nowhere in the Makefile — a leftover from the move into `objects/widget/`.

### Debug that was missing

Nothing logged that a write crossed a link, and `ScriptBox_Activate` printed
nothing at all, so an entire evening of failures were invisible and had to be
inferred. Now: `SetOrDeliverProp` logs every write by name at `-v 3` (`WIRE`) —
what was asked for, the value, **STORE** vs **DELIVER**, what it resolved to,
and `[crossed a link]`; `ScriptBox_Activate` logs `firstCall`/host/enabled/
language at `PROG_FLOW`. The general lesson: a mechanism with no debug output
is a mechanism whose failures cost hours.

### Retired by this work

The change-check in `SetProp*` was a fixed-point detector for a two-copy sync
protocol nobody meant to write — two nodes for one datum, each announcing to
the other, stopping only when the values matched. That is why alternating
values (a Button's 1-then-0) never converged and crashed the engine, and why
`button.c` needed `SetPropStrPrivate` to hide its falling edge. With one node
there is nothing to reconcile: `Activate` can take a 1 as often as it likes,
and button.c's private `0` is now vestigial.

### Defaults versus saved values — needs a decision

Adding `change` to the Filter's `ModeList` did not reach anything that already
existed. The list is set at `InstanceStart`, so a fresh Filter offers the new
option — but a Filter restored from a saved flow gets its saved `ModeList` back
and keeps offering the old four. **A default that lives in code is shadowed by
any value a file remembers**, so changing one only reaches instances nobody has
saved yet.

That is sharpest for a property like `ModeList`, which is not instance state at
all — it is the same string for every Filter that will ever exist, a fact about
the class. Saving it per instance is what lets it go stale, and no amount of
care at the writing end fixes that as long as the file pins it.

**Idea worth pursuing: do not serialize a property whose value equals its
published default.** `PublishProp` already records the default in the class
Interface, so the serializer can compare and omit. Two things fall out of one
change:

- **Files get smaller** — most properties on most instances are untouched
  defaults, and today every one of them is written out.
- **Defaults become live.** A property nobody overrode carries no saved value,
  so it picks up whatever the code says today. Adding an option to a list, or
  correcting a default, reaches every flow that never touched it.

Open questions before doing it:

- **"Never set" versus "deliberately set to the default".** Today they are
  indistinguishable in the file, and after this change they behave differently
  the next time the default moves. Does anything need to pin a value *at* the
  default? If so it needs a way to say so.
- **Which properties are class facts, not instance state.** `ModeList` is the
  obvious one; there are likely others (published option lists, sizes that come
  from a widget table). Those may want to live on the class node and never be
  saved per instance at all, which would make the default question moot for
  them.
- **Load order.** A default now comes from the class at load time rather than
  from the file, so the class must be registered and its Interface available
  before instances are rebuilt. Probably already true, worth confirming.
- **What a diff of two flows means.** Omitting defaults makes files smaller but
  also makes two flows harder to compare literally, since absence now carries
  meaning. `flowdiff.py` would need to know the rule.

### Saving is a presentation too — `show/json`

Raised 2026-08-12, and it answers the section above rather than sitting beside
it. **A saved flow is a rendering of the node graph onto the JSON surface**,
exactly as the page is a rendering onto the web surface. So it is
`show/json` beside `show/web`, and the question "which properties get written"
stops being the serializer's to answer.

**What this takes out of the serializer.** It has no per-control knowledge today
— no `LED`, no `Slider`, no widget switch in 1646 lines — but it does hold three
things that are the class describing itself:

- `Widget` and `Label` copied by name on the way out and re-stamped on the way
  in (serializer.c:349-352, 758, 787-792), including
  `SetPropInt(inst, "Widget", GetPropInt(pub, "Widget"))` — the property widget
  TYPE, an engine enum, persisted as a bare number into the file format.
- **`Alias` as a class it knows by name**: `ImportPlace` branches on
  `strcmp(className, "Alias") == 0` into a whole deferred second pass
  (`ImportDeferAlias` / `ImportAliasesPass`, which does `CreateObject(home,
  "Alias")` and mints `Alias_N`). That is the alias-as-a-species model the
  engine no longer has — every control points at its data now, and "alias" is
  the relationship, not a class. The file format did not get the message.
- the "which props to skip" filter (serializer.c:317).

**Why it is nearly free.** The class already published its own interface — name,
widget type and default per property, straight off `PublishProp`. So
`show/json` has a **universal default implementation**: walk the interface, emit
what differs from the declared default. One function, inherited by every class
through the parent walk (Object provides it, see the class-chain design), and a
class writes its own only when it holds something the interface cannot describe
— ScriptBox's Source, an Image's data. Forty-three classes get it without
forty-three serializers.

**Why it beats a global defaults rule.** Omission becomes a decision the class
makes about itself. Mostly "leave out what is unchanged", but a class with a
field that must always be written can simply always write it. A global rule
cannot express that exception without growing a list of exceptions, which is the
shape this project keeps deleting.

**One honest correction, so this is not oversold:** it is not fewer comparisons.
The same values are still compared against the same defaults; what changes is
WHERE that knowledge lives. The win is knowledge placement, not CPU, and testing
it as a performance change would find nothing.

**Three things to settle before writing any of it:**

1. **The inverse.** If a class writes its own JSON, what reads it? Either
   `show/json` defines both directions, or writing stays per-class while reading
   stays generic-by-name — which only holds if what is written is always plain
   properties. An asymmetry here is a bug farm.
2. **Link ordering does not disappear.** A link's target may not exist yet at
   load, which is the entire reason for the deferred pass. That is a fact about
   graphs, not about aliases. The right outcome is: the `Alias`-by-name branch
   goes, the deferral stays, and the JSON surface expresses links so anything
   carrying one is restored in the second pass.
3. **Versioning.** A class that changes its `show/json` must still load
   yesterday's file. `SetClassVersion` is already on the class node; this is
   where it starts earning its keep.

**And then reflecting a view over a connection is the same call.** This is the
half that makes it worth doing. `Bridge_InstanceEvent` builds its JSON by hand -
`snprintf` plus a row of `JsonEscapeStr` calls for alias, class, classParent,
parent, container, hidden, reservedIn, reservedOut, gui, interface. That is a
hand-written encoder for precisely what `show/json` renders. Saving a view to a
file and reflecting it down a socket are one operation with two sinks; so are a
REST GET and an MCP resource read. The transport stops being an encoder.

It composes downward, which is what makes it more than tidying. Reflecting a view
is "render this subtree"; keeping it in step is "render this one property". Not
two mechanisms - a property IS a node, so a delta is the same rendering at a
smaller scope, and the event stream becomes renderings instead of fifteen bespoke
event shapes. It also makes federation fall out rather than be built: reflecting a
view to ANOTHER framework instance is the same call as saving it, and "the app is
an empty view" stops being a statement about one process.

**The rule that keeps this safe, stated once: only PUBLISHED properties render.**
Instances carry `local` (a malloc'd C pointer stored as a long), `Activate`,
`OnMsg`, `InstanceStart` - raw function pointers in properties. A "serialize
everything" walker would put those on a wire. Rendering from the published
interface cannot, because no class publishes them. This is a safety boundary, not
a nicety, and it is the same filter that makes the default implementation
possible.

**Where it must NOT overreach: commands are not renderings.** Client-to-engine is
imperative - create, connect, activate. State reflects; requests do not.
`set-property` is the tempting edge ("here is a new value for this node"), and
the temptation is exactly where this idea would start inventing.

Precedent, and the reason to believe the shape: the LED carried its own
`show/web` on 2026-08-12 and the host ended up with zero knowledge of it — see
docs/20260812_1545_a_control_brings_its_own_presentation.md. The number that
would have been duplicated (`PROP_LED`) stayed in C because the Bridge derived
it from the class's published interface. `show/json` is the same derivation
against a different surface, and the persisted `Widget: 2` above is exactly the
duplication it would retire.

### Aliasing is a core verb, not a class — and it is tangled

Found 2026-08-12. **Controls are just controls; some of them redirect
internally.** "Alias" is a GESTURE that calls one verb into the engine, the
same way Move writes X/Y and Connect records a subscription. It is not a kind
of thing, and it should not be a loadable object.

**The tell:** `Alias` is not in the palette. 43 classes seed it; Alias is not
one of them, because you cannot drop one - you can only make one with a
gesture. A class that cannot be instantiated like a class is not one.

**The second tell: Clone.** Clone also creates an instance where you drop it,
and there is no `clone.object`. It answers "what class?" from the thing being
cloned. Alias should answer it from the property being pointed at, and instead
answered it with a new species.

**What exists today**

- `bridge.c:3172` - `AddDependency(temp, "alias.object", "Alias", "1", "0")`,
  a hard dependency: without the module the Bridge refuses to load.
- Four creators of the species: `bridge.c:616` (the `create-alias` command),
  `bridge.c:838` (**every internals/options panel member** - this is the big
  one), `serializer.c:734` (the import pass), and `src/object.c:807`.
- `src/object.c:767` `AliasProperty()` and `:795` `CreateAlias()` - **this is
  where aliasing LIVES**. They are not dead code and must not be read as
  such: they are the core verbs, and they have no callers because the Bridge
  went around them and did the job itself with `CreateObject(home, "Alias")`.
  That detour is where the dependency on a loadable class came from.

**What it should be:** the callers stop doing it themselves and call the verb
that already exists. That makes this SMALLER than it looks - not "write a core
verb" but "delete three private copies of one". Fixing `CreateAlias` fixes
every caller at once, which is why they must go through it rather than around
it. Note the core verb was written against the species too (`object.c:807`
does `CreateObject(container, "Alias")`), so that one line is where the change
actually lands: create an ordinary control and link its `Value` with
`LinkPropertyAs`. The
class to create is a registry lookup, not a decision: the target's published
interface gives that property's widget type, and every control class now
STATES which type it renders (`PublishShow(ClassSelf, PROP_SLIDER, ...)`).
Look up the type, find the class that claims it, create it, link it. Any
object, any property, no module to load.

**What that retires**

- `alias.object` and the Bridge's dependency on it.
- The `Widget` stamp carried on an alias instance and persisted into saved
  flows - the control already IS that control (see the `show/json` section:
  this is the same `Widget: 2` duplication from the other end).
- `strcmp(className, "Alias")` in `ImportPlace` and the deferred pass keyed on
  a class name. The deferral stays - restoring a link before its target exists
  is an ordering fact about graphs - but it keys on links, not on a species.
- ~156 lines of client rendering (`registerAliasAtom`, `renderAliasControl`,
  the `aliasAtoms` map, the `className === 'Alias'` branch). The browser draws
  a control; the redirect never reaches it.
- The options panel stops being a separate construction: a panel row is a
  control that redirects at the object's property, which is the same sentence
  as a dropped alias on the canvas. Two code paths become one.

**Why this is filed and not done.** It touches `Bridge_CreateAlias`, the
internals-view builder, the import pass and the client at once, and the
evidence that the area is tangled is concrete: on 2026-08-12 an attempt merely
to RELOCATE the client half into `objects/alias/show/web/` (a move, not a
redesign, of the kind that worked for Control and View the same evening)
failed in a way that could not be explained from the logs - the atom was
created, its X/Y/Container subscriptions arrived, no page errors, and yet
`aliasAtoms[...].target` never populated. It was reverted. **Do not attempt
this as a relocation; it is a deletion, and it wants a session of its own with
the harness green underneath.**

**One wrinkle to settle first:** `renders` currently lives under `Show`
(added 2026-08-12 for the web assembly). If the core needs it to answer "what
control shows this kind of property?", it is not presentation - it is a class
fact that presentation happens to use, and it belongs on the class directly.

### Filter widget: In/Out readouts stay blank, and `change` looks dead

Observed 2026-08-12 on the Filter panel. Both need tracing rather than
theorising; what follows is what is known and what is only suspected.

**`In` does not update as traffic passes.** A store-on-arrival was tried -
`DeliverToSubscriber` (node.c) and `SetOrDeliverProp` (object.c) setting the
property BEFORE calling its handler - and it was a double send: every control's
own handler stores what arrived, so the same value was written twice for one
arrival, and with the on-change gate gone both writes fanned out. One click of a
Checkbox reached its subscribers twice; a script counting rising edges counted
six for three.

**Settled 2026-08-12: the handler says whether it stored, and the deliverer
believes it.** The three existing return codes already carry exactly that, so
nothing new was added and no handler was edited:

    rtrn_handled    took it, and a control that takes a value stores it -
                    deliverer stores nothing
    rtrn_propagate  watched it without consuming it (a probe) - the universal
                    default applies, the value lands on the property and its
                    write fans on
    rtrn_dropped    nothing took it (a disabled control refuses the value
                    rather than displaying it) - deliverer stores nothing

The handler runs first and ANSWERS, instead of being told afterwards. Both
delivery paths follow it. `SetOrDeliverProp` calls the handler directly rather
than through `DeliverMsg`, because `DeliverMsg` reports whether a handler
EXISTED, not what it decided - and its 0 return collides with `rtrn_handled`
being 0, so widening its meaning would break tcp, udp, mcpsource and router.

Filter still returns `rtrn_handled` (it consumes and sends its own filtered
copy), so its `In` readout is unchanged by this and stays open - and it is
Filter's own business, in filter.c.

**`Out` will not update, and the reason is known: sending does not store.**
`SndMsg`/`SndMsgNode` builds an envelope and dispatches it to the property's
subscribers; the source property itself never holds what went out. So a readout
on `Out` has nothing to show, and no amount of traffic changes that. This is
the exact mirror of the `In` gap - **arriving now stores, leaving still does
not** - and it wants the same answer: what left a property is that property's
value. One store in the send path, symmetric with the one in the delivery path.
Watch for the loop it could create: storing on `Out` fans out to `Out`'s
subscribers, which are the very things the send is already being dispatched to.
That likely means storing without announcing, or storing before the dispatch
rather than as a second delivery.

**`change` mode appears not to work.** Implemented 2026-08-11 (compare against
the last value that PASSED, not the last seen), added to `ModeList`. Reported
as having no effect. Candidates, in order of cheapness to check: the instance
is still carrying a saved `ModeList` without the option (see "Defaults versus
saved values" above, which is exactly this failure); the mode string never
reaches `local->mode` because `Activate` reads `Mode` once and the panel's
set-property lands later (there is already a comment in filter.c about that
race); or the comparison itself is wrong. The `-v 3` `SET` line will say which,
since it reports every write by name, what it resolved to, and whether it
stored or delivered.

### Subscribing to a property that is not there — needs a decision

Found 2026-08-12, and it cost most of a day's confusion before it was named.
`ScriptBox.Out` was removed in 9283c27 (Output IS the out), but six harness
tests still subscribed to it and wired from it. Not one of them errored. They
ran green-ish and silent and reported nothing forever, because `Bridge_Subscribe`
calls `Connect(inst, port, bridge, "Taps")`, and **Connect creates the source
property if it is absent**. So asking about a property that does not exist
brings a dead one into being, hands back a subscription to it, and every party
believes the wire is made.

That behaviour is not an accident and it is not obviously wrong. It falls
straight out of "a property is a node": naming one is how you get one, and the
alternative — refusing to wire to a name until something has created it — would
make wiring order-dependent, which is the thing the whole subscriber model
avoids. A late-arriving handler still wins, since `DeliverToSubscriber` reads
`OnMsg` off the node at delivery time, not at Connect time.

But the cost is that a typo, a renamed property, and a property that has not
been created yet are all the same event, and the system's answer to all three
is silence. Which is the one bug class this design is otherwise very good at
surfacing loudly.

**What the code actually does, checked 2026-08-12.** The asymmetry is already
half-decided, and in the opposite direction to the intuition. `Connect` (object.c)
requires the **sink** to exist - if `ResolvePort` cannot find it, it logs an
ERROR naming the instance and the property and returns 0. Only the **source** is
invented, and even that is careful: a property that exists but whose link dangles
is refused rather than overwritten, because writing 0 over it would destroy the
link and the value it carried. So the whole of create-on-demand is five lines:

    /* genuinely absent - make the source property exist */
    SetPropInt(fromNode, from, 0);
    fromPort = GetPropNode(fromNode, from);

Making both ends behave the same is deleting those five and letting the source
take the sink's error path. `Bridge_Subscribe` already handles a failed Connect
("connect failed"), so the silence becomes a reported error with no new
mechanism anywhere.

**RESOLVED the same evening, in the other direction: creation on demand is the
MECHANISM and must stay.** The leaning recorded here first was "both ends must
exist" - and it was wrong, because it would break late binding. A property
exists BECAUSE something referred to it, and that is how an instance is
annotated (`GUI_Format`), how a default is overridden on one instance, and how
a script adds state to an object whose class never declared it. A class
publishes what it ships with; an instance grows the rest. Requiring both ends
to pre-exist would forbid all three.

Measured, once the logging below was in: **78 properties come into being during
a single normal boot** - 34 `ReservedViewResizeable`, 23 `LabelPos`, 17
`LastMember`, and a `W`/`H` pair. Not one of them is a typo. That is the system
working.

So the problem was never the creation. **It was the silence** - a conjured
property is indistinguishable from one that was always there, which is what let
six harness tests subscribe to a property removed two commits earlier and report
nothing for weeks. The fix is to see it, not to forbid it.

**"A connection is not finished until both sides have been updated."** The
larger idea behind it: today the record lives only on the source, so "what am I
connected to" is a registry-wide walk (one of the 22 - see the walks/categories
work). If the sink held its half, that question would be local, and deletion
would not need a registry-wide scrub. Worth wanting.

**The two ends hold different things, which is why this is not the two-copy
trap.** What wedged the engine on 2026-08-11 was two nodes holding the SAME
value and each trying to bring the other into step - a convergence protocol
with no fixed point. A wire recorded at both ends is not that. The source's
list answers "who do I send to" and is walked at delivery; the sink's answers
"who sends to me" and is walked by a gather, by delete, and by every
introspection question that currently has to walk the whole registry
(docs/20260807_1335_wires_that_know_both_ends.md: the Sum widget is the visible
motivation, deletion is the real prize - O(my edges) instead of O(the session),
and correctness stops depending on a walker being exhaustive). Two questions,
two answers, nothing converging.

What it does cost is referential integrity - the two entries must appear and
disappear together across delete, rename, clone, import and scrub. That is a
much cheaper problem than value sync, and the plan already addresses it
directly: teardown sends break-connection down its own list and the far end
just removes its entry (nobody negotiates), and the doc's Phase 0/Phase 2 are
an oracle and a checker built BEFORE any reader is allowed to trust the mirror.

Which also settles this section's question. If a connection is something both
ends take part in, **both ends must be there to take part** - you cannot notify,
or be refused by, a property that does not exist. Create-on-demand is not a
separate decision; it falls out.

The questions to settle:

- ~~**Does anything legitimately rely on the source being invented?**~~
  **Answered 2026-08-12: yes, constantly - 78 times per boot, and by design.**
  Deleting the five lines would break annotation, per-instance override and
  script-added state.
- **Is there something to check against?** A class already publishes what it
  has (`PublishProp` / the class interface the bridge sends as `interface`). A
  wire to a name the class never published is a different thing from a wire to
  a name it published but no instance has written yet. That distinction exists
  today and nothing consults it.
- ~~**Should this be loud without being fatal?**~~ **DONE 2026-08-12**
  (`object.c`, `WIRE`): the line names the instance by path and the property,
  and says late binding is normal - worth a look only if that name was meant to
  already exist. Still open above it: whether a TRANSLATOR (bridge, script,
  MCP) should refuse a name the class never published, since that is where a
  human's typo enters, while the C API keeps creating, which is where an object
  and a script legitimately build their own shape.
- **Does the same question apply to reads?** `get-property` on a name that does
  not exist returns empty, which is indistinguishable from a property holding
  an empty string. Same failure, same silence, probably the same answer.

Worth deciding before the palette grows: every new widget is a new set of
property names, and every renamed property is another silent wire.

### Naming is not happening at a low enough level

Raised 2026-08-12, off the back of the silent-subscribe find: too many things
cannot say who they are, so too many error messages say `?` or `(unnamed)`.
Counted in the tree today: **27** call sites carry a fallback string for a
subject whose name could not be got.

The cause is that there are two naming systems at two different levels, and the
lower one is not the one instances use.

- **node.c gives every node a name slot** - `SetName` / `GetNameStr`, one call,
  works on anything, used 68 times. That is naming at the right level: a
  property, a data chunk, a class node and an instance are all nodes, so all
  four should answer the same question the same way.
- **An instance is named by a `Name` PROPERTY instead** - `GetPropStr(inst,
  "Name")`, 32 sites. And its own name slot holds something else entirely: the
  CLASS name (`SetName(instance, "Checkbox")` in every InstanceStart), which is
  a third copy of what the class node it is registered under already says.

So a function holding a bare node can always name a property and can never name
an instance without knowing the convention that lives a layer up. Every error
message written from the core has to guess which kind of thing it was handed,
and the ones that guess wrong print `?`.

Downstream of that:

- `PathOfInstance` derives from the `Name` and `Container` PROPERTIES, so
  anything mid-rename has no path at all (already a known sharp edge - capture
  the path before mutating either).
- The serializer treats the `Name` prop as the identity (`the node's IDENTITY
  is its "Name" PROP (Slider_1), NOT the JSON key`) - so the file format is
  built on the upper system, and any change here is a format question too.
- "What is this" and "who is this" are answered from different places, and one
  of them is stored twice.

**Two steps, and they are independent.**

1. *Cheap and uncontroversial:* one accessor that names any node and never
   returns NULL - property name, instance name, class name, whatever it holds -
   so every DebugPrint site becomes one call with no ternary. That deletes 27
   fallbacks without deciding anything, and it is the lowest layer, which is
   where it belongs.
2. *The real question:* which slot is the identity. The uniform answer is that
   the node's own name IS its name, for an instance exactly as for a property,
   and "what class is it" is answered by the class node it is already
   registered under - no copy needed. The cost is the `Name` property's 32
   readers and the saved-flow format. Worth doing, worth not doing by halves,
   and NOT worth two live copies of the name during a transition - that is
   precisely the shape that wedged the engine.

### Presentation: what shipped 2026-08-12, and what Phase 2 is

The section below argued for it; this is the outcome. Full account:
docs/20260812_1545_a_control_brings_its_own_presentation.md.

**Shipped.** Sixteen classes carry their own browser half - fourteen controls,
then View, then Control itself - as `show/web/<name>.js`/`.css` on the class
node, published at `ClassStart`, compiled into the `.object` so a control is
still ONE file to install. 1292 lines moved out of the host; `web/app.js` went
2693 -> 2102. Two shared pieces were hoisted only AFTER the LED had done it the
long way: `objects/show.mk` and `PublishShow(class, renders, js, css)`.

**The shape decision is gone.** No atom-or-view branch: the host walks the
class chain (class, parent, View) and asks whoever answers. A base class that
renders differently takes over its whole branch with no host edit - which is
the entire point, and is now mechanism rather than intention.

**Loud failure.** A class with no registration draws a visible `?ClassName`
and logs. The silent textbox fallback is gone.

**What the net caught** (all three would have shipped otherwise): a class's
rendered-type INFERRED rather than stated, which would have let MoButton steal
the LED's type and Button claim every `PROP_NULL` property; atoms built from a
module never subscribing to their own Value, so an LED would never light; and
a CSS extractor splitting a multi-line compound selector, which took every
atom to 190px wide. The golden palette-shape test named each in one line.

**Phase 2, with owners.** ~600 of the remaining 2102 lines are genuinely the
host's (socket, send, routing, loader, mode relay, mount). The rest:

    ~300  the wire layer                    -> View (already per-view layers)
     220  startDrag                         -> Control/View (a drag writes X/Y)
    ~130  the gui* mask/validate family     -> Control (annotation on the data)
    ~120  the flow dialog                   -> Serializer (its own verbs)
      89  makeMenuButtonEl                  -> MenuButton
      32  dropTargetAt                      -> View (a drop is containment)
     152  the alias rendering               -> nobody: deleted, see below

**One correction found by doing it:** `renders` currently sits under `Show`
because it was added for the web assembly. The alias work needs it in the
core, so it is not presentation - it is a class fact presentation happens to
use, and it belongs on the class directly.

**And the derailment, because it is the useful part.** Control and View both
relocated in minutes. Alias - also a control - failed the same move
inexplicably and was reverted. The questions that followed showed why: there
is nothing visible that IS an alias, controls just redirect internally, and
aliasing is a core verb that already exists in object.c. The 156 lines were a
client-side reimplementation of a link the engine resolves for free. See
"Aliasing is a core verb, not a class" above.

The generalisation, which extends "The asymmetry, and using it as a signal":
**when a move that should be mechanical is not, suspect the THING, not the
move.** Two relocations of identical shape had just succeeded; the third
resisted. Debugging the move could never have produced the answer, because a
relocation cannot tell you that what you are relocating is imaginary. An hour
of failing to move something is evidence about what it is.

### Presentation belongs to the control — and there is more than one surface

Extends the Phase 8 item "A widget's client half ships with its class", which
had the renderer as a property on the class node and assumed one client. The
generalisation: **a control carries its own presentation, per surface, and the
host never enumerates controls at all.**

    objects/checkbox/
        checkbox.c                    what it IS
        presentation/web/             how a browser draws it   (.js, .css)
        presentation/rest/            how it answers over REST
        presentation/macos/           a native surface
        presentation/mcp/             how an agent sees it

Same object, several presentations, none of them privileged. `web` is simply
the one that exists today.

**Why this and not more branches.** Right now a control is defined twice -
`objects/checkbox/checkbox.c` and, in `web/app.js`, an entry in
`INPUT_WIDGET_CLASS`, a `case` in `buildValueControl`, sometimes a
`className ===` branch, sometimes a whole bespoke maker. Adding a control means
editing the host, and forgetting the client half fails **silently**: the lookup
misses, the switch falls through, and the control renders as a textbox with no
error anywhere.

That is not hypothetical. Every rendering failure on 2026-08-11 was one of
those ladders missing a rung: `PROP_BUTTON` absent from `INPUT_WIDGET_CLASS`;
no `case 'Button'` in the factory; `makeMoButtonEl` and
`makeSelfActivateButton` building DOM directly and so skipping the sizing every
other control got; the input/display/readout split deciding a control's kind
before drawing it. Hours went into those, and each fix was another rung rather
than a reason for the ladder.

**What it retires.** `app.js` stops being a switch over widget types and
becomes a loader: an `instance-created` names a class, the client fetches that
class's `presentation/web` if it has not already, caches it, and asks it to
render. `INPUT_WIDGET_CLASS`, `DISPLAY_WIDGET_CLASS`,
`READOUT_WIDGET_CLASSES`, `buildValueControl`'s switch and the `className`
branches all go, and a control written tomorrow renders today without the host
being told it exists - the same promise the engine already keeps for
`.object` loading.

**How it is served: build it once, at bridge start.** No per-view scanning, no
lazy fetch, no cache negotiation. When a Bridge instance comes up it walks the
registered classes, concatenates every `presentation/web` it finds into one JS
blob and one CSS blob, holds them in RAM, and serves them as a single file that
`app.js` pulls in. The palette shows one instance of every class, so the page we
are actually building for needs all of it anyway - selecting a subset would be
work done to save nothing.

That also settles what "adding a control" means end to end: drop the `.object`
in the scan path, restart, and its browser half is in the blob the bridge
serves. Same deployment story as the engine half, same moment, no host edit.

**The contract that keeps the host generic.** Splitting the rendering out is
only half of it - if the host still needs a `case` per widget type to UPDATE a
control, the ladder has just moved. The Pico W framework linked below solves
this in about forty lines: every value-bearing element it emits carries
`data-rid="<item id>"`, and the whole client update path is one selector over
that attribute. A bar needs min/max, so it emits its own hidden element
carrying them; the generic loop reads what is there. **A widget's per-type
knowledge travels in the markup it emitted, not in the host's JavaScript.**

The DataObj form: a control's presentation emits markup tagged with the
property it stands for, plus whatever its own updater needs; the host
subscribes to that property and drives every tagged element the same way, with
no case for what kind of control it is. That contract is what determines
whether the ladder comes back - more than the file layout does.

**A surface is not chosen, it is composed.** You get the web presentation
because you ran a Bridge; you get the MCP one because you ran an MCP object.
Each translator reads the presentation directory that matches it - Bridge reads
`presentation/web`, an MCP object reads `presentation/mcp`, a REST object reads
`presentation/rest` - and builds its own blob at its own instance start. There
is no negotiation, no surface parameter, and no registry of surfaces: the
surface IS the object, exactly as an application is a set of objects and their
wiring. Run two and you have both at once, over the same live instances, with
neither knowing about the other.

Open question:
- **What a presentation is allowed to be.** For `web` it is code (js/css). For
  `rest` and `mcp` it is closer to a schema - a description of the control's
  value and how it is written. Those may not be the same kind of artifact, and
  pretending they are would be its own special case.
- **The fallback.** A class with no presentation for the asked-for surface has
  to do something. Today that fallback is "textbox", chosen by accident of a
  switch's `default`. Whatever it becomes should be a stated rule, and should
  say so out loud rather than silently drawing the wrong control.

**What every surface actually is.** Underneath, all of them reduce to the same
tiny thing: **get and set on an addressable property, plus verbs you can define
on top.** That is already true of both surfaces that exist. The bridge speaks
`set-property` / `subscribe` and then a verb list - create-instance, connect,
activate, clone, save-flow. A script host speaks `get`/`set`, `sibget`/`sibset`,
`pathget`/`pathset` and then the same verb list in its own syntax. Neither is
a different protocol; they are the same two ideas wearing different notation,
which is why `script.c` and `bridge.c` keep needing the identical fix on the
identical day.

So a new surface is not a project. It is: get and set over whatever transport
it has, then whichever verbs make sense there. The engine owns what a verb
DOES - the mechanisms live in object.c as language-neutral calls - and the
surface owns only how it is spelled. `presentation/<surface>` is the rendering
half of the same split, and the verb table is the control half.

That is also the honest test of any new surface work: if adding one requires
teaching the engine something, the split has been drawn in the wrong place.

**And underneath that: hold the data so a functor can carry it.** The point of
get/set plus verbs is not that the protocol is small - it is that if the node
graph is held in a faithful enough shape, one structure-preserving mapping can
carry it into a target's data map, instead of a translator being written per
control per surface. Containment maps to containment. A property maps to a
field, an endpoint, a binding. A value maps to a value. A link - a control
pointing at its data - maps to whatever that surface calls a reference.

If that holds, then `presentation/<surface>` only has to carry what is
genuinely presentational: how a slider LOOKS, not how its value is found or
written. The data plumbing is generated from the graph, once, for every object
at once - including objects that did not exist when the surface was written.
That is the difference between a surface being a project and a surface being a
mapping.

**What it demands of us, and where we currently fail it:** a functor cannot
carry a convention it has to parse. Today the annotations a presentation needs
are held four different ways - `Widget` as an integer on the class Interface,
a control's options as a comma-separated string in a companion `<prop>List`,
min/max/width packed into a `"min:0,max:100"` props string in a widget table,
sizes as ordinary `W`/`H` properties. Only the last of those is node data. The
rest are encodings that every consumer has to know how to unpack, which is
exactly the per-surface knowledge the mapping is supposed to remove.

So the enabling work is not the surfaces - it is making the annotations
ordinary nodes, the way `W`/`H` already are. Properties can have properties;
nothing new is needed for a control's min, max, options, or widget type to be
nodes hanging off the property they describe. Once they are, a mapping can walk
them without knowing what any of them mean, and the surface stops being a
place where the framework's own vocabulary has to be re-implemented.

**Nor does a value have to be a scalar.** `DataObj` holds STRING / INTEGER /
HEX / REAL / LONG, and everything that crosses a boundary crosses as text.
That is why the MCP work was tractable in a day - both sides are stringly
typed, so no schema layer was needed - and it is also exactly where that
approach stops, the moment something wants real nested or typed data.

The way out is not more scalar types in `data.c`. It is **topology nodes:
objects that carry a shape** - a table, a graph, a linked list, a JSON or XML
document, a custom format - addressable, subscribable, and get/set like
anything else. A Table is an object in the same sense a Reader is. Nothing in
the core changes; the structure lives in the object that carries it, which is
the same answer this framework gives to everything else.

For the mapping, this is what makes it shape-to-shape instead of
string-to-string. A REST resource, a SQL row, an MCP schema, an RDF triple:
each is a topology the graph can hold directly rather than flattening into
text and hoping the far end parses it back the same way.

**The discipline that keeps it honest:** the structure has to be WALKABLE
through the node interface - a table's rows and fields reachable as nodes -
never an opaque payload in a format only its consumers understand. An opaque
blob is the convention-parsing problem again, one level up, and it costs the
automatic conversion that makes the data model work at all: the moment a value
is a private encoding, nothing generic can read it, and every surface is back
to needing bespoke knowledge of every object. Properties can have properties;
a structure made of nodes needs no new mechanism and stays legible to a mapping
that knows nothing about what it is looking at.

### A date is a widget, not a type

Worth settling because the instinct pulls the other way: a date looks like it
wants to be a `DataObj` type with a default representation and an override.

It should not be. The **value** does not change between ISO 8601, Julian, epoch
seconds, or a locale rendering - only how it is written down does. Putting a
DATE type in `data.c` puts representation in the core, and then every surface,
every script, and every serializer has to know about it. Whereas a control that
renders a value in a chosen format is just a control, and the engine keeps
holding what it already holds.

The mechanism is also already here, one step short. A property can carry
`GUI_Format` - today a character MASK, `"(###) ###-####"` - and `GUI_Pattern`,
a regex the raw value must match. Both are ordinary properties, read by the
client, ignored by the engine. A date control is that with **named
representations** rather than a mask: a dropdown of presets, plus `custom` and
a format string to fill in.

Two things to keep straight when it is built:

- **Keep the preset and the custom pattern as separate nodes**, not one
  overloaded string. "The name of a format" and "a format spec" are different
  facts, and squashing them into one field is the encoding-instead-of-node
  problem from the section above - a mapping to another surface would have to
  parse its way back out.
- **The list of presets is the same shape as a Dropdown's `<prop>List`**, which
  is currently a comma-separated string. Whatever fixes that fixes this too;
  they should not get separate answers.

One consequence worth noticing, and it is only true since controls point at
their data rather than owning it: **two controls on the same timestamp can
render it two different ways at once.** An ISO readout and a Julian readout
side by side, neither of them owning the value, no argument about which is
"the" representation, and nothing to keep in sync. Under the old copy-and-
reconcile model that was two copies drifting; now it is one node seen twice.

### Cadence, and what it demands

The rate is steady, and the shape of the work changes once the core settles:
the core stops being the thing that grows and the library does. One new control
a week is fifty-two in a year - date pickers, an address widget, colour
pickers, spinners, whatever a panel turns out to want.

That cadence is the whole argument for the presentation work above. At one
control a week, a control that has to be defined in two places is a hundred and
four edits and fifty-two chances to forget the second one - and forgetting it
fails silently, rendering a textbox with no error anywhere. The authoring path
has to be: write the object, write its `presentation/web`, drop it in the scan
path. Nothing else, nowhere else.

Two of those examples are also composites - a date picker and an address widget
are several fields behaving as one control - so they lean on Phase 5 rather
than on new primitives. Worth knowing before the list gets long: a good share
of the fifty-two will be Views with bound ports rather than new C objects, and
that path wants to be as cheap as writing a control.

### Containers inside a view

Nesting already exists - a View is a container and Views nest, which is what
`Widget_SubPanel` builds (TPLink's Settings and Help are sub-views). What is
missing is that **a container can carry properties that apply to what is inside
it.**

- **Background colour** is the easy half: an ordinary property on the container
  that its presentation reads. No engine involvement, nothing new - the same
  annotation-as-node shape as everything else in this section.
- **Radio** is the interesting half, because mutual exclusion is behaviour, not
  decoration, and there are two readings of it:
  - *Several properties, one rule.* Each checkbox points at its own property
    and the container enforces "setting one clears the others". Something has
    to hold that rule, and it is engine-visible - a script reading those
    properties sees the exclusion too.
  - *One property, several options.* The group is a single menu property
    rendered as buttons instead of a dropdown. Nothing new is needed at all:
    it is the Dropdown's `<prop>List` with a different presentation, and
    exclusion is a consequence of there being one value rather than a rule
    anyone enforces.

The second is almost certainly right, and it is worth noticing why: exclusion
stops being something to implement and becomes something that cannot be
violated. That is the same move as a control pointing at its data instead of
holding a copy - the invariant is structural rather than maintained.

Which leaves the real question for containers: **which properties of a
container apply to its members?** Background colour clearly does. Enable
probably should - disabling a container disabling what is in it is what anyone
would expect, and today Enable is per-instance. That wants deciding once, as a
rule about containment, rather than per-container-type.

### The asymmetry, and using it as a signal

Observed after a long day of both: **once the right change was understood it
took minutes; every wrong direction started breaking things immediately.** That
is not luck, and it is usable.

**The right change is small because the mechanism is already general.** Almost
nothing on 2026-08-11 added capability. `ResolvePort` was already on every
write path, so pointing a control at its data meant removing the copy, not
adding plumbing. `LinkPropertyAs` was already written, for aliases.
`local->last` was still sitting in the Filter's struct with its comment intact,
waiting for the mode that had been deleted around it. `ScriptBox_Activate`
already served both its callers. The work kept turning out to be deletion, and
in a uniform system one deletion is inherited everywhere.

**The wrong change is loud because uniformity has no corners.** Making panel
controls a different class meant every general walker met the new species at
once - the renderer, the sizing, the caption logic, the subscribe path. There
was nowhere for it to hide. In a system built from special cases a wrong turn
breaks one path and may go unnoticed for a week; here it broke everything
inside one reload.

That is the design refusing, and it inverts the usual cost profile: **a wrong
direction is cheap to DETECT and expensive only if you continue.**

**So the asymmetry is a signal, and it is available before the tests run:**

- Minutes, and quiet - you found the layer the fact belongs to.
- Hours, and spreading breakage - you are adding a case, wherever you think
  you are and whatever you are calling it.

The concrete tell is writing code so that something will KNOW ABOUT a
situation: a second species, a new enum value, a wrapper around a function that
already did the job, another branch in a factory. Every one of those grew the
breakage. Every removal of the thing that made a case necessary took minutes.

**One caveat, and it is the argument for closing the seams.** This only holds
where the invariants already hold. The hours that day went to the places where
one thing was already pretending to be two - two render paths, two panel
constructors - and in a seam BOTH directions are slow, because you can neither
fix the general mechanism nor isolate the change. That is the real reason to
close them: not tidiness, but that a seam is the only place where the system
stops telling you quickly whether you are right.

## Found 2026-08-13 — the day aliasing came back into the core

`CreateAlias`/`AliasProperty` had sat in object.c with no callers while three
places built aliases by hand, and a fourth (`CloneAliasNode`) did it again
inside the core. Removing the detour deleted a loadable class, a hard
dependency, five branches on a class name, and a compatibility shim that turned
out to be unnecessary.

**Done that day** (the story is in
`docs/20260812_2300_aliasing_is_a_verb.md`, appended):

- `CreateAlias` makes the control that says it `Renders` the target property's
  type — `FindClassRendering`, the `FindClass` walk asking *what* instead of
  *who*. There is no Alias class; `objects/alias/` is gone.
- All four builders call the verb: the Bridge's `create-alias`, the internals
  panel builder, the serializer's import pass, and `CloneAliasNode` (which now
  copies the source's own class).
- `IsAlias(inst, &target, &prop)` replaces every `strcmp(className, "Alias")` —
  it asks whether the instance's `Value` is a link, which is the question those
  five branches were always really asking.
- Saved flows needed no shim: `ImportPlace` recognises an alias by carrying
  `Target`/`TargetProp`, so files from either side of the change load identically.
- The Bridge's `alias.object` dependency is gone, and it announces the real
  class instead of the literal `"Alias"`.
- `web/app.js` 2102 → 1914 lines: the client's alias rendering deleted, with
  the suite running identically before and after.
- The View claims `PROP_ICON` (item 1) and `PROP_NULL` stopped being treated as
  a type (item 1a).
- `Bridge_FindTap` is keyed by who asked, and one change reaches every tap on
  the node (item 3).

What follows is what that turned up and did NOT fix.

### 1. Nothing declared that it renders an icon — FIXED same day: the view IS the icon

`control.c:531` publishes `ReservedViewOpen` as `PROP_ICON`, and no class
declares `Renders = PROP_ICON`. `CreateAlias` asks the registry which class
shows a property of that kind, gets nothing, and refuses.

Two consequences, both live:

- `create-alias` of a thing's Open fails outright.
- **Every options panel silently drops its `ReservedViewOpen` row**, because
  `Bridge_Internals` makes one alias per property and that one cannot be made.
  A thing's own doorway is part of its state — "the whole frog on the
  dissection table" — and it is currently missing from the table.

**The answer: the View claims it.** Opening a thing shows its view, so the
doorway and the thing behind it are one object rather than a picture of one —
`objects/view/view.c` now publishes `PublishShow(ClassSelf, PROP_ICON, …)`, the
same way Slider claims `PROP_SLIDER`. An alias of an Open is a View, which is
what it always was.

Worth keeping as the record of how it was found, because the search was
backwards: the question looked like "what should we build to draw an icon",
and the thing that draws it already existed and was the most obvious object in
the system. A missing renderer for a published type is a question about which
EXISTING class owns that meaning, not a request for a new one.

Covered by `rawtest: widget stamp: an alias of ReservedViewOpen is a doorway`,
and it was half of why `guitest: rename-cascade: the dissection table has its
members before the rename` failed — the other half was the alias map.

### 1a. `PROP_NULL` is the absence, not a kind — FIXED same day

`X`, `Y`, `W`, `H` and `Container` all publish as `PROP_NULL` (control.c), and
`CreateAlias` was looking up a class that renders it. Asking which class renders
*no control* is a category error; those five rows silently vanished from every
options panel until it fell through to text like any undescribed property.

Same shape as item 1 from the opposite side: there a real type had nobody
claiming it, here a non-type was being claimed. Both were "the engine asked the
registry a question the registry could not answer", and both showed up as a
panel quietly missing rows rather than as an error.

### 2. The client renders an alias as a species — DONE same day

Deleted: `registerAliasAtom`, `renderAliasControl`, the `aliasAtoms` map, the
`className === 'Alias'` dispatch branch, the `Target`/`TargetProp`/`Widget`/
`Label` branch in `onPropertyChanged`, the teardown and rename bookkeeping, and
the two widget constants only that renderer read. 188 lines, and the suite ran
identically before and after — the definition of dead.

The eleven guitest checks that reached for `aliasAtoms` were rewritten to ask
the engine instead (`stands_for`, `members_of`): a panel's contents are found by
containment like anything else on any canvas, and what a control stands for is
read off the control. All but one pass.

Near-miss worth keeping: `widgetClassForType` looked equally dead — one
occurrence in `app.js`, its own definition — and is called three times by
`objects/control/show/web/control.js`. The controls' JS is concatenated into
`widgets.js` and shares one global scope, so **"unused in this file" is not
evidence in this codebase.**

**A consequence worth knowing independently of the tests:** a panel row used to
register its control under the TARGET's key (`box.Source`), because the alias
atom bound to `Target`.`TargetProp`. Now the row is an ordinary control that
subscribes to its own `Value`, so it registers under the ROW's key. That is
correct — resolving locates data, it does not re-address the answer — but
anything keying on `liveControls['<object>.<prop>']` for panel rows moved with
it.

### 2a. A doorway must wear the name of what it opens — and open it

Deleting the client's alias rendering does not fix this; deleting it is what
REMOVES it, so this is behaviour that has to land somewhere else first.

`app.js:817`, inside `renderAliasControl`, is the whole doorway:

```js
lb.textContent = rec.label || baseName(rec.target);         // the TARGET's name
ic.addEventListener('click', () => panels[rec.target].setOpen(true));
```

A View labels itself `baseName(alias)` — its own name — and opens its own
panel. So a control standing for a thing's `ReservedViewOpen` currently reads
`Alias_4` and opens an empty panel of its own instead of the thing's.

Two behaviours, and only the first is asserted anywhere
(`guitest: rename-cascade: the Open icon's own label reads the NEW name`). The
click-through is unasserted and currently wrong.

The shape of the answer is visible in that line: `rec.label ||` says `Label`
was always meant to be the source and the alias code was synthesising a
fallback. So the engine stamps `Label` on the control the way it already stamps
`Widget`/`Target`/`TargetProp`, and `view.js` draws `Label` when it has one —
no control learns what an alias is. For the click: the control's `Value` IS a
link to the target's `ReservedViewOpen`, so writing its own `Value` opens the
target through the link, and nothing needs to know why.

Filed separately from gap 2 on purpose. It was first written down as a
subclause of "delete the dead client code", which reads as though it comes out
in the wash — the exact mistake of assuming a deletion is a fix.

### 3. A node's address is not a unique key, and code assumed it was

`Bridge_FindTap` identified a tap by WHICH NODE CHANGED. That was safe only
while one name ever reached one node — and links have never guaranteed that.
The moment aliasing became ordinary, two controls legitimately pointed at one
property, the second subscriber silently joined the first one's tap, and every
update went out under somebody else's name. Writes worked the whole time,
because those travel DOWN through the link; only updates coming back UP through
a tap were lost.

Fixed (the tap key now includes who asked, and one change is emitted to every
tap on the node). **The general hazard is not fixed and is not audited**:
anywhere a node pointer is used as an identity, two names for one node is a
case that was never considered. That is a sweep waiting to happen, and this is
the first known instance rather than the only one.

It is also the sharpest argument yet for the both-ends subscriber lists
(`docs/20260807_1335_wires_that_know_both_ends.md`): a node that knows who
points at it does not need anyone to key a side table by its address.

### 4. The harness had no opinion about any of this

The tap bug was found by hand, in a browser. Nothing in the harness exercised
two names on one node, so nothing failed. That test still needs writing, and it
should be written against the old key so the record shows what it catches.

More generally, this is the day the harness started carrying `roadmap=`
declarations (`Report.expect(..., roadmap="...")`): a check describes what the
engine SHOULD do, and when the engine does not do it yet, it is measured and
listed under NOT YET instead of being absent. Two rules keep that honest and
are enforced in code — it must fail for its stated reason, and a not-yet that
PASSES is a failure, so a declaration cannot outlive the work it names.

Tests are written against the design. A check that has to be weakened to go
green was measuring the implementation.

### 5. The planned compatibility shim was unnecessary — and that is a lesson

`docs/20260812_2300_aliasing_is_a_verb.md` makes a saved-flow translation shim
step 1, the thing everything else depends on: *class Alias + Widget N → create
the class that renders N*. It was never needed. A saved instance already
carries `Target` and `TargetProp` as ordinary properties, so recognising an
alias by what it CARRIES rather than what class it claims makes files written
on either side of the change identical to the serializer.

The general form: **a compatibility shim is evidence you are still asking the
wrong question.** The class name was never load-bearing; only the belief that
it was made the shim look necessary. Worth checking against the next migration
that seems to need one.

That document should be corrected rather than deleted — the plan's ORDER was
right, and the reasoning that produced a step that turned out to be free is
more useful kept than tidied away.

### 6. Properties are not in the namespace, so a question about one has no address

Asking whether something exists is already solved and already free:
`ResolvePath` (namespace.c, via object.c) answers it in O(path length) and
creates nothing. That is what the namespace is FOR.

But only INSTANCES are in it. A property has no path, so "does this thing carry
a Target?" cannot be asked the cheap way, and the only read the protocol offers
is `subscribe` — which goes through `Connect`, which grows a missing source
property rather than failing. Late binding working exactly as designed, in a
place where nobody meant to refer to anything. So the probe creates what it
probes for.

Found on 2026-08-13 by a test telling an alias from an ordinary control by
asking each member of a view whether it carried a `Target`. Every member it
asked came away carrying one, freshly made, reading `0` — it picked the wrong
member and then believed the zero it had just invented.

**The gap is the naming, not the reading.** A property is a node; it lives in a
container and is a container. Everything the addressing work already built —
`RegisterPath`/`ResolvePath`, the trie, paths that survive renames — stops at
the instance, and the moment it does not, a property answers the same
non-destructive question every instance already answers, with no new verb and
no protocol change.

The same shortfall shows up as the older note in this file about naming at too
low a level: things with no name throw errors that cannot say what they are
about. This is the same sentence from the other end — a thing with no name
cannot be asked about either.

(The test was fixed the local way: a Target is a path, and nothing conjured
starts with a slash. That is a workaround for the probe, not for the gap.)

## Reframed 2026-08-13 — three windows, one running thing

The goal was restated mid-session and it changes the near-term order. Written
up in full as `docs/20260813_1500_three_windows_one_dataflow.md`; the operative
parts:

**`app.js` and the Bridge must be able to serve a REST interface with no GUI in
it.** So the metric is not "how small is app.js" — it is **can something with
no eyes do this?** Anything that only works through the browser is a HOLE, not
a client feature. `alias.object` was one; every gesture still living in the
client is a capability an agent does not have.

**The claim that the GUI is incidental is currently untested**, because there is
exactly one window. That is the argument for building the second translator
early rather than last: it is the instrument, not the reward. It finds holes as
failures to express something, which is unambiguous.

### The near-term order this implies

1. **Properties get paths** — promoted from item 6 below to the spine. A REST
   route IS an address and an MCP tool name IS a name. `GET /Root/Filter/Mode`
   is the namespace answering the question it already answers over a different
   transport; today `Mode` has no address, and the only read (`subscribe`)
   creates what it reads. Same sentence as the older note in this file about
   naming things at too low a level.
2. **One walk, four sinks.** `IsPortableProp` already exists as the shared rule
   for "what does this thing carry", but three traversals implement it —
   `CloneObject`, export/import, and `Bridge_Internals`. They are one walk whose
   sink differs: clone makes nodes with copied values, export makes text with
   copied values, a panel makes controls LINKED to the values, the Interface
   makes names and types with no values. Copy-versus-link is the same axis as
   clone-versus-alias, one level down.
   Consequence: the Bridge's panel layout (`y += mh + 14`, the 14px inset, the
   `PROP_TEXTBOX` floor) is a formatter's concern that leaked into the walk. It
   dissolves rather than relocating. And `GET /Root/Filter` is `NodeToJson`
   restricted to one instance — a narrower path, not a new one.
3. **The REST translator.** Prediction worth recording so being wrong is
   informative: **it should need no per-class code at all** — no `show/rest`
   beside `show/web`. `show/web` exists because a browser needs presentation;
   REST needs none, because the published Interface is already the schema. If it
   turns out to want a per-class half, the Interface is less complete than we
   think.
4. **Cross-translator equivalence tests.** Build the same flow over raw JSON and
   over REST, compare the node trees. Identical trees are the mechanical proof
   that translators are syntax-only. A new category: the existing suites prove
   one translator works, this proves a translator is interchangeable.
5. **The `app.js` split**, demoted to maintenance. It is two files wearing one
   name — a protocol client (`connectSocket`, `send`, `handleEvent`,
   `parseInterface`, the caches of engine facts; ~350 lines, no `document` in
   any of it) and a GUI host whose whole vocabulary is the classes' `show/web`.
   The seam runs through the event handlers: `onPropertyChanged` updates
   `propertyValues` (every client needs it) AND pushes into `liveControls`
   (rendering). Those split rather than move.

### Phase 2 of the presentation work, as far as it went

Appended to `docs/20260812_1545_a_control_brings_its_own_presentation.md`. Three
of seven rows resolved: the alias row by DELETION (188 lines, suite identical
before and after), the `gui*` family to Control, `dropTargetAt` to View.
`web/app.js` 2102 → 1778.

**The test for whether a cut is right, discovered doing it:** ownership says
where something belongs (what is true of every instance of that class); the
class dependency graph says whether it is ALLOWED to live there, and that half
is mechanical. `DependenciesReady` refuses to start a class whose declared
classes are absent, so a call from `led.js` into `control.js` is enforced at
load — while `control.js` may call nothing in another class's js, because
Control declares only the core and cannot declare View without a cycle. The host
needs no declaration of its own: the Bridge assembles `widgets.js` and serves
the client, and the Bridge declares `view.object`. **The Bridge is the host's
dependency declaration.**

That test immediately killed a row: **the drag cannot move to Control**, because
it must ask which view is under the cursor. The seam is at that question — being
dragged is Control's, where it landed is View's — and no call crosses it,
because the browser dispatches document-level pointer events to both files
independently.

**Worth building as a check rather than an argument:** every identifier a
class's `show/web` calls that is defined in ANOTHER class's `show/web` must be
covered by that class's `AddDependency`. The Bridge already walks every class
and reads every `Show/web/js` to build the blob — it is the one place that sees
them all at once. Then "is this the right cut" stops being a judgement call.

### What composition turns out to be

A View with bind-ported properties is indistinguishable from a primitive class
to a caller: published properties, inputs, outputs, a name. So wiring five
objects together and binding three ports **publishes a REST resource and an
agent tool** — no build, no deploy, no schema file, no code. The composite never
learns it was published. "An application is a set of objects plus their wiring"
reaches the network boundary.

And the capability that only exists because all three are windows: connect a
browser and watch what an agent is building in `/Root/mcp`. Instances appear as
it makes them, wires draw as it connects them, values move as data flows —
because `instance-created` from an MCP call is the same event as from a palette
drop, and the canvas cannot tell which happened. Watch it wire something wrong,
fix the wire by hand, and its next query sees the correction. Two peers standing
next to one running thing, not agent-with-human-review.

### Data objects that carry a shape — the walk belongs to the kind

Sharpens "Nor does a value have to be a scalar" above (Phase 8), which already
called for topology nodes — a table, a graph, a linked list, a document — held
as walkable structure rather than an opaque payload. What was missing was the
contract that makes them uniform, and it is one thing: **the kind defines its
own walk.**

**A DataObj answers a fixed set of questions.** What am I; give me as
string/int/real; set me from one; copy me; free me; serialize me; walk me.
Scalar, table, list, graph — the same set. Then clone, export, the panel walk
and the Interface all keep working untouched, because none of them ever asks
what kind a value is. An `if (type == TABLE)` anywhere outside `data.c` means
the design leaked.

**Containment costs nothing new.** A node's value slot IS a DataObj. If a
composite's cell slot is also a DataObj, then a table of graphs and a node tree
of tables are the same fact stated twice. Messages are free for the same
reason: a payload is a data node, so sending a whole table is the existing
`SndMsg` under the existing ownership rule.

**The conversion matrix extends by one rule per kind.** Today it is kind-to-kind
across five scalars. A composite read as a string has to answer something, and
read as a real has to answer something; whatever is chosen is declared the same
way the scalar matrix is, and `DataTest` prints the extended matrix as its
proof.

**Type and widget become two axes.** `Renders` maps widget→class today. A table
property wants a grid, a chart or a tree depending on what the view is for, so
the property declares its type and its widget separately, and
`FindClassRendering` stays the same walk asking the same question.

**The walk resolves the density question.** Node-shaped storage (a node per row,
a node per cell) buys one walker, one serializer, and a subscribable cell for
free; a compact representation is what a million-row table wants. If the walk
comes from the kind, the storage is nobody else's business and both hand back
the same iterator. It also makes cycles safe without anyone else's help: a
cyclic graph knows it is cyclic, so ITS walk carries the visited set, and the
serializer never needs cycle detection, because that was never the walker's
problem.

**A kind can offer more than one walk** — a table by row, by column or by a
selection; a tree pre-order or post-order. The walk is named and the property is
a node, so which walk a sink uses is another property on it: the panel walks one
way and the REST translator another, over the same table, with no copy.

**Serializing is the walk with a text sink** — the same one the flow file, the
bridge and REST already are, nothing per-kind on the writing side. The one thing
it adds is IDENTITY: a cyclic graph cannot be written as pure nesting, so the
walk must say "you have seen this one, here is its handle" and the reader must
bind handles back. That is already solved one level up — export writes internal
links relative, import is a clone-drop that rebinds them — and the same problem
arriving twice with the same answer is a good sign the answer is right. The
payoff is one representation serving every mouth: a table in a flow file, over
the bridge, as a REST body, as an MCP tool result.

### Changing representation is a functor

A mapping between two representations maps the ARROWS, not just the values. A
table→tree mapping carries the walk across, so the mapped thing is walkable,
subscribable and serializable without being materialized: a view, not a copy,
costing nothing until someone reads it.

- **The scalar matrix is the degenerate case** — five kinds, twenty-five
  hardcoded arrows in `data.c`. Generalized, the arrows are declared and looked
  up the way `Renders` is.
- **They compose, so it is not N².** Declare the natural arrows and get the rest
  by composition; table→tree→JSON is two declarations, not a third.
- **A mapping is an object** — an input, an output, properties — so it is a
  palette class and it wires like anything else, which also means a mapping can
  be SCRIPTED. A user-defined representation change costs a ScriptBox, not a
  rebuild.

**The law that keeps it honest, and it is testable:** map across and back is
identity, and mapping commutes with the walk — walk-then-map equals
map-then-walk. One harness test written once, run against every declared pair.

This is the shape-to-shape half of the mapping described under Phase 8: with it,
`presentation/<surface>` carries only what is genuinely presentational, and a
REST resource, a SQL row, an MCP schema or an RDF triple is a topology the graph
holds directly instead of flattening into text and hoping the far end parses it
back the same way.
