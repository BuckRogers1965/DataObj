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

1. **Extend the widget vocabulary**: PROP_SLIDER, PROP_VUMETER,
   PROP_TEXTOUT, PROP_KNOB, PROP_LABEL alongside the existing
   PROP_TEXTBOX / PROP_LED / PROP_BUTTON / PROP_CHECKBOX.
2. **Revive the Intercept path**: the parked machinery in
   `SetPropLongOLD` (node.c) fires a callback when a property is set.
   Un-park it in the live `SetProp*` functions (and fix
   shadow-vs-update while in there). This is how a browser slider
   moving a property reaches the object's OnChange handler.
   *Already present: the whole mechanism, parked.*
3. **Property change events outward**: when an object updates a
   property (Enable already mirrors "1"/"0"), interested parties get
   a message. Properties are nodes; give them subscribers exactly
   like ports. LEDs and VU meters in the browser are just
   subscriptions to properties.
4. **Skin every existing object**: declare widgets for Reader
   (filename textbox, run/stop buttons, status LED — reader.c already
   sketches exactly this), Writer, Pulse (interval knob, enable LED),
   Filter (mode dropdown), Queue (depth VU meter!), TCP (port box,
   connection LED), Out (text output).
5. **Skin descriptors**: per-class layout (positions, labels, look)
   as a node tree loaded from a file, falling back to a generated
   default from the published interface.
   *Already present: "skins for objects load from an xml property
   file" comment in main.c.*

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
4. **Live taps**: "subscribe" attaches a JSON-emitting probe variant
   to any port or property, streaming over the socket. The instrument
   panel is a bundle of taps.
5. **Sessions and login**: users as nodes (`Main/Users/<name>`), each
   with canvas containers; token auth first, TLS later.
   *Already present: objects/network/testkey/ certs, SSL code paths
   in the VNOS reference TCPObject.c.*

## Phase 4 — The browser client

1. **Palette** rendered from the published interfaces (Phase 1.4)
   fetched over the bridge.
2. **Canvas**: drag from palette → create-instance command; drag
   between ports → connect command. The canvas document is the flow
   file (Phase 1.3) — saving a canvas and saving a flow are the same
   act.
3. **Two views of one container**: flow view (boxes and wires) and
   panel view (just the widgets, arranged as an instrument panel).
   *Already present: the multi-view idea in the CreateContainer
   comments.*
4. **Widgets**: LED, slider, VU meter, text output, button, checkbox,
   textbox — each a small component bound to a property subscription
   (render on property-changed, send set-property on input, throttled).

## Phase 5 — Flows become objects (composition)

1. **Container ports**: a container publishes named In/Out ports that
   alias ports of inner instances; SndMsg to a container port routes
   inward. One new subscription-record type.
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
