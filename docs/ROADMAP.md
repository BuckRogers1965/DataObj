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
3. **Wiring**: Connect()/SndMsg, exactly the Reader.Out → Writer.In
   wiring already built, now reaching every property once Phase 2.3
   lands, not just ones with handlers — driven by Connect mode's two-click
   source/destination gesture (Phase 4.6), not a dedicated dot
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
2. **Web API wrapper classes**: generated from OpenAPI/REST
   descriptions — each endpoint a palette object with typed input
   properties and a response Out property. Webhook receiver object for
   the inbound direction (a route on the HTTP server → an Out property).
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
