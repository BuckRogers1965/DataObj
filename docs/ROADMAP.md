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

### 2a. A doorway must OPEN what it points at

**The label half of this is struck (2026-08-13, evening).** It said a doorway
must wear the name of what it opens. It must not: a control wears its OWN name,
like every other instance, and if you want the caption to read something else
you rename the control. The deleted client rendering synthesised the target's
name (`lb.textContent = rec.label || baseName(rec.target)`), and putting that
back would be a labelling rule for one kind of thing — the species this whole
piece of work removed. What made the captions unreadable was never the label
rule, it was the NAMES: every panel row was called `Alias_N`. The bridge now
names each row for the property it stands for, so the caption says something
useful with no rule about doorways at all. `guitest: rename-cascade` asserts the
uniform behaviour instead — the doorway keeps its own name through a rename.

**What remains is the CLICK, and it is still unasserted.** The other line of
that deleted rendering was:

```js
ic.addEventListener('click', () => panels[rec.target].setOpen(true));
```

A View opens its own panel, so a control standing for a thing's
`ReservedViewOpen` opens an empty panel of its own instead of the thing's.
The control's `Value` IS a link to the target's `ReservedViewOpen`, so writing
its own `Value` opens the target through the link and nothing has to know why.
That needs a test before it needs a fix.

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

### Found 2026-08-13 (evening) — one clone path, and a mark on everything

**The clone had two paths and they had drifted.** `CloneGroupPass` asked
`IsAlias` and sent linked members to `CloneAliasNode` and everything else to
`CloneObject`. In a compiled widget's panel EVERY member is a control pointing
at data, so the branch decided nothing - it only picked which of two
implementations of "copy this instance" ran. And they disagreed:
`CloneObject` walked every portable property (`IsPortableProp`), while
`CloneAliasNode` carried a hand-written list of four names - `Widget`, `Label`,
`X`, `Y`. So a cloned control arrived without its `W`/`H` (default-sized boxes)
and without its `LabelPos` (captions back on things that hide them), and
anything added to a control later would have gone missing the same silent way.
Worse, `CloneAliasNode` required a `TargetProp` string that only the alias
gesture writes, so a widget's controls - linked by `Widget_Ctl`, which records
nothing - failed outright: cloning a TCPPort produced its sub-views with every
control inside them missing, 50 `FAILED` lines in the log, and a clone verb
that still reported success.

Now: pass 0 clones every member the same way, pass 1 re-makes the links every
member carried (against the map, so they point at the copies), pass 2 wires.
`CloneAliasNode` is deleted. Two consequences beyond the bug: `CloneObject`
skips link slots instead of copying the string a link currently reads (the
hazard its own comment warned about, now handled), and relinking walks EVERY
property rather than just `Value`, so a composite whose own property is bound
to an inner member's comes across bound.

**Test:** `viewclonetest.py: test_clone_compiled_widget` clones a palette
TCPPort and checks the copy holds every member at every depth, that the
sub-views brought their controls, AND that each control was ANNOUNCED - the
engine holding a control a client was never told about is an empty panel in
every window, and the two fail separately.

**The general lesson, and why the suite stayed green:** the failure was loud
(50 log lines) and not fatal (the verb reported success), and nothing fails a
run for what is in the log. `CloneAliasNode` returning NULL should be an
`ERROR`, and `run.sh` should fail a variant when an `ERROR` line appears in the
server log. Then the next silent partial-success announces itself in every
suite at once instead of waiting for someone to clone the right thing in a
browser.

### Target is a mark on everything, which is no mark at all

`Target`/`TargetProp` are the WRITTEN-DOWN copy of a link - the pointer
recorded as text because a `.flow` file cannot carry a pointer. The live fact is
the link itself, which `IsAlias` reads directly.

Commenting out the engine's write (`AliasProperty`) changed nothing observable
in the browser, because the bridge computes it anyway in both paths that make
one: `Bridge_CreateAlias` resolves the port and writes the SESSION path, and
`Bridge_Internals` writes the instance's alias for every panel row. So the fact
is written twice, in two different naming schemes, with the translator's version
winning - which is the one that reaches the flow file.

**The harness then failed six checks and settled it: the engine's write is NOT
redundant.** A `.flow` carries `Target`, and the serializer reads it from the
FILE to recreate the alias - but the instance it creates gets its `Target` from
`AliasProperty`, and nothing else. The bridge's writes happen on ITS create
paths only; import goes through neither. So with the engine silent, an imported
or loaded alias worked as a link and could no longer say what it pointed at.

Which answers the ownership question properly: **`Target` belongs to the
engine**, because a serializer has to export and import with no bridge loaded
at all - a headless host that never links a translator still saves and restores
flows. The writes to question are the BRIDGE's, which overwrite an engine path
with a session alias, and the repath job that exists to keep that copy in step.

And every control that points at data IS a target - a widget's panel control
stands for its widget's property exactly the way a dropped alias stands for
its target. Marking all of them is marking none of them. The asymmetry today is
the other way round from how it first looked: `Widget_Ctl` records nothing, so
those controls are the ones missing the mark, and a clone GAINS one because the
bridge stamps it on announce.

Direction (not scheduled): the serializer asks the link at save time instead of
reading a stored duplicate - `IsAlias` already hands back both the target
instance and the property name. That retires the bridge's repath-the-string job
(`Bridge_RepathSubtree`, keeping the copy in step through renames) and both
fallbacks, and leaves one source of truth with a text rendering produced where
text is needed. Same shape as the walk belonging to the kind: the fact lives in
one place, and whoever needs it in their own notation derives it.

### An object must not act until something asks it to (found 2026-08-13)

A palette UDPPort arms a task the moment it is born and runs a settle pass at
boot:

```
UDPPort[]: arming setup in 300ms (born)
UDPPort[UDPPort]: activate: building panel
UDPPort[UDPPort]: setup (settle): stopping the object
UDPPort[UDPPort]: setup (settle): forced lamps to stopped
```

Nothing asked it to. It is sitting in the palette as a thing you might drag,
and it is already scheduling work, driving its own lamps, and deciding not to
open a socket. **It should do nothing until an event reaches it** - an arriving
message, a property write, an explicit activate.

The standing rule this breaks: a control never acts at creation. Being created
is not an instruction.

**This is a safety property, not a tidiness one**, and the line is whether the
write LEAVES the object.

Setting your own lamps at creation is right: an object that is not running
should say so, and initialising `On=0 Off=1` is a description of itself, kept
consistent and then corrected as it actually runs and stops, enables and
disables. That is state, not action.

Arming a task is action. So is opening a socket, and so is commanding an axis -
and it is the same lifecycle for all three. The settle pass above decided to
STOP; with `AutoStart=1` in a saved flow the identical path decides to OPEN.
Which way it goes is a property value, not a guarantee, so "it happened to be
safe" is the only thing standing between existing and moving. If that instance
were a robot arm, being loaded into a palette would have commanded it.

Nothing that acts on the world may act because it was constructed. Construction
establishes what a thing IS - including what its lamps say about it; only a
message says to DO. An object that cannot tell the difference cannot be trusted
with anything physical, and this framework is aimed squarely at things that are
physical.

The cost is also structural: a palette full of objects that each arm a task at
boot is a program that cannot go quiet, and quiescence is how this framework
decides it is finished.

Sightings of the same shape in the same boot, worth checking when this is
picked up rather than fixed piecemeal:

```
reader.c 160  Error  Reader has no Filename to read.
writer.c 188  Error  Writer has no Filename to write.
```

Both are palette instances complaining about configuration nobody has given
them yet, because they were activated rather than left alone. An object with
nothing to do should be silent, not apologetic.

The check that makes this visible rather than a matter of reading logs: at
boot, with a palette and no flow, the task list should quiesce - nothing armed,
nothing rescheduling. That is a test the harness can make (count the tasks
after settle), and it is the same measurement the leak suite already knows how
to take.

### IPv6 in the resolver (found 2026-08-13)

The DNS engine (`objects/dns/dns.c`) resolves with `gethostbyname()`, which is
what makes it behave exactly like the host it runs on - intranet short names,
the search domains in `/etc/resolv.conf`, `/etc/hosts`, whatever the local
server serves. That is the wanted behaviour and stays.

What it cannot do is IPv6: `gethostbyname` asks for A records, so a host that
exists only as AAAA does not resolve, and the answer is always a dotted quad.
The engine stores a `struct sockaddr_in`, so this is not only a call to swap -
the address the entry carries has to widen too (`sockaddr_storage`), and
`DnsGetIPAddr`'s `inet_ntoa` becomes `inet_ntop`.

`getaddrinfo` is the replacement, and it also answers the "which family" and
"which server" questions the current path cannot: it returns a list, so a
caller can be told every address a name has rather than the first A record.

Nothing above the engine changes. `dns.h` passes addresses as text, and the
Resolver widget shows text, so a longer string in the same property is the
whole visible difference.

### Deleting a subtree: close the sources, don't detect the strays

Found while building the DNS object (2026-08-13), which is the first thing in
the tree with a worker thread and therefore the first that can be answered
after the asker is gone.

The general problem is not DNS's: a message can be in flight when either end
dies. Today two special cases handle two paths - `ScrubRegistrySubscriptions`
strips Subscriber records pointing at a dead instance, and `CancelPendingSends`
blanks queued envelopes it sent. Neither covers messages queued FOR it, and
neither is reached by anything an object generates on its own.

**The protocol, in this order, for the whole subtree at once:**

1. **Walk the subtree once** to get the set being deleted.
2. **Sweep the message list against the SET** - one pass over the queue testing
   membership, not one pass per instance. Everything already queued for
   anything in the set dies here.
3. **Cut the subscriptions** - nothing can route a NEW message to them.
4. **Each instance releases its private handles** in `InstanceEnd` - the engines
   stop, cancel their own queued work, and generate nothing further.

After those four, nothing can produce a message aimed at the set, and nothing
already produced survives.

**No tombstones, no countdowns, no generation stamps.** Those are what you need
when the exposure can only be DETECTED; this closes it instead. The property
that makes it exact is the single thread: the teardown runs to completion in one
turn, so nothing slips in between step 3 and step 4. In a threaded fabric this
would have to become a grace period (RCU's answer) or a generational handle
(what game engines do with reused slots) - worth knowing as the reason the
single-threaded rule earns its keep.

**What the protocol cannot reach**, and where it therefore delegates: work
living outside the engine's own structures - a worker thread that will produce
a message later. No walk finds it, because the engine does not hold it. Step 4
is where that is handled, by the private handle that started it and is the only
thing that knows it exists. In the DNS object that is the pending list (queued
lookups, retracted at teardown) and the live-instance ring (the one lookup
already running, which cannot be cancelled but whose answer lands nowhere).

**Why the residue stays small: engines hand out nothing.** `udp.h`, `tcp.h` and
`dns.h` expose a handle you cannot inspect and a set of message ids. No nodes,
no structs, no pointers into the engine cross that line, so there is no external
reference that can go stale - the same conclusion the language hosts reached
from the other direction with their opaque handles. Three subsystems, one
answer, and it is what keeps deletion a walk rather than a search.

## Found 2026-08-15 - a clone's Help panel comes up empty

A cloned widget opens its Help panel and there is no text in it. The source
widget's Help works; the clone's does not.

**What is established.** `HelpFile` is written with `SetPropStr`, so it is a
STRING, so `IsPortableProp` passes it and the clone carries it. The handler is
not so lucky: `Widget_AddHelp` stamps `SetPropLong(openPort, "OnMsg",
(long)Widget_OnHelpOpen)` onto the `ReservedViewOpen` property NODE, and
`IsPortableProp` rejects LONG values - correctly, since a function pointer is
not data. So a clone has the file and no handler unless something re-stamps it.

Something is supposed to. `CloneObject` calls the class's `InstanceStart`,
which arms `Widget_DeferBuild`; when that task fires, `Widget_AddHelp` runs and
`Widget_SubPanel` re-stamps the handler on whatever `Help` view it finds. But
`CloneInstance` runs its three `CloneGroupPass` passes SYNCHRONOUSLY, before
that task fires - so the passes have already cloned the source's own `Help`
view by the time the build tries to adopt one. If the cloned member did not
land on the name `Help` (a minted `Help_1`, say), adoption misses, a second
empty Help view is created, and the one on screen is the one with no handler.

**The general rule this is an instance of, which is the part worth fixing.** A
handler stamped on a property node is not portable, by design. Therefore
anything whose behaviour depends on a stamped handler must re-establish it
after a clone, and "re-establish" means adopting the cloned member rather than
racing it. This is the same family as the widget clone that lost its controls
(2026-08-13): the structure survives the copy and the behaviour does not, and
the structure is what tests were looking at.

**The test gap, which is why this got through.** Nothing in the harness touches
Help - not one line in any suite. And the clone tests assert STRUCTURE: that
the copy holds every member at every depth. A clone can pass all of that with
every handler missing. The test to add is behavioural: clone a widget, open the
clone's Help, assert text arrives - because that assertion passes only if the
whole chain survived the copy, including the parts that are function pointers.

Worth writing the general form of that test once and applying it to every
stamped-handler behaviour, not just Help.

## Found 2026-08-15 - the client stacks by arrival order

`registerWidgetAtom` and `registerView` both call `toTop(el)` as they render,
and `toTop` is `zIndex = ++topZ` off a counter that only counts up. So **z-order
in the browser is the order `instance-created` events arrived**, and nothing
else. Not position, not class, not anything the engine states.

That makes the order the engine happens to announce things in into a rendering
decision. Reparenting the class registry changed the instance walk from library
order to type-tree order, which changed announcement order, which restacked the
palette - a large control ended up in front of ones it had always sat behind and
started swallowing clicks aimed at them. Five guitest failures, all of the form
"timed out waiting for <subject>", because each test takes an element's centre
and clicks that point: the pick landed on whatever was on top.

**Done now, engine side:** `Bridge_ListInstances` sorts a container's members by
name before announcing them, so arrival order is a stated decision rather than
an artefact of the registry's shape. That stops the leak; it does not fix the
cause.

**The actual fix, client side.** Stacking should not come from arrival order at
all. This is the `toTop` item already listed above - the counter starts at 100,
never resets, and the wire layers sit at a fixed 100000 to stay above it, which
is a ceiling rather than a guarantee. Renumbering the stack down to its real
depth on each promotion fixes the unbounded growth; deciding stacking from
something the engine states (or from nothing, for members laid out in a
container) fixes this.

**And the test is fragile, which is the third thing.** A test that computes an
element's centre and clicks the coordinate is asserting about the whole page,
not about the element it named - anything overlapping silently redirects the
gesture, and the failure surfaces later as a timeout with no mention of what was
actually hit. The gesture helpers should verify that the point they are about to
click resolves to the element they meant (`elementFromPoint`), and say so when
it does not. That one change turns five mystery timeouts into one accurate
message.

## Planned - four bounded bands, and no counter

The `toTop` item above has a shape now, arrived at while working out what an
embedded View needs (see the "two chains and a table" post). **Z-order is
depth, not arrival, and every band is bounded:**

    structural depth    containment depth - free from the DOM
    floating panels     bounded by how many are open
    overlay             popups, menus, tooltips
    modal               a scrim, and the dialog above it

**Embedding retires the counter for everything it touches.** A DOM child
paints above its parent with no `z-index` at all, so once a View can render
*in place* inside its parent panel rather than as a root-level peer, the
depth rule costs nothing to implement - it is what the browser already does.
The counter exists today only because every panel is a flat sibling and
nothing else orders them. What is left after that is raise-to-front among
floating peers, which renumbers down to actual depth on each promotion
instead of counting up.

**The bug this already causes, today.** `.flow-dialog-overlay` is pinned at
`z-index: 300` (web/style.css) while `topZ` starts at 100 and only ever
increments - once a session has rendered and raised its way past 200, **the
file selector on Save / Load / Import renders BELOW open panels.** The scrim
dims them correctly and they paint straight over the dialog anyway. That is
not a dialog bug; it is the unbounded counter arriving at its first victim,
exactly as the TODO predicted.

**The modal band is the fix, and it is half-built.** The scrim already
exists - `position: fixed`, `inset: 0`, `rgba(0,0,0,0.45)`. What it needs is
to own the top band outright and to swallow pointer events, so that while a
dialog is up nothing below it can be raised at all. Then "below the panels"
is not reachable by any sequence of clicks.

**The overlay band answers popup escape.** A Dropdown in a table cell has to
open over the cell and over the panel edge, and CSS stacking contexts make
that unwinnable from inside: an ancestor with a `transform`, a `filter`, or
`opacity` below 1 traps its descendants and no `z-index` lifts them out.
Rendering transient things - a dropdown's list, a menu, a tooltip - into a
shared overlay layer sidesteps the whole class. Decide it before three
widgets depend on the other answer.

**A modal here is a View with a flag** (`ReservedViewModal`, beside the
`ReservedViewEmbedded` the embedded rendering needs), and it is worth naming
what it is *not*: in a conventional toolkit a modal spins a nested event
loop, and that re-entrancy is a genus of bugs on its own. There is one loop
here, always. The scrim blocks the pointer, not the fabric - messages keep
flowing and tasks keep firing behind the dialog. And because modality is
per-connection presentation state rather than engine state, a dialog open in
one browser does not freeze another session on the same instances. Both fall
out of the GUI being a projection rather than the program.

**Order of work:** the bands are a prerequisite for the embedded View, which
is itself the prerequisite for a TableView. The file-selector bug is
independently worth fixing first, because it is live and it gets worse with
session length.


## Planned - a theme is an instance, not a stylesheet

Stated plainly because it is the honest reason it is on the list: **if this
looked good, far more people would look at it.** Architecture does not
survive first contact with a screenshot. Someone decides in about a second
whether the thing in the picture is worth reading about, and nothing else on
this roadmap has a comparable interest-per-hour return.

**A theme is an ordinary instance whose properties are the tokens.** Colors,
fonts, sizes, radii, spacing - each an ordinary property on a `Theme`
instance living in a container. That buys the whole deployment story for
free, because it is the same one everything else here has: a theme is
addressable, subscribable, clonable, savable, exportable, and mailable as a
flow fragment. Nobody edits CSS to reskin a session.

**Restyling is live, because a property write fans out.** Wire a control to
a theme token and the whole session restyles as the value changes - no
reload, no rebuild, no regenerated stylesheet. That is not a feature to
build; it is what property writes already do.

**The mechanical prerequisite, measured.** There are **52 hardcoded hex
colors in `web/style.css` and 40 more across the controls' own
`show/web/*.css`, and exactly zero `var(--...)` anywhere.** Converting those
literals to custom properties is the actual work, and it is mechanical.
**Do it before the z-order/embedded-view work touches the same files**, or
it gets done twice.

**Fonts belong to the theme, and by role.** Today there are four
`font-family` declarations in four places - `web/style.css` twice, plus
`textbox.css` and `markdown.css` each independently choosing their own
monospace stack. Those are exactly the roles a theme should name: UI text,
data/monospace, labels, headings, each with size and weight. Right now they
disagree in four files with no way to change them together.

One constraint worth stating so nobody reaches for a CDN: the page is served
by the framework's own Http object, so either the theme names system font
stacks (free, zero bytes, and what is there now) or it ships a font file
that Http serves off disk like any other asset. There is no third option and
that is fine.

**Per-session, without new machinery.** A theme is an instance and a session
points at one by path - one browser on `/Root/Themes/Dark`, another on
`/Root/Themes/Light`, same engine, same instances. Nothing per-connection
has to be stored but a path, and `prefers-color-scheme` can choose the
default on first connect.

**Not to be confused with Skin.** `objects/skin` is a class's default
*layout* - one row per published property, generated from the interface and
overridable from a file. That is position. A theme is appearance. A control
gets where it sits from one and how it looks from the other, and merging
them would recreate exactly the fusion this design keeps taking apart.


## Planned - break from the desktop, lay out the way the web does

**Nearly no web site positions anything by pixel.** Absolute X/Y for every
element is a desktop-toolkit inheritance, and this framework carries it
because that is where its lineage is. It is worth noticing that the medium
we actually run in has spent thirty years building the other thing, and that
fighting it costs us its single best property: **content that lays out
correctly at any font, any size, any density, and any language.**

That is what "infinite themeability" means, and it is not reachable from
pixel layout. A hand-placed panel can be themed only within the slack
somebody padded into it - which is precisely why the standing rules are
"pad the panel +40" and "pad 50". Those numbers are error bars on a human
estimate of something a layout container computes exactly.

### Two kinds of container, and the boundary between them

**Canvas.** X/Y is engine state - shared, saved, dragged, wired against.
The Root view and any flow diagram are canvases forever. Positions here are
facts, so every client must agree on them, and scaling has to be a uniform
transform rather than a re-layout.

**Layout.** `Stack`, `Row`, `Grid`. **No X/Y is stored at all.** Position is
a function of order, rule, and font, computed fresh in each client.

The second half is the realisation that makes the whole thing safe:
**nobody has to agree on an X/Y that was never asserted.** Two sessions at
different font sizes render different pixel positions inside a layout
container and neither is wrong, because neither position is data. So
per-session theming - already the plan for colors and fonts - extends to
scale and density for free, with nothing to synchronise and nothing to
conflict.

### The rule that survives unchanged

**Sized by structure, at build time: fine.** Four stacked rows are four rows
tall the moment the panel is built, identically in every client running the
same font. Deterministic and repeatable.

**Resized by content, at runtime: still no.** A Textbox does not grow
because somebody typed a long line. A label does not shove its neighbours
sideways because a value got wider. Content scrolls inside its declared box,
exactly as now. A UI that twitches while data moves through it was always
the thing being prevented, and none of this relaxes it.

The case between them is a layout container whose *members* change at
runtime - a group gaining a checkbox, a table gaining rows. That is
structural, so it re-lays-out legitimately, and it needs a ceiling or a
table grows a million rows tall: the container declares its maximum extent
and scrolls past it.

### It is half-written already

`objects/skin` generates "one row per published property, stacked
`SKIN_ROW_HEIGHT` apart, in the order it was published." **That is a stack
layout.** It is simply pre-computed into pixel offsets instead of expressed
as a rule the browser executes - which is also why it cannot follow a theme.

What changes for a widget author: a panel table entry declares its place in
a sequence rather than a coordinate, for containers that opt in. Today every
widget hand-computes X/Y for its controls in C - the same layout algorithm
written thirty times - and "make the panel bigger" means editing every
offset in it.

### Scale on the canvas side

Canvas containers keep authoritative pixel coordinates, so a theme's font
size cannot re-place them. It applies as a **uniform scale**: declare
geometry in font units so a 300x200 panel at 14px becomes 343x229 at 16px
with everything inside scaling by the same factor. Nothing reflows, nothing
moves relative to anything else, the declared size is still obeyed - only
the unit under it tracks the theme. That is what OS-level zoom does, and it
is why it is an accessibility feature rather than a layout mode.

### Cost and order

Three properties on View cover all of it - embedded, modal, and how it
places its members - and **there is no engine change under any of them.**

Order: theme tokens first (the 92 literals), then the z-order bands, then
embedded views, then placement modes, then TableView. Each one is a
prerequisite for the next, and the first three all edit the same CSS.


## Planned - the flow owns the icons, the device owns the panels

Right now some presentation state shares between clients and some does not,
and the split was decided per property inside its own branch rather than by
a rule. `ReservedViewOpen` is deliberately per-window with a stored default
(app.js:824 - *"Open's stored value is the initial presentation only - after
first paint, open/closed is this window's own business"*), while
`ReservedViewPanelX`/`PanelY` five lines below apply on every change and are
therefore live-shared. Those two are the same kind of thing and disagree.

**The rule: the engine holds what the flow IS, the device holds how it is
being looked at.**

| flow - shared live, saved, exported | device - owned per device, restored per device |
|---|---|
| instances, values, wiring | which panels are open |
| names, containment | where each panel sits on screen |
| **an icon's X/Y in its view** | scroll position, interaction mode |
| a control's X/Y/W/H in its panel | cursor position, theme and scale |

So **icons move live** - drag one on the desktop and it moves on the pad,
because where a thing sits in a view is part of the flow and always was
(it saves, it exports, and the round-trip test asserts on it). **Panels do
not** - your desktop keeps its arrangement and your pad keeps its own, each
restored to whatever you last had open there.

**A device profile is an ordinary instance**, in your own tree, at something
like `/Root/Devices/pad`: one entry per panel holding open/x/y. Which means
it is addressable, savable, clonable and aliasable, and three separate
features fall out of that rather than being built:

- **Multi-device work.** A pad beside the monitor holding messaging or a
  monitoring panel, while the desktop is on a different task entirely. One
  instance, two arrangements, no sync.
- **Fast task switching.** Named arrangements on the same device - "monitor"
  and "build" - and switching is loading a different profile. Virtual
  desktops, as data, with nothing new underneath.
- **Support sessions.** A second connection adopts your profile and sees
  what you see.

**Mirror versus follow is alias versus clone** - the same two operations the
share model already defines. Support *aliasing* your profile is a live
mirror: you both move each other's panels. Support *cloning* it is "start
where you are, then diverge." Default to the clone; make the mirror
explicit, because most of the time the helper wants to poke at something
without dragging your windows around.

**And support controlling anything is a real permission**, not a view
setting: it is a full client on your engine, which has no per-connection
restrictions, so it is an authentication decision at the launcher - explicit,
time-boxed, and revocable. With always-on instances, revoking is dropping
the connection.

### Two collaboration shapes, and they are not the same permission

**A shared workspace** is peers in a common view, each placing their own
nodes and cloning each other's out. Single-writer still holds, because it is
per node and not per container - I write mine, you write yours, and exchange
is a clone. Authorization is "who may place things in this view," and it
belongs on the **connection** at connect time rather than as a list stored
on the node: enforcement at the translator, same as everywhere else.

**Remote assist** is a different thing entirely - not sharing a node but
delegating a session, so the helper acts as you, in your engine. Windows
Remote Assistance is the right reference because of its four properties,
and every one of them already has a mechanism here:

- **the user invites; support cannot just connect** - the invitation IS a
  one-time key, the same object as the reconnect ticket
- **the user sees everything that happens** - both connections are on one
  engine, so effects are visible by construction
- **the user can end it instantly** - drop the connection; the always-on
  instance is unaffected
- **it is a session, not a standing grant** - TTL on the ticket, plus a
  scheduled task that disconnects

**Pointing is a published value, so it needs no permission.** Each
connection publishes its cursor - read only to everyone else, because your
pointer is yours to write - and others alias it and render a labelled
cursor. Publish, alias, subscribe: nothing new. The property that matters
is the separation it creates: in every screen-sharing tool, pointing at
something requires taking control. Here "let me show you" costs nothing and
"let me fix it" is a separate decision.

**Presence falls out of it.** The pointer publishes which VIEW it is in, so
a helper who wanders off to look at something else simply stops rendering on
your screen, and reappears when they come back. No presence protocol, no
join/leave events - a value whose scope stopped matching yours.

**Point at the node, not the pixel.** This is the part to get right at the
start, and it is the thing screen sharing cannot do at all. With per-device
panels, per-device theming and per-device scale, a raw (x,y) is meaningless
across devices. Everything here is addressable, so a pointer publishes what
it is OVER - `/Root/Sheet/Total` - and each device highlights that node
wherever it happens to have drawn it. The phone, the pad and the desktop all
light up the right thing while agreeing on nothing about layout.

**And it beats Remote Assistance rather than imitating it: RA shares pixels,
this shares the engine.** No pointer contention. The helper has their own
device profile, their own open panels, their own cursor, so both people work
at once instead of wrestling over one mouse. Which is where mirror-versus-
follow earns its keep - alias the profile when they should be looking at
what you are pointing at, clone it when they should fix one panel while you
carry on in another.


## Planned - one root per user, and a share folder

The three entries above worry about two sessions agreeing on the same
instances. **Give each user their own session, their own root, and their own
saved directory, and most of that becomes moot** - nobody is co-observing a
canvas, so there is no geometry to agree on, no cross-session conflict, and
per-session theming and scale stop needing any justification at all. The
per-connection dirty set stays, but as a rate limiter, which is all it was
ever for.

**Process per user is affordable here, and almost nowhere else.** The
library is 128 KB, an object is 20 KB, and an idle instance stops scheduling
once its last client detaches and the flush task stops re-arming - so an
unattended tenant costs its footprint and nothing else. A Node or JVM tenant
starts at 50-200 MB before it does anything. This design can afford the
isolation model that everyone else has to simulate, and it should take it:
there is no security boundary between objects inside a process, so the
process IS the boundary, and one per user is the honest answer rather than a
deployment preference.

### Spawn on login, hand over the port

The mechanism is inetd's, and it suits this framework better than it suits
anything modern, because the thing being spawned is 128 KB and knows how to
stop. A listener accepts the connection, authenticates, spawns that user's
engine with their own cwd - which is also their scan path and their saved
directory - and hands the socket over.

**The one code change is in the TCP object**: it listens-and-accepts, and it
client-connects, and it needs a third mode - **adopt an already-connected
fd**. Skip `socket`/`bind`/`listen`/`accept` and start from a descriptor
handed in. That covers both handoffs: `fork`+`exec` with the fd inherited
for a fresh session, and `SCM_RIGHTS` over a unix socket for a user whose
engine is already running when they reconnect.

**The listener is an object, not host code.** Features never go in main.c,
so the accept/authenticate/spawn path is a flow like everything else, and
the share engine is simply one more process the user engines bridge to. N
user processes plus a share process, all speaking the protocol they already
speak. Authentication happens before the handoff, in the launcher, because
it is the one thing that cannot live inside what it protects.

**The cwd is the whole handoff.** `chdir(userdir); exec framework` and there
is no user id to pass, no config to look up and no tenant table, because the
working directory already does all three jobs: it is the scan path
(`InstallObjects` is `ScanDir(".", ".object", ...)`, cwd-relative by
design), it is the saved directory (flows, and the help READMEs objects read
relative to cwd), and it is where the session file is picked up at boot. The
process's directory IS the user as far as the engine can tell.

Two things fall out unbuilt. **Per-user object sets**: a user can hold
objects nobody else has, which sharpens the shared-native-code fork above -
a shared `.object` lands in one directory and its blast radius is one user.
And **this is the third governing principle as a deployment model**:
shipping a different product means different objects and a different flow,
never a different binary - so the binary is identical for every user and the
directory is the entire difference.

There is already a working prototype: `run.sh` runs five build variants at
once, each in its own directory with its own build, its own `.object` files,
its own flows and its own port offset. That is the multi-tenant model,
exercised on every test run. The only change is that a tenant becomes a
person instead of a compiler flag.

**Where TLS terminates is the fork.** A TLS session's state cannot be handed
to a child along with the descriptor, so "no crypto in the engine" and
"encrypted past the handoff" cannot both be had from a plain fd pass. Two
shapes, and they are not exclusive:

1. **The launcher terminates and proxies** plaintext to the child over a
   unix socket. Crypto stays entirely in the launcher, the engine never
   links a TLS library, the browser's origin never changes, and no per-user
   port is exposed. One local hop, which is nothing at these message sizes.
2. **TLS as an object.** A transport is just an object here - WebSocket
   already layers over TCP by sniffing the first message, and TLS layers the
   same way. The crypto lives in `tls.object`, loaded only when a flow uses
   it, so `libframework.so` stays 128 KB.

**Build (2) regardless, because outbound needs it**: Ollama, ComfyUI, REST
and any remote MCP service will want HTTPS, and there is no TLS anywhere in
the tree today. Then let the launcher use (1), which is the simpler inbound
story.

**No cookies, and that is a feature.** Session state lives in the engine,
not the browser, so the usual traps do not apply - and the absence buys
more than it costs. **No ambient credentials means no CSRF class at all.**
The connection IS the session: a persistent WebSocket plus a per-user
process means identity is established once at spawn and never re-asserted,
so there is no session id because there is nothing to multiplex. And with
mutual TLS the key lives in the browser's certificate store rather than the
page's, so a refresh loses nothing because nothing was there - which is the
"GUI is a projector" rule showing up as a security property.

(For the record, since it constrains any future variant: a per-user PORT
would not have been an isolation boundary anyway. Cookies ignore port, so
`host:8083` and `host:41823` are one origin to them, and a browser
redirected to a high port does not survive corporate networks or NAT.)

**Reconnect uses a one-time key, delivered inside TLS.** The launcher
authenticates, then redirects the client with a single-use ticket that the
engine consumes on the WebSocket upgrade. Three details decide whether it
actually holds:

- **Not in the query string.** A redirect URL leaks through browser history,
  the `Referer` header on any later outbound request, and access logs.
  Single-use defeats replay but not observation - anyone who sees the key can
  race the legitimate client and win. Put it in the URL **fragment**, which
  is never sent to a server and never logged, and `history.replaceState` it
  away on read. An auto-submitting POST does the same with the key in a body.
- **TTL in seconds, not minutes.** The key's entire job is to authenticate
  ONE upgrade, because after that the connection is the session. Small job,
  small window - and it is small because of the persistent-connection design.
- **Atomic consumption**, which is free: one launcher process, single
  threaded, so "mark used before acting" has no race to lose.

**The first connection needs no key at all.** With the fd handoff above, the
child receives a socket that is already connected and already authenticated.
The ticket is only for *re*connecting to an already-running engine, minted
fresh each time - which is another argument for launcher-as-proxy over a
per-user port: one TLS endpoint, one certificate, and the key never leaves
the encrypted channel to reach a differently-secured port.

**The instance is long-lived, not a login session.** It keeps running when
you log out - your flows keep flowing, your ports keep listening, your
shares keep serving - and you reconnect a browser to look at it. On a
hypervisor it can migrate between hosts with its connections intact, which
is cheap here for the same reason everything else is: live migration cost is
dirty pages, and a 128 KB engine with a few MB of node tree moves in
nothing. A 4 GB JVM heap does not.

**Which means quiescence was already right, and the browser was never what
held it up.** The process persists exactly as long as it has work. A
listening TCP object has a poll task; a running flow has tasks; a subscribed
share has a connection. An empty workspace exiting costs nothing, because
the session file is on disk. The flush task stopping when the last client
detaches remains correct - it stops the viewer's work, not the user's. No
grace period is needed.

**And always-on is a requirement, not a preference, for the directory case**
(see the org-chart worked example): a live status board only works if
everyone's instance is up while they are asleep. Each person is a peer
serving their own profile, not a row in somebody's database.

**Two consequences that follow.** *Polling cost bounds tenant density* -
nothing here is interrupt driven, so a listening socket with no traffic
still wakes its poll task, and a thousand idle instances waking to check
quiet sockets is measurable CPU. The fix is the existing idiom: an idle
listener re-arms with a longer delay and backs off, snapping to its fast
cadence on the first byte. That is the one place "everything is polled" has
a price at scale, and it wants measuring before any density number is
promised. *And hot reload stops being a convenience* - a 24/7 instance
cannot be restarted to upgrade it, so the half-built machinery (`_fini` ->
`UnregisterLibrary`, `UnloadClasses`) becomes load-bearing rather than
someday work.

*REST remains the exception on auth* - `objects/rest` is genuinely stateless
request/response, `curl` has no connection to be the session, so that
surface needs per-request auth of its own.

**Mutual TLS is the good fit for the real audience.** A per-user client
certificate beats a password flow for a team, a lab or an operator: nothing
travels, there is no password store, no session table and no cookies, and
the browser proves possession on every connection. The costs are enrollment
and revocation, and browsers' certificate pickers are unpleasant. Fair trade
for a handful of trusted users; wrong for public signup.

**Session cleanup is free.** A user's engine exits when nothing is
scheduled. A browser holds it up through the flush task; when the last
client detaches and that task stops re-arming, the process ends on its own.
No reaper, no session timeout, no garbage collection of abandoned sessions -
logout IS quiescence, and quiescence was already the shutdown mechanism.

**Crash isolation comes with it**, which is the honest answer to having no
boundary between objects inside a process: make the process the user.

**The share folder is a container in the engine, and the verb is clone.**
Not a directory, not a file format, not a serializer round trip: a View that
users clone INTO to share a thing and clone OUT of to take one. Which is the
Palette pattern applied to user-made things - the Palette is already a View
holding one instance of every class, and dragging out of it is already a
clone. Same mechanism, different container, and the GUI gesture exists.

Everything that makes clone correct is therefore already done: internal
links come across relative, compiled handlers are rebuilt by construction
rather than copied, help comes with it. And it needs no locking, no merge,
no conflict resolution and no multi-writer anything, because taking a copy
is taking a copy.

It is also strictly better to look at than a file listing. **A shared thing
is live.** Open its panel and see what it does before you decide to take
one - the shared instance is a real instance, not a description of one.

Consequences that are already handled:

- **The file dialog grows a second source.** It already lists what the
  engine says exists in `saved/`; the share folder is one more place to list.
- **Import is where a singleton class earns its keep.** A foreign flow
  carrying somebody's engine-settings object is refused on create, so
  importing cannot overwrite your own. The rule that makes it a singleton is
  the same rule that makes import safe.
- **Provenance has a precedent.** Every library node already carries
  `UUID`/`Company` for exactly this reason; a shared export should say who
  shared it, on the same grounds.

**The caution: a shared instance is running.** A share view full of live TCP
objects would hold ports and keep the process from ever quiescing. The
existing rule covers it - a widget does not act until something tells it to,
so a shared thing sits built and unactivated - but it has to be *stated*
here, because "clone it in and leave it" is otherwise an invitation to a
folder full of half-live network objects.

**Two deployments, one idea.** A share view inside one engine serves the
users who share that engine - a team, a tenant - and sharing is a clone
between containers in one tree, immediate and with nothing serialized.
Across engines, the same gesture pointed at a Bridge is federation: browse
what that instance has and clone it here. Export/import to files stays for
what it is already good at - carrying a thing to somebody you are not
connected to. Three ranges, one verb.


### Reflecting a remote share into a local view

Across a network the gesture is the same, and the object that does it
already has a working twin. **MCPSource** connects to a remote service, asks
what it has, and builds real widgets for each of them inside a view named by
its `ViewName` property. A **ShareSource** is that object pointed at another
engine's share view: contain a TCP client, speak the bridge protocol to the
peer, and reflect what is there into one of your own views.

The pieces it needs are built. `Bridge_ListInstances` enumerates a
container. Creates and deletes are already announced scoped by container
key, so a reflection stays in step by subscribing rather than polling. And
a Bridge is already proven as a client of another Bridge - the harness
composes its own raw transport through the web bridge to run its tests.

**Two kinds of reflection, and they must not be conflated.**

*Catalog reflection* mirrors the remote share's **contents list** into a
local view: you see what is on offer, and cloning one brings a copy across
to live in your tree. This is the palette of somebody else's engine, it is
cheap, and it is what "share a folder over a network" should mean.

*Live reflection* makes a shared node's value visible to both sides: I share
a Textbox, you subscribe to it, and my updates arrive in your view. Note
what this is NOT - it is not two people editing one field. **One node has
one writer.** I put a box in the share and only I write it; you put one in
and only you write yours; each of us is read-only on the other's. Two-way
talk is two one-way channels, which is exactly how the bridge is already
wired to TCP - `Connect(Tcp,"Out",Bridge,"In")` and
`Connect(Bridge,"Out",Tcp,"In")`. So the conflict problem that makes
collaborative editing an entire product (OT, CRDTs) never arises here,
because nothing is ever written by two parties.

That also supplies a permission model with no ACL in it: **you put it in the
share, so it is yours to write, and everyone else reads.** Ownership by
placement. Enforcement belongs in the ShareSource - refuse writes to
anything it did not originate - not in the engine, the same way the bridge
is the only thing that knows what a frame is.

**Lifetime follows the same rule, so there is no destructive conflict
either.** Nobody can delete what somebody else put in; you remove your own
contribution, or you drop the whole shared view from your side. And when a
peer does remove theirs, the event already exists: a peer closing is
`msg_eof` out the relevant property, exactly as it is for a TCP peer
disconnecting or a Reader hitting end of file. A remote user leaving needs
no disconnected state and no reconnection protocol - a consumer goes quiet
when its producer finishes, which is what consumers here have always done.

Two hazards remain and are both small. **Echo**: a box writes the shared
node and is subscribed to it, so its own write returns - covered today
because `SetPropStr` fans out only on a real change, but that protection is
now load-bearing across a round trip and wants a test that pins it.
**Latency and rate**: keystrokes should coalesce through the same flush
mechanism as the browser's dirty set rather than sending one message per
character.

And a distinction worth keeping: a shared Textbox holds the **latest**
thing said; a shared Queue holds the **transcript**. Walkie-talkie versus
history - two different objects for two different intentions, neither one a
workaround for the other.

**The fork that has to be a decision, not a default.** Cloning an instance
does not ship code - it produces an instance of a class the receiving engine
must already have. So a share either:

- carries **only instances of classes both sides hold**, which is safe and
  limited; or
- carries **`.object` files too**, which is powerful and means executing
  somebody else's native code in a process with no security boundary between
  objects.

The second is a real capability and a real exposure, and it should be chosen
deliberately with the trust model written down beside it.

**And there is a sweet spot in between: the scripted composite widget.** A
View with bound ports, controls, and a script inside is a working custom
widget that contains **no compiled code**. It crosses a network as data,
runs in a language host that already has a runaway guard, and needs nothing
installed on the far side but the host. That makes it the natural unit of
network sharing - the thing people will actually want to trade - and it is
already built.


## Planned - users and security: what is a boundary and what is not

The security posture is currently stated in pieces across several entries.
Collected, because the pieces are individually reasonable and the whole is
what someone deploying this needs to see.

### The model, stated positively: sharing by subscription IS the security

Each user has their own process, their own root, their own directory and
their own objects. **Nothing in your environment is in mine.** The only
thing that crosses is the value of a node you chose to publish, and single
ownership means I write mine and you write yours - neither of us can write
or delete the other's.

That is the whole model, and it is stronger than an access-control list
because there is nothing to grant and nothing to misconfigure. **A node is
reachable because you placed it in a share view.** Absence is the default,
and `objects/rest` already works exactly this way: "containment is the
publication - drag an object into that view and it is published, drag it
out and it is not. There is no exported flag and no category." **Default
deny by construction, not by policy.**

It also disposes of the gap listed below rather than mitigating it. "No
boundary between objects" is a *multi-tenant-in-one-process* problem. This
architecture never puts two tenants in one process, and inside your own
process everything is yours already. The remaining trust surface is small
and ordinary: the protocol parser handling untrusted peer input, and
resource exhaustion from a peer subscribing to everything or flooding
writes - which is what the flush/coalescing work already answers.

**What crosses is an alias, not an object.** A share view holds links to
your real nodes, and what a peer receives is the *value*, read only.
Nothing is instantiated on their side and nothing executes.

**Three verbs are the whole protocol: walk, read, subscribe.** Discovery is
walking, drill-down is walking, live status is subscribing - and MCP, REST,
a peer engine and the browser are dialects of the same three. Note the
direction that puts things in: **the native surface is the richest one and
every translator is a lossy projection of it.** REST is pull-only so its
translator turns push into polling; MCP downgrades similarly. Neither could
be upgraded into push if the source lacked it, which is why subscribe being
native rather than bolted on is what lets the other surfaces be honest about
what they offer.

**The walk must root at the share view and must not escape upward.** No
`..`, no resolving above the published container - otherwise "walkable"
quietly becomes "the whole tree is readable." `objects/rest` already
enforces this ("the published view is the root of the URL space, and
nothing in a URL says where the view lives"), and the same rule has to hold
for a ShareSource. It is what makes containment-as-publication a boundary
rather than merely a listing.

**The address form is a URL, and that is what makes it survive.** A remote
alias is a link whose target carries an authority component -
`dataobj://peer/Root/Sheet/Total` beside the local `/Root/Sheet/Total`.
Because
it is a **string**, `IsPortableProp` passes it: a remote alias lands in a
saved flow, exports, imports, and reconnects on the far side. A runtime
handle could never do that, so the URL form is what makes remote wiring
serializable at all.

The scheme is **`dataobj://`**. Deliberately not `data:`, which is
registered - RFC 2397, `data:text/plain;base64,...` - and which browsers,
JS and every HTTP library treat specially.

Three things fall out. **Local and remote aliases become one record**: the
only difference is whether the target has an authority component, so
`LinkPropertyAs` gains a resolver rather than a species - the same
resolve-through `ResolvePort` already does for container ports.
**Multiplexing is already proven**: ten aliases to one peer must share one
connection, and the Bridge already multiplexes many subscriptions over a
single socket, which is what every browser is doing right now. **And address
stays separate from resolution**: the URL is data in a property and a
ShareSource is what resolves it, so a flow can name a peer it is not
connected to yet.

The REST surface already half-proves the mechanics - `curl -X PUT -d 1
http://localhost:8483/bob` writes a button today. What this adds is the
other direction: subscribe once, get pushed on change, which is the Bridge's
native verb wearing a URL instead of a JSON command.

**And a shared node carries no authority until the receiver grants it
some.** A Button I share is inert in your environment - it is a value that
changes when I press it, and it does nothing at all until *you* connect it
to something of yours. So "what can a remote peer do to me" has an exact
answer: whatever I wired their node to, and nothing else. Revoking is
disconnecting. That is capability discipline, and it falls out of the fact
that a connection is something the receiving side makes.

The one thing that would void all of it is a share carrying `.object`
files. Values cross; **code stays local.**

### What is NOT a boundary

- **Objects inside a process.** A loaded `.object` is native code with full
  access to the fabric. Isolation between objects is message *discipline*,
  not enforcement - there is no sandbox, no capability check, and nothing
  stopping a module from walking the registry and writing anything it finds.
- **`ReadOnly`** (once it exists). It is an interface contract: it stops a
  client, not a module. Worth having and worth not mistaking.
- **Containment and paths.** Addressing, not access control. A path that
  resolves is a path anybody in the process can resolve.
- **The message fabric itself.** Queued dispatch decouples senders from
  receivers; it does not restrict them.

### What IS a boundary

- **The process.** This is the real one, which is why per-user deployment is
  per-*process* rather than a preference: make the process the user and a bad
  object costs one person.
- **The filesystem, via cwd.** Per-user working directory means per-user scan
  path AND per-user saved directory - so which objects a user can load and
  which flows they can open are both settled by one `chdir` before `exec`.
- **The listener.** `-ip` decides who can reach the port at all. The default
  is `0.0.0.0` deliberately - the operator is usually remote and
  `127.0.0.1` locks them out - which makes it a decision to be made per
  deployment rather than a safe default to rely on.
- **The launcher's authentication**, which happens *before* the socket is
  handed over, because it is the one thing that cannot live inside what it
  protects.
- **Each translator.** REST refuses a `PUT`, the Bridge refuses a
  `set-property`, a ShareSource refuses a write to anything it did not
  originate. Enforcement belongs where the vocabulary is, and the engine
  keeps having no opinion - it has no directions, and it should not acquire
  permissions either.

### The gradient of what a share may carry

Riskiest last, and the line should be drawn deliberately rather than by
whatever the first implementation happens to allow:

1. **An alias to a value.** The default and the safe one. A peer reads what
   the node holds and cannot write it, and it does nothing in their
   environment unless they wire it to something. This is what "share"
   should mean, and it needs no gradient at all.
2. **A copy taken deliberately** - cloning a widget OUT of a share into your
   own environment. Cloning does not ship code: it produces an instance of a
   class the receiver must already hold, so nothing executable moves. But it
   is a distinct act from watching a value, and should look like one.
3. **A scripted composite widget, copied.** A View with bound ports,
   controls and a script: a working custom widget containing **no compiled
   code**. The script executes, but inside a language host, and all three
   hosts (Lua, QuickJS, atlast Forth) share one runaway budget held in
   `script.object`. This is the natural unit of trade - powerful, portable,
   and bounded.
4. **`.object` files.** Native code in the receiver's process, which is full
   trust. Under per-user processes and per-user scan paths the blast radius
   is one user's directory, which is what makes it tolerable at all.

### Where it actually stands today

- One engine, one root, all clients sharing it. Per-user processes are
  planned, not built.
- The authenticated bridge flow exists but is **disabled** in the default
  app; the harness composes its own raw bridge at test time.
- `ReadOnly` does not exist - nothing marks a property unwritable.
- So the honest current posture is: **run it on a network you trust**, and
  say so plainly to anyone who asks, rather than letting the architecture's
  tidiness imply a hardening that is not there.

### A consequence: build your own widgets, load and unload them live

If code stays local, then what a user builds in their own environment is
theirs to build freely - and **a widget you build is not compiled**. A View,
controls, bound ports and a script. So creating and destroying one is
`CreateObject` / `DeleteInstance`: **runtime load and unload of scripted
widgets needs no `dlopen`, no reload plumbing, and works today.** The
half-built hot-reload machinery (`_fini` -> `UnregisterLibrary`,
`UnloadClasses`) is only needed for native `.object` files, which are
exactly the rung of the sharing gradient that does not travel.

And the builder is not speculative - it exists once already. **MCPSource
generates real, working, clonable widgets at runtime** from a description:
an input box per declared input, a readout per output, the help text, a
Submit button. Point that same generation at a person's choices instead of
a remote agent manifest and that is the widget builder. Which also means
the thing a person builds is, from the first moment, an ordinary instance -
clonable, savable, exportable, shareable as a value-carrying composite, with
no separate "user widget" species anywhere.

### What has to be written down before any of this ships

A trust model, per surface, answering four questions each for the Bridge,
REST, MCP and a ShareSource: who may connect, who may write, what a share
may carry, and what an authenticated session is permitted to do. Every
mechanism above is cheap. Deciding these four is the part that is not, and
doing it after three surfaces exist is how the answers end up different on
each one.


## Found 2026-08-16 - "done" has to mean every path it made resolves

`load-flow` answers with `flow-loaded`, sent from `Bridge_LoadFlowDone` - the
serializer's own completion callback. A client that waits for that event and
then acts has, on the evidence, acted too early:

    list-instances '/Root/RawTests/LoadThenClone/LrView': announced 0 member(s)
    BRIDGE ERROR: 'create-instance' - unknown container
    BRIDGE ERROR: 'clone-instance' - unknown instance

Those are not missed events. That is the engine correctly reporting that the
paths do not resolve, after it said the load was finished. The same log shows
listings landing between one instance's `Container` write and its `Name` write,
so the client was reading a tree that was still being assembled.

**Why only one build.** A load is staggered on purpose -
`DESTROY-CONTENTS-ASYNC: snapshot done, 367 victims - staggering destroy`, then
a rebuild spread over ticks, so the engine never blocks. Under asan every step
is slower and the half-rebuilt window is wide enough to fall into. On `-O3` it
closes before anyone looks. That is the whole reason this read as "rawtest
randomly blows up in asan" for weeks.

**Why no human has ever hit it.** Nobody clicks faster than a staggered load.
The window has never been reachable by hand, which is why it only surfaced in a
test.

**Why that stops being a defence.** Scripts are becoming first-class clients
here - a REST caller doing POST then GET, an agent driving a flow through the
manifest. None of them have human latency, and all of them will do exactly what
the test did. Human slowness has been holding this closed, and the `/Root/mcp`
surface is the thing that opens it.

**The general rule, which is bigger than this one event.** An event that claims
completion must mean every path it created resolves. Otherwise a client cannot
tell "done" from "nearly done", and its only recourse is to guess a delay -
which is a race with better manners, not a fix. Worth auditing every completion
event the protocol sends against that rule, not just this one.

**What to check first:** whether `LoadViewAsync` invokes its done callback after
the last create step or merely after scheduling them, and whether `RegisterPath`
has run for everything by the time it fires.

**Meanwhile:** `rawtest`'s `settled()` waits for the container to actually hold
its members instead of sleeping, and PRINTS A NOTE when it had to wait. That
note is the tripwire - if `flow-loaded arrived before it was ready` appears, the
completion claim is wrong and every non-human client inherits it. The harness
absorbing the race is a stopgap, and the note is there so absorbing it never
becomes the same thing as not having it.

## Found 2026-08-16 - a test waits for a STATE, never for a duration

`leaktest`'s subscribe/destroy check failed on `release` and passed on the
other four builds, with the totals conserved: `d1={'Nodes': 128, 'Datas': 256}`
against `d2={'Nodes': 112, 'Datas': 224}`. Sixteen nodes did not leak - they
landed on the near side of a sampling boundary instead of the far side, and
the two windows add up to the same thing. Allocation counts do not depend on
the optimiser. Timing does, and that cycle is built out of
`time.sleep(0.6/0.4/0.3/1.2/0.6)` with a flat `2.5` before each sample.

**A sleep is an assertion about how fast the machine is.** It says "by now the
work is finished" without ever asking, so it is wrong twice: too short and the
test measures a half-finished fabric, too long and every run pays for the
slowest build. Both failures look like flakiness rather than like the guess
they are, which is what makes them expensive - the report accuses the engine
and the engine is fine.

**The rule: every wait in the harness names the state it is waiting for.**
Wait for the event that says the thing happened, or poll the fabric for the
condition itself - a path that resolves, a container that holds its members, a
counter that stopped moving - with the sleep left only as a timeout, whose
expiry is a FAILURE with a message saying which state never arrived. "It has
been 1.2 seconds" is not a state.

**The two shapes already in the tree, one right and one wrong.** `delete()`
waits for `instance-removed`, so it is correct at any speed.
`subscribe_destroy_cycle` destroys through a Lua script's `destroy()` verb
precisely because that is NOT the delete-instance path - and there is no event
on that route, so it can only sleep. That is the real gap: the state exists
(the instances are gone, the subscriptions are reclaimed) and nothing
announces it. `rawtest`'s `settled()` is the pattern that works - it waits for
the container to actually hold its members, and prints a note when it had to
wait, so absorbing a race never quietly becomes not having one.

**Also found: the harness stages no help files.** `run.sh` hardlinks only
`objects/*/*.object` into a variant directory, flat. A widget declares its help
as `objects/<name>/README.md`, read relative to the cwd, so under test that file
has never existed and every Help panel has always opened blank. Nothing could
test help at all, which is part of why "a clone loses its help" went unnoticed.
Fixed 2026-08-17 - the READMEs are staged beside the objects.

**What to do, in order.** Audit every `time.sleep` in testharness for what
state it is standing in for, and replace it with a wait on that state; where
no event exists to wait on, that is a finding about the protocol, not a
licence to sleep - a route that destroys things without announcing it is a
route a non-human client cannot follow either. Same rule as the completion
claim above: a client that cannot tell "done" from "nearly done" can only
guess a delay, and a test is just the first client to have that problem.

## Found 2026-08-16 - what naming-at-creation unlocked, and what it did not

`CreateObject` now mints and registers, `CreatePrivate` is the deliberate
opposite, and `RegisterPath` treats a second name as a move. Three things
follow from that, two of them worth doing and one of them a correction.

**`NameTakenIn` is now redundant, and it is the expensive half of minting.**
`MintFreshName` probes twice per candidate: `ResolvePath` (O(path length), the
trie) and `NameTakenIn` (a walk of every instance in the session, comparing
`Name` and `Container`). The scan exists because a placed instance could be
unnamed, and an unnamed member is invisible to the index - which is exactly why
it was reverted back in when the Members index landed. **Placed now implies
named**, so the scan can go and minting becomes a trie lookup. Worth checking
first: private handles ARE placed and unnamed by design, so confirm nothing
mints a name a `CreatePrivate` instance is already carrying (a socket calls
itself `TCPSocket` in its own `InstanceStart`) - the bases differ today, and
that should be verified rather than assumed.

**The container scans still have not moved.** `Bridge_ListInstances`,
`NextContainerChild` (serializer) and rest's manifest still walk every instance
comparing `Container` strings. The Members index answers that directly, and
after this change the two agree on every placed member. This is the same item
as the classing work's - it is just now unblocked.

**The correction: the two-phase widget constructor does NOT go away.** The blog
piece claimed a constructor can now build its own panel. It cannot.
`CreateObject` calls `InstanceStart` and mints after it RETURNS - the instance
does not exist until the constructor makes it, so inside `InstanceStart` there
is still no path, and `scriptbox.c`'s deferred build is still required and
still correct. Closing that window is a separate change with a different shape:
the name would have to be decided before the constructor runs and handed to it,
which means `InstanceStart` taking its identity as an argument rather than
discovering it afterwards. Not obviously worth it - the deferred build works -
but it is the only remaining reason widget construction is in two halves, so
it should be recorded as a choice rather than left looking like an accident.

> **DONE 2026-08-17, and "not obviously worth it" was wrong - the deferred
> build did not work.** It was the cause of a cloned widget's dead panel: the
> deferred phase needed a `PanelBuilt` flag, the flag was an ordinary property,
> and every clone, save and import inherited "already built" and skipped the
> phase that installs the compiled handlers. The shape is exactly the one
> guessed at here - the name is settled before the instance is made and handed
> to the constructor with its location - and it took out more than the two
> halves. See `20260817_0039_a_blank_help_panel.md`.
>
> Closed by it:
> - **the two-phase constructor**, and with it `Widget_DeferBuild`,
>   `Widget_DeferBuildQuiet`, `Widget_BuildOnce`, `Widget_BuildTask`,
>   `Widget_BuildTaskQuiet`, `Widget_BuildDone`, `Widget_CancelBuild`, the
>   `PanelBuilt`/`WidgetTable`/`WidgetBuildTask` properties, and the private
>   copies of the same pattern in ComfyUI, Ollama and StableDiffusion.
> - **one task per widget panel ever built.** `CreateTask` no longer appears
>   in widget.c. The leak note in `Widget_BuildDone` - "the palette alone arms
>   about twenty-eight of them before anyone touches anything" - describes
>   something that no longer happens.
> - **mint-then-rename on every object.** `CreateObject` kept the name it was
>   handed instead of minting one and letting the caller rename it, so
>   `Checkbox_1 -> Enable` and its 40-odd siblings are gone. Renames at boot:
>   zero.
> - **the clone's second creation path.** `CloneObject` calls `CreateObject`
>   rather than the class's `InstanceStart` directly, and the group walk writes
>   values onto members that creation already built instead of making
>   duplicates of every declared control.
> - **help in a clone**, which is what was actually being chased.
>
> Not closed by it: each widget's `*_Activate` still holds init work that
> belongs in the constructor now that the constructor has a place and a name.
> That is a per-widget judgement - init moves, action stays - and nothing
> forces it, since panels build without it.

---

## The last cleanup of the class path: the serializer's own special case

Clone and serialize are messages now (`msg_serialize` / `msg_deserialize`,
callback.h), and both callers ask before they act: `CloneData` (object.c) and
the serializer's per-instance walk send the message, use whatever came back,
and do the default property walk only if no class claimed the job. A class
that keeps state where a property walk cannot see it — a private object it
points at with a LONG, which `IsPortableProp` refuses by design — answers, and
answers by sending the same message one level down to whatever holds the data.
`objects/tableview` is the worked example: a `Table` reached by a `long`, nine
controls aliased onto a window of its cells, and it survives clone, export and
import with its values because it answers the message rather than because
anything walks into it.

What is still outside the class path, and it is the last of it:

- **The serializer names the contribution.** Export writes what the class
  produced as a property literally called `Self`, and the import loop tests for
  that name to know where to send it back. That is a special case at both ends
  of the file format — the one place the format knows something about the
  mechanism instead of just carrying it.
- **A deliberate drop is still overridden by its parent.** `rtrn_unhandled`
  exists and means "I did not handle this", but `PuntToClass` still accepts
  `rtrn_dropped` as "keep walking", because 238 handlers across `objects/`
  currently spell "not mine" that way (only 4 sites in the core test a verdict
  at all). Each of those 238 is one of two statements — "I recognised this and
  consumed it" or "I have never heard of this" — and only reading it says
  which. Converting them lets the second test go, and a class that refuses
  something stops being answered for by the level above it.

The conversion fails closed if it is done in that order: nothing walks until a
handler says `rtrn_unhandled` deliberately, so a missed site shows up as an
object not doing something, rather than as a parent quietly answering for a
child that had refused.

---

## Resolve from where you already are: a namespace offset per instance

Every path lookup starts at the root and walks the whole string. An
instance that knows its own position in the namespace trie could start
there and resolve only the tail - `A1` instead of `/Root/Sheet/Data/A1`.

The API is already shaped for it. `NSSearch(NSObj *Root, char *String)`
(namespace.c) takes the node to start from and walks `Root->Child`, so
handing it a sub-node instead of the real root needs no change to the
search at all. What is missing is a way to GET that node: NSSearch returns
the `Value`, not the `NSObj *`. One sibling entry point that hands back the
position is the whole addition.

The saving is larger than the character count suggests, because each level
is a linked-list scan across siblings (`Current->Next`) - cost is length
times branching, and the prefix skipped is the widest part of the trie.
Everything in a session shares `/Root/`, so those first levels carry the
most siblings; resolving `A1` from the owning instance's own position is a
couple of steps at a level with a handful of children.

**The hazard is the one that costs a day when it is got wrong: a cached
trie pointer is a dangling pointer the moment the path changes.**
`NSDelete` frees chains, and a rename re-keys an entire subtree
(`Bridge_RepathSubtree`). So the position has to be dropped whenever the
instance's own path changes - and there is exactly one choke point for
that, `RegisterPath`/`UnregisterPath`, which every creator, clone, import
and rename already passes through and where `LastMember` is already
written. Hold the position in a LONG property so `IsPortableProp` refuses
it and no file or clone can ever carry a stale one.

Independent of this, and probably the larger prize: several things resolve
by walking the registry rather than the trie at all - the table widget
finds its controls with `FirstInstance`/`NextInstance` sweeps, and
`DeleteInstance` sweeps the whole registry twice to answer "who points at
me?". Those are O(session) per lookup against the trie's O(tail). The
offset makes every path resolution cheaper whatever happens to them; see
also the two-sided subscription note, which is what would retire the delete
sweeps.

---

## Security: "anything can insert into anything" needs a boundary

Everything in this system is reachable. A script resolves any path, writes
any property, creates and destroys instances, and - once functions can be
installed on properties - can put its own handler on something it did not
write and did not ship with. That is the power the whole design is for, and
it is exactly why a thing that arrived from the internet must not have all
of it by default.

This is a new topic and nothing here is decided. What follows is the shape
of the problem as this framework actually presents it, so the answer is not
invented from a generic threat model that does not fit.

### Three different surfaces, and only two are defensible

**A native `.object` is total trust and always will be.** It is `dlopen`ed
into the process; it can do anything the process can do, and no policy the
framework enforces can change that. The boundary for native modules is
therefore the decision to install the file, not anything at runtime. What
the framework can offer there is provenance - the `UUID`/`Company`/version
already on every library node - and the discipline that objects arrive as
single files a person chose to put in the scan path.

**A script is defensible, because it reaches the system through exactly one
door.** Every language host goes through the shared verb table in
`objects/script/script.c` - `pathget`, `pathset`, `create`, `destroy`,
`connect`, `disconnect`, `send`, `activate`, `exists`. One table, three
languages, and the comment on it already says "add a verb HERE and every
language has it". That is also where a check goes: one place, all
languages, no per-host policy to keep in step.

**A flow is data that becomes behaviour.** Loading one creates instances,
wires them, and - once scripts attach to nodes - installs functions. So
"download a flow" is "download code", and the load path is where that has
to be faced. A flow describing its own contents is one thing; a flow that
reaches out and installs a handler on something already in the session is
another.

### The questions to answer

**What is a script's default reach?** The natural boundary is already in
the design: a widget's script talks to its siblings, and containment is a
path. So "my container and below" is a scope that costs nothing to express
and matches what scripts legitimately do today. Reaching outside it is the
thing that should need saying out loud.

**Who grants it?** A host is created by whoever inserts it, and the
`{owner, base, port}` handed over at creation is already the pattern for
"the creator decides how this thing reports back". A capability handed over
the same way - which verbs, which subtree - follows the shape that exists
rather than adding a policy object.

**Who may install a handler on a property?** This did not exist as a
question before intercepts. A control that came from elsewhere should
probably not be able to put a function on the session's File menu. There is
already a precedent for a property refusing an operation - `Deletable=0` -
and it is worth deciding whether the answer is a property on the target, a
capability on the installer, or both.

**Can an intercept be refused?** The chain means a later intercept wraps an
earlier one. If some behaviour must not be overridable - an Enable that
really disables, a lock that really locks - then something has to be able
to say no, and that is a different rule from "who may install at all".

**What does a flow get to touch?** A load that only builds what the file
describes is bounded by the file. A load that installs handlers on
pre-existing instances is not. Whether those are the same verb with
different scope, or two different things, decides how hard the boundary is.

### The constraint on any answer

It must not become a second mechanism. This system's strength is that there
is one kind of thing and one way to do each gesture; a security model that
adds a parallel permission tree, a policy object, or a second dispatch path
would cost more than it protects. The likely shape is a capability carried
where creation already carries the reporting address, checked at the one
door scripts already go through, and expressed in paths because addressing
is already paths.

And it must fail closed the way the verdict conversion does: a script with
no grant reaches nothing outside itself, rather than everything until
someone remembers to restrict it.

### The likely answer: the tree is the policy

Scope it by containment. A script installed into a thing can see its
SIBLINGS AND DOWN and may intercept there; reaching ABOVE that level -
subscribing to, or installing on, anything outside its own container -
needs permission.

That satisfies the constraint above, because it adds nothing. Containment
is already a path, so the check is a prefix compare against the installer's
own container: at or below, allowed; anywhere else, refused unless granted.
The grant is a path too, in the same vocabulary as everything else.

The asymmetry is the right way round. Installing a handler beside or
beneath you is acting on something you are part of, and it is what a
widget's own script does all day. Reaching upward is reaching into the
context that CONTAINS you, and that is the direction a downloaded thing
would escape in - the File menu, the bridge carrying the session, another
author's widget on the same canvas.

The consequence worth having on purpose: **placement becomes the sandbox.**
Drop a widget into a view and its reach narrows to that view, with nothing
declared anywhere. Move it out and it widens. That is an existing gesture
doing the security work, which is the cheapest possible form this could
take - and it means a person can reason about what a downloaded thing can
touch by looking at where it sits, rather than by reading a manifest.

Left to settle: whether "down" means the whole subtree or one level; what a
grant looks like when it is given (a path on the host instance, presumably,
handed over at creation the way the reply address already is); and whether
a thing can refuse to be intercepted even from within its own container.

### Policy as an intercept, the way a directory did it

Netscape Directory Server put access control in the directory. An ACI was
an ATTRIBUTE ON AN ENTRY: it lived in the tree, governed that entry and
everything beneath it, was administered with the same tools as the data,
and several of them combined at the moment of access. There was no separate
policy store to drift out of step with the thing it protected.

That is an intercept, described in another vocabulary.

Put the check on the subscription rather than in the plumbing, and policy
becomes an ordinary node: it sits where it governs, it inherits downward
because containment is a path, it clones and saves with the branch it
protects, and several of them stack because the chain already lets each
link refuse or decline to the next. Nothing is administered by a different
mechanism from everything else - you install a function, and the function
happens to say no.

**What it requires: connect and subscribe have to be messages the target
answers, not calls that simply happen.** That is what makes them
interceptible at all, and it is the same question as "could an object deny
a connection if I wanted it to" - which turns out to be the security model
arriving early, wearing different clothes.

Consequences worth wanting:

- **A branch can carry its own rules.** Drop a subtree into a session and
  its policy comes with it, because the policy is in it.
- **Policy is inspectable with the tools that already exist.** It is nodes;
  list it, wire something to it, watch it fire.
- **Deny is a verdict, not a special case.** A refusing intercept returns
  the same code any handler returns; nothing needs a second notion of
  failure.
- **The default stays open only where nothing was installed.** A branch with
  no intercept behaves exactly as it does today, so this costs nothing until
  somebody wants it - and a downloaded thing dropped into a governed branch
  is governed by where it landed.

**And the policy enforces access to the policy.** That is not a rule anyone
adds - policy is nodes, so reaching it is governed by the same containment
asymmetry as reaching anything else. A thing inside a governed branch
cannot reach UP to the node carrying its own constraints, so it cannot
remove them. The authority to set policy comes from being above what it
governs, and being above something is a fact about the tree rather than a
privilege the mechanism has to know about. There is no bootstrap exception
and no special branch: the ACI controlling access to the aci attribute was
always just another ACI.

Left to settle: whether a refusal is distinguishable from "not mine". It
has to be - declining continues the chain and refusing must stop it - and
that is the one genuinely new thing this needs.

### Administration is not access

The section above says authority comes from being above what it governs.
That is too coarse, and the coarseness is a real conflation: it hands
whoever can ARRANGE a branch the right to READ it.

Serious systems separate those. The person who administers - places things,
sets the rules, manages who may do what - is not thereby the person who may
see the data. And the person working with the data cannot change what is
allowed. Neither right implies the other, and that is the point: an
administrator who cannot read is not a weaker administrator, it is a
different job.

This framework can express it honestly because the two are genuinely
different operations on nodes rather than a distinction someone has to
invent:

- **Administration** touches structure and policy: create a member, move
  it, delete it, install a handler, write the rules that govern a branch.
- **Access** touches values: read a property, write one, subscribe to it,
  receive what flows through it.

An intercept sees which one is being attempted, so a single mechanism can
permit one and refuse the other. A grant therefore is not one capability
over a subtree but two, independently given, in the same path vocabulary.

What that buys, concretely here:

- **A container can hold a branch it cannot look inside.** You may place a
  widget, wire it, move it and delete it, and still not be able to read
  what passes through it. That is the useful shape for a session hosting
  somebody else's flow.
- **A worker can be denied the ability to rewire.** A script processing
  values can be given full access to them and no administration at all, so
  it cannot install handlers, cannot move members, and cannot alter what
  governs it - even inside its own container, where the containment rule
  alone would have allowed it.
- **The policy-protects-policy rule gets sharper.** Reaching UP is still
  refused, but now being above only confers administration by default.
  Reading downward becomes something granted rather than something owned.

Left to settle: whether the two rights are two grants or one grant with a
kind, whether administration divides further (placing versus policy-writing
are arguably different jobs), and how a refusal distinguishes "you may not
do this" from "not mine" - which is the same open question as before, and
now carries more weight, because the answer is what a denial IS.

### Where identity lives: properties on the bridge

None of the above says WHO. The answer is the same shape as the rest: the
bridge is the only piece that knows a client exists, so a login installs
the user's properties onto that bridge instance, and everything downstream
reads them as ordinary properties.

Two pieces of this are already standing. The protocol carries a
`logged-in` event, and the design already anticipates a root per login -
`CreateRoot` makes an ordinary View with no container, and as many as you
like. So the join is: the login puts the user on the bridge, the bridge is
handed that user's root, and containment scopes everything from there.
**Your root is your reach.**

What follows without further mechanism:

- **No ambient authority.** A second connection with a different login is a
  different bridge instance carrying different properties. There is no
  session-wide "current user" for anything to consult, and nothing acquires
  rights by being in the same process.
- **Rights are data.** They are properties on an instance - granted from
  above, listed with the same verb as anything else, saved and restored with
  the session, and unreachable from below by the same rule that stops a
  branch editing its own policy.
- **The intercept sees who.** A policy intercept is a handler; the delivery
  it is answering came through a bridge, so the identity it needs to decide
  with is reachable rather than passed along in a parallel channel.
- **One person, several roots.** Nothing says a login gets exactly one, and
  a grid of connections each with its own root is a wall of sessions
  belonging to different people, in one engine, isolated by containment
  rather than by a tenancy mechanism.

Left to settle: how the identity travels to a policy intercept that is
several hops from the bridge (the delivery knows its source - MsgFromNode -
but "which connection" is a different question from "which node"), and
whether a login's grants live on the bridge or on the root it was handed.

### The real boundary is the process, not the policy

Everything above organises access. Only one thing enforces it.

A native `.object` is loaded into the address space and can do anything the
process can - it can walk the node tree directly, ignore every intercept,
and read any value in the session. That is stated plainly at the top of this
topic and it does not get better with more policy. In-process rules are
worth having: they structure a session, they keep honest code honest, and
they make a downloaded widget's reach obvious from where it sits. They are
not a wall.

**The wall is a different process.** Run the dataflow holding the data on a
server, connect to it over the network, and present a token you were given
when you authenticated. Then a compromised or malicious thing on your side
can do exactly what the token permits and nothing else, because the data was
never in your address space to reach.

This needs no new protocol. A Bridge is already verbs in and events out over
TCP; a remote dataflow is a Bridge you connect TO rather than one that serves
a browser. Same commands, same events, different direction. And the identity
work above lands on the far side: the user's properties and grants live on
the SERVER's bridge for that connection, so nothing about your rights is
stored anywhere you control, and revoking one is a change on the machine
that owns the data.

What today's work already contributes:

- **A view talks to its data through messages.** The table widget asks its
  data object for cells rather than reaching into it, so where that object
  lives is the object's own business. That split is the precondition for the
  data being somewhere else at all.
- **Addressing is paths.** `/Root/Sheet/A1` names a cell without saying
  which process holds it.
- **Verbs are the whole surface.** A remote peer is limited by which
  commands it may send, and that list is short and already written down.

What it needs:

- TCP client mode (already on this roadmap for other reasons).
- A token on the connection, checked once, carried by the bridge instance -
  which is where identity already goes.
- A decision about what a remote reference looks like locally. Today a
  widget points at its data with a LONG, which is an in-process pointer. A
  remote one has to be a path plus a connection, and the widget must not
  care which it holds.

The last of those is the interesting one, and it is the same shape as every
other question that turned out well here: make the two indistinguishable to
the thing using them, and where the data lives stops being an architectural
decision anybody has to take twice.

### dataobj:// - one name for a node in any address space

The open question above was what a remote reference looks like locally. It
looks like a URL.

    /Root/Sheet/A1                    this space, as today
    dataobj://box/Root/Sheet/A1       a node in another engine

Same relationship a file path has to an http URL: the bare form is relative
to where you are, the full form names the space as well. A thing holding a
reference holds a string either way and does not care which it has - which
was the whole requirement, stated one section earlier as "the widget must
not care".

Resolution splits on the authority: no authority means the local namespace
trie; an authority means a connection to that engine, speaking the verbs it
already speaks. Nothing about the vocabulary changes.

**And a wire across machines is a subscription.** The browser is already a
remote subscriber to a dataflow - it sends `subscribe`, it receives
`property-changed`, over a Bridge. A wire between two engines is that same
mechanism with something other than a GUI on the end. The first remote
consumer simply happened to be a browser, which made it look like a GUI
feature rather than what it is.

**A URL is a name, not an authority.** Holding the string gets you nothing.
Rights are per space: you authenticate to each engine you reach into and
hold what it granted you there, so a reference that crosses a boundary is
checked on the far side by the machine that owns the data. That is the same
separation the section above describes, made explicit in the naming - which
is worth having, because a system where the name IS the permission is how
capability leaks happen.

What this asks for:

- An authority component in the addressing, and resolution that dispatches
  on its presence.
- A connection registry, so two references into the same engine share one
  connection rather than each opening their own.
- Per-space credentials held where identity already goes: on the bridge for
  that connection.
- A decision about failure. A local path that does not resolve is a miss; a
  remote one can also be unreachable, slow, or refused, and those are
  different answers a caller may want to tell apart.

The last is the one that will shape the API, and it is the same question the
data-class notes raise about `AsOf` and freshness: a value from another
space is not simply present or absent the way a local one is.

---

## The plan, in the order it has to happen

Core first, each step its own full harness run, because each one changes
dispatch and a broken dispatch is indistinguishable from a broken
everything. Then the gesture. Then the things the gesture makes easy.

### Step 1 - a handler chain on a property (core)

The three places that read AND CALL a handler - `DeliverMsg`
(object.c:2914), `SetOrDeliverProp` (2976), `DeliverToSubscriber`
(node.c:649) - go through one helper: walk the handler records on the
property, then fall through to `OnMsg` as the last link. The other six
`OnMsg` reads are existence tests and become "is there any handler".

Nothing installs a record yet, so with zero records every delivery behaves
exactly as it does today. That is the point of doing it first and alone:
the diff is large and the behaviour change is none, so a harness run says
cleanly whether the mechanism is right before anything depends on it.

Done when: full suite green, and a hand-installed record is reached before
`OnMsg` and can pass through to it by declining.

### Step 2 - "not mine" means it, everywhere (core)

Convert the 238 `return rtrn_dropped` sites in `objects/` that mean "I have
never heard of this" to `rtrn_unhandled`, and drop `rtrn_dropped` from the
walk conditions in `PuntToClass` and the new chain.

This is mechanical, large, and fails closed: nothing walks until a handler
says `rtrn_unhandled` deliberately, so a missed site shows up as an object
not doing something rather than as a parent silently answering for a child.
It has to precede any notion of refusal, because while `dropped` still
means "keep going" there is no verdict left to mean "stop".

Done when: full suite green with both walks testing only `rtrn_unhandled`.

### Step 3 - a refusal that is not a decline (core)

One appended verdict meaning "you may not do this", distinct from "not
mine". Declining continues the chain; refusing stops it and the operation
does not happen. This is the one genuinely new thing in all of the
security work, and everything above it - policy as an intercept, deny,
administration versus access - is unbuildable without it.

Done when: an installed handler can stop a property write and the caller
can tell that apart from nobody having answered.

### Step 4 - InstanceStart becomes a message (core)

`CreateObject` stops calling the class node's function pointer and sends the
class a message instead. A compiled class answers it in C exactly as now; a
class that answers nothing gets a default. Same "stop doing, start asking"
as clone and serialize, and it is what allows a class whose constructor is a
script.

Done when: full suite green, and every existing class is created through the
message with no change to its own code.

### Step 5 - the trampoline (objects/script)

One generic trampoline registered as a handler record, carrying which host
and which function beside it. A script installs a function on any property -
its own, a sibling's, a cell's - and the engine cannot tell it from compiled
code. The script's return maps onto the verdicts, including declining so the
original runs and refusing so it does not.

Depends on 1 and 3. Not core: it is one module.

Done when: a script installs a function on another control's property, that
function runs on write, and passing through reaches the compiled handler
underneath.

### Step 6 - the ring of gestures (browser)

Control-click a control, get a circle of the verbs that apply to it: alias,
connect, disconnect, clone, move, delete, options - all existing verbs - plus
the two new wedges. Presentation choosing a target, one command per wedge,
scoped to the thing clicked instead of putting the whole window in a mode.

Awkward first is fine and probably right: a list with a box settles whether
the wedges work; a circle settles how it looks, and the browser is a
projector so that half is cheap to redo.

### Step 7 - the two wedges that make it worth having

**Format** is a `set-property` writing `GUI_Format`, which the client
already reads for masking and validation. No engine work; it is a wedge
around a verb that exists.

**Insert a function** uses step 5: pick a target - this control, a sibling,
a cell - name a function, and it is installed. Cells get formulas out of
this without cells being special, because a cell is a node like the rest.

### Show what is in your hand

A gesture in hand is a text label following the pointer - "move:
Slider_1", "clone: Textbox" - and that is enough to say WHAT is being
carried but nothing about where it will land. Carry an OUTLINE instead:
the picked-up thing's own size, drawn as a rectangle under the pointer,
so a drop is aimed rather than guessed. Move is where it bites first,
because a move already has a real size on screen and the placement is
exact; clone and alias want the same thing, and import wants the
outline of the view it is about to unpack.

Two details this is really asking about, both about pixels rather than
mechanism:

- **The grab offset.** The carry places at the pointer, where the old
  press-drag placed at the pointer MINUS where the thing was grabbed.
  Picking a widget up by its right edge and putting it down should not
  re-centre it on the cursor. The outline is what makes that visible,
  and fixing the offset without the outline is fixing something nobody
  can see.
- **What the outline says about the drop.** It is the natural place to
  show refusal before it happens - a view being dragged into itself, a
  container that will not take it - by drawing the outline differently
  rather than accepting the click and reporting an error afterwards.
  The engine already refuses (`ContainmentCycle`); this only shows it.

Cheap, and purely presentation: the client knows the element's size
because it is rendering it, so nothing new crosses the bridge.

### After that

Security, in the order the topic above sets out, and it can start any time
after step 3 because refusal is what it is built from. Remote spaces and
`dataobj://` after that, since a wire across machines is a subscription and
that machinery is not disturbed by any of the steps here.

Not on this path, independently useful, do whenever: the serializer's `Self`
special case, and the namespace offset.
