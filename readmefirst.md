# READ ME FIRST — the bridge has a wrong side, and I keep building on it

This document exists because a pattern of mistakes kept recurring while
building the web GUI. Read it before touching `web/app.js`, before adding a
Bridge verb, before implementing any feature that was described in GUI terms.

## The law

**Nothing on the GUI side of the bridge is real.**

The engine owns every object, every property, every relationship, every
behavior. The GUI is one big alias — a projector pointed at the session.
A browser window does exactly two things:

1. Translates a raw user gesture into **one Bridge verb carrying the whole
   intent** ("clone X into C at x,y").
2. Renders the events the Bridge sends back.

That is the entire job. If the GUI creates something, names something,
decides something, owns something, or remembers something the engine
doesn't, it is fake — a second implementation of the system that will
drift, leak, and lie. CLAUDE.md already states the corollary for main.c:
*"features never go in main.c or the host — if a feature is tempting
there, it's an object."* The browser is just another host. The same law
applies, and I repeatedly failed to apply it.

## What was done wrong (the inventory)

These are real things currently living in `web/app.js` that are the
engine's business. Each one worked, passed its tests, and is still wrong,
because the tests checked *behavior*, not *where the behavior lives*.

1. **Hidden helper widgets** (`makeInputWidget` / `makeDisplayWidget` /
   `makeButtonWidget`). The client invents an object's control surface:
   it creates real instances (`create-instance hidden:1`), binds them, and
   bookkeeps them in `widgets{}`. This caused an actual memory leak (one
   set per page load, cleaned up only after per-connection ownership was
   bolted on). The engine now builds a real internals view — one Alias per
   published property — which makes this entire mechanism a redundant,
   GUI-side fake. It still exists. It must die.

2. **The node-box card panel.** `registerCard` builds a parallel,
   client-coded answer to "what is an object's panel?" — property rows,
   port rows, a footer — out of the class Interface. The engine's answer
   is the internals view. There must be exactly one answer. The card's
   panel should BE the engine's internals view, presented; the client-side
   panel builder should be deleted.

3. **Client-side naming.** `aliasCounters` / `widgetAliasCounters` mint
   identities (`/Root/Slider1`, `/Root/Alias3`) in the browser. Identity
   is the engine's to give. The server already names clones and internals
   members; `create-instance` and `create-alias` should name everything
   (client `as` optional at most), and the client should learn names from
   events.

4. **Multi-command births.** Creating a thing from the GUI sends
   create + set X + set Y + set Container as four commands. Placement is
   part of the birth. `clone-instance` already does this right (one verb
   with `container/x/y`); `create-instance` and `create-alias` don't.
   Every multi-command gesture is a race the raw protocol user can lose
   and the flow log records as fragments.

5. **Client-side decisions dressed as rendering.** `renderAliasControl`
   consults the *target's* class Interface to decide what widget an alias
   should be, and special-cases alias-of-Open into an icon. Deciding a
   default presentation from the schema is engine work (stamp `Widget` on
   the Alias at creation); the client should render what the property
   says, not deduce it.

6. **Optimistic mutation.** The delete gesture removes the rendering
   locally before the server confirms. For a moment the GUI is the source
   of truth about what exists. It never is. Send the verb; act on the
   event.

7. **History (already repaired, listed so the shape is recognized):**
   clone was once implemented as a client-side loop of set-properties
   copied from a client-side cache (`propertyValues`) — it cloned only
   what one window happened to have seen. The fix was `CloneObject` in the
   engine. That is the template for every repair on this list: the moment
   the mechanism moved into the engine, deep clone, alias remapping, and
   protocol parity fell out for free.

## Why this kept happening

Honest causes, so they can be watched for:

- **Habit.** The dominant pattern in modern web development is "the
  frontend is the application; the server is an API." My instincts are
  trained on thousands of codebases shaped that way. This project is the
  opposite: the engine is the application; the browser is a dumb terminal
  with nice fonts. Under time pressure, instinct wins unless checked.

- **Features arrive described in GUI words.** "Click a thing and a panel
  opens" *sounds like* JavaScript. The words name the gesture, not the
  mechanism. I implemented at the location of the words instead of asking
  what object, property, or verb the engine is missing.

- **The wrong pattern was locally consistent.** app.js already had hidden
  helpers, client naming, staged positions. Extending an existing pattern
  feels like good citizenship — "match the surrounding code." When the
  surrounding code is on the wrong side of the bridge, consistency
  compounds the error.

- **The tests couldn't see it.** The harness drives the browser and checks
  outcomes. A fake implemented client-side passes every one of those
  checks. Green tests made wrong architecture invisible.

## How to stop making this mistake

Before writing anything in `web/app.js`, apply these tests **in order**:

1. **The raw-client test.** Could the raw JSON protocol user (the
   `nc`-to-port-8091 user, the future MCP agent) get this exact feature
   with **one command**? If not, the feature belongs in the engine and the
   missing thing is a verb, a property, or an object. Build it there
   first. Prove it with the raw client first. Only then present it.

2. **The dumb-terminal test.** Could a second, stupider client — curses,
   a phone, a shell script — reproduce this behavior with zero logic,
   just verbs out and events in? If the answer requires "well, it would
   have to know…" then knowledge is leaking to the wrong side.

3. **The vocabulary test.** If the diff introduces client-side creation,
   client-side names, client-side counters, client-side copies of engine
   state used for decisions (not display), or more than one command per
   gesture — stop. Wrong side.

4. **The harness rule.** Every new GUI test scenario must have a
   raw-protocol twin that exercises the same mechanism without a browser.
   If the twin can't be written, the mechanism is in the browser, and
   that's the bug. (The GUI tests then only prove *presentation*:
   gestures emit the right verb, events paint the right pixels.)

What the client is *allowed* to keep: input interpretation (which element
was clicked, where a drop landed — those coordinates are genuine user
input), per-window presentation (which panels this window currently
shows, scroll positions), and display caches used only for display.
Nothing else.

## The repair list for the current engine

In order. Each item is done when the raw client can do it in one command
and app.js got smaller.

1. **Atomic birth.** `create-instance` and `create-alias` accept
   `container`, `x`, `y`, and name things server-side (`as` optional).
   One verb, one fully-placed thing, one flow-log line.

2. **Engine-stamped presentation defaults.** `create-alias` and the
   internals builder set the Alias's `Widget` from the target property's
   published widget type. The client renders `Widget`, never deduces it.

3. **The internals view becomes the only panel.** An Operate-mode click
   on a card icon asks `internals`, same as Options mode (Options can then
   mean "even the plumbing rows"; or the two collapse into one). The
   node-box builder, `makeInputWidget`, `makeDisplayWidget`,
   `makeButtonWidget`, `widgets{}`, and hidden helper creation are deleted
   from app.js. The per-connection `_OwnerConn` sweep machinery in
   bridge.c loses its main customer and likely simplifies too.

4. **One-verb move.** `move-instance` (`of`, `container`, `x`, `y`):
   engine validates (a view cannot enter itself), re-containers, renames,
   emits. The client's drop handler sends it and stops sequencing
   X/Y/Container writes.

5. **Event-driven delete.** The client stops removing renderings
   optimistically; `instance-removed` is the only remover.

6. **Then the recursion pays off:** with panels being real views of real
   aliases, "save my panel layout," "clone my panel," "a composite
   object's published face" are all engine facts already — no GUI work at
   all beyond presenting them.

## The one-sentence version

**If it matters, it's in the engine; the GUI only asks and shows.**
When a feature feels like it wants JavaScript, that feeling is the bug.

## Status (2026-07-15): the repair list is executed

All five repairs landed, each proven raw-first (testharness/rawtest.py,
port 8091 — the harness rule is now enforced by run.sh) and then
presented, with the GUI pixel- and behavior-identical throughout
(testharness/guitest.py all green, unchanged expectations):

1. **Atomic birth** — `create-instance` carries container/x/y and the
   server names the result (`as` optional, still honored for replay).
   The client-side birth path (`createInstance`, `aliasCounters`,
   `pendingPositions`/`pendingContainers`) is deleted.
2. **Engine-stamped presentation** — `create-alias` and the internals
   builder stamp `Widget`/`Direction` from the target's published
   Interface (`InterfacePropForInstance`, object.c). `Open` publishes
   `PROP_ICON`, so the doorway rendering is a stamped fact, not a
   property-name special case. `renderAliasControl` deduces nothing.
3. **The card panel is the internals view, presented** — a card's rows
   are the engine's member aliases (same members Options mode shows as
   the dissection table; two projections of one panel), its icon LED a
   plain subscribed readout, its Activate button the `activate` verb.
   `makeInputWidget`/`makeDisplayWidget`/`makeButtonWidget`, `widgets{}`,
   the client name mint, and the whole hidden-helper species are deleted;
   the `_OwnerConn` sweep went with them (nothing is per-connection).
4. **One-verb move** — `move-instance` (`of`/`container`/`x`/`y`), on
   `MoveInstance`/`PlaceInstance` in object.c; the engine refuses
   containment cycles. The drop handler sends exactly one command.
5. **Event-driven delete** — the optimistic local removal is gone;
   `instance-removed` is the only remover.

Placement rule that emerged during the work (the VNOS lesson): a verb's
MECHANISM lives in object.c as a language-neutral engine call
(PlaceInstance, MoveInstance, InterfacePropForInstance, alongside
CreateObject/CloneObject/Connect/SetOrDeliverProp/ActivateInstance/
DeleteInstance); bridge.c is JSON syntax, session naming, and eventing
only. The Script object and the future MCP server bind to the same
engine calls — never to the bridge, never to re-implementations.

## Status (2026-07-16): clone / rename / load, the same law again

A long debugging session on cloning a view, renaming it, and saving/
loading. Every bug was the same disease as the repair list, and the
fixes are worth reading before touching clone, rename, or load:

1. **The engine clones AND names; the bridge only translates.** Deep-
   cloning a view — its members, its aliases re-pointed at the copies,
   the wires re-made between the copies — was living in bridge.c
   (`Bridge_CloneViewMembers`, `Bridge_CloneAliasMember`, `Bridge_CloneOne`)
   with the walk and the naming both there. Wrong side. It is now one
   engine operation, `CloneView` (object.c): the caller passes only the
   thing to clone and the container to put it in; the ENGINE walks the
   members (by their `Container`, in its own registry), copies them,
   remaps the alias links and the wires, and mints the name (registry-
   unique, source-name with any trailing `_N` stripped — the `Slider_1`
   → `Slider_1_1` bug was the bridge naming it). `Bridge_CloneCmd` is now
   resolve → `CloneView` → walk the result into paths and events. Same
   clone whether a script or the html asks.

2. **A rename or move is a RE-PATH OF THE WHOLE SUBTREE, not a re-key of
   one node.** Containment is a string path (`Container = "/Root/View_1"`),
   so renaming `View_1`→`slider` must swap that prefix everywhere it
   appears or the members are orphaned: a clone finds zero of them, a set
   by the new path misses. `Bridge_RepathSubtree` (bridge.c) now rewrites,
   across the subtree: each descendant's **alias key**, its **`Container`**,
   any alias's **`Target` string** that named the old path (the link is by
   pointer and survives, but the GUI binds by the Target string — a stale
   Target is why a renamed view's own alias looked dead while a clone's
   worked), and every viewing connection's **scope key**. Partial re-path
   = subtle, invisible breakage. Both `Bridge_Rename` (move) and
   `Bridge_RenameName` (rename) call it.

3. **A property-to-property wire has an ADAPTER as its sink, not the
   instance.** Connecting a Slider's `Value` to another's `Value` (a
   property with no message handler) routes through a `PropertyBinding`
   adapter (object.c) — so the source port's Subscriber points at the
   nameless adapter, and the real target lives on the adapter's
   `Target`/`TargetProp`. Anything walking subscribers (CloneConnections,
   and by the same token any future scrub) must recognise the adapter and
   read through it, or it silently drops the user's connection.

4. **Load is action-replay, and a client that renders through the replay
   burst races its own subscribes.** The GUI subscribed to `/Root/View_1`
   for position, the load renamed it to `slider` before the subscribe
   arrived, and the subscribe resolved to nothing — the view came up at
   the wrong spot. A projector RE-PROJECTS after a bulk state change: the
   GUI now reloads on `flow-loaded` and re-lists the engine's final state.
   (Roadmap 3a/3b — direct node serialization + subtree mount — retires
   replay and this whole race class.)

Two working-discipline lessons that cost the most time, recorded so they
are not relearned:

- **Do not argue with the user's observation using a passing test.** A
  green test that never failed for the reported reason proves nothing; it
  means the test does not exercise the bug. Reproduce the REPORTED bug,
  watch it fail, then fix. (Every bug here was real; each of my "the
  engine is fine, look at my test" replies was wrong.)
- **Use the standard debug framework; make failures loud.** The killer
  race was invisible for many messages because `Bridge_Error` only told
  the client, never the log. Route failures through `DebugPrint` (it now
  logs at ERROR, always), trace a new subsystem with its OWN category
  gated to a high `-v` level (the `CLONE` category), and observe before
  concluding. A silent failure is a debugging session you pay for twice.

## Status (2026-07-16, later): connections — the adapter was the last fake

Connect mode showed no existing wires, and a new wire drew onto the root
canvas instead of into the view. Both were the same law again, and the
fix retired an interim mechanism the roadmap had already sentenced
(Phase 2.3):

1. **The engine had two kinds of wire, and the walkers could only see
   one.** A wire into a compiled port recorded the real sink; a wire
   into a plain property (the GUI's own Value→Value gesture, always)
   hid behind a nameless `PropertyBinding` adapter — invisible to
   `list-connections`' graph walk, special-cased in `CloneConnections`,
   invisible to the delete scrub (a freed sink left the adapter's
   `Target` dangling — a live use-after-free), and leaked besides.
   Now a Subscriber records `{Instance, Port, Callback}` and delivery
   with no Callback applies the universal default — store what arrived
   (`DeliverToSubscriber`, node.c, shared by both fan-out walkers).
   `Activate` is an ordinary port (`ActivateOnMsg`, stamped by
   `RegisterInstance`). The adapters are deleted; every graph walker
   reads the same records; bind-property/bind-activate are dispatch
   synonyms for `connect` kept only for flow replay.

2. **A wire was never announced.** `Bridge_Connect` succeeded silently,
   so the clicking window drew its own line (repair #6's optimistic
   mutation, still alive in `onPortClick`) and no other window ever
   learned. Now `connected`/`disconnected` events go to every
   connection viewing the endpoints' containers, and the client draws
   and erases ONLY from those events — the exact shape of repair #5.
   `disconnect` (engine `Disconnect()`, Connect's inverse — it never
   had one) drives the mid-wire ×.

3. **The one genuinely-GUI bug was DOM nesting, not coordinates.** One
   root SVG sat behind every view panel (z-index), so a same-view wire
   painted "onto the root". Each view-inner now grows its own wire
   layer; a wire renders in the deepest view both endpoints share and
   travels/hides with it — the same correct-nesting rule the members
   themselves follow. Only container-spanning wires use the root
   overlay.

Raw twins: testharness/connectiontest.py (listing, announcing,
disconnect, delete-scrub, chaining, Activate wires — all one command
each). Presentation twin: guitest's connect-wires (event-drawn, in the
view's layer, redrawn on mode re-entry, × removes). New `WIRE` debug
category traces every Connect/Disconnect/scrub at `-v 3`.

## Status (2026-07-17): addressing moved into the engine

Phase 1.5 landed as groundwork for the script-language hosts (which
are Bridges whose wire is function calls — same verbs, same paths):
path → instance is now ONE engine index (the namespace trie) instead
of a per-bridge alias table, and the reverse direction is DERIVED from
Name + Container, verified by resolving back. Lessons paid for:

1. **A derived reverse lookup must be captured BEFORE mutating what it
   derives from.** Bridge_Set wrote the new Name first, then asked for
   the instance's path — which now derived from the already-changed
   Name, failed verification, and silently skipped the re-key. Capture
   the current path, then write. (And the skip is loud now.)
2. **Deleting from a shared structure needs tests for SHARED parts.**
   NSDelete's tail-chop freed trie chains still shared by sibling keys
   (deleting Slider_9 destroyed Slider_8). The validation had covered
   prefix-overlap but not last-character siblings — the case the
   session mints constantly (`_8`/`_9`). Rewritten as walk-down/unwind:
   free only nodes carrying no other key, re-link the sibling level.
3. **One namespace is a semantic upgrade that tests can trip over:**
   instances created over the raw port are now addressable from the
   GUI (before, each bridge's private table hid them — a split-brain
   nobody had noticed). GUI tests that grabbed "any /Root/View_N"
   started grabbing the raw suites' leftovers; they now snapshot
   before creating and match only the NEW name. The engine behavior
   is the correct one: one session, one truth.

## Status (2026-07-21): the root is a real View, and everything lives in it

A long session that started with the GUI unusable and ended with the
containment model made honest. The through-line: there was no real
"root" — the bridge fabricated a `/Root/` prefix for anything with no
container, and three places compared against the literal string
`"/Root"` to mean "nowhere". Making that real fixed a whole class of
bugs, and broke several things on the way that had quietly depended on
the fiction. What landed:

1. **A root IS a View, created at boot.** `CreateRoot` (object.c) makes
   an ordinary View with no container — the one thing allowed to have
   none, because it is what locations are measured from. The app builds
   `/Root` at startup (CreateDefaultApp, main.c) and the menus and the
   palette are ordinary instances IN it: no chrome category, no palette
   category, no "root" special case. The Bridge is handed the root view
   and walks views; the client renders `/Root` as the canvas. As many
   roots as you like, eventually one per login.

2. **Everything is created in a container, at creation.** `CreateObject`
   now REFUSES a NULL/unaddressable container — it logs the reason (the
   `PLACE` debug category at `-v 3`, or ERROR on refusal) and returns
   NULL rather than dropping the instance in the root. "No object is
   created nowhere." Main is a real place (`/Main`), so the web-flow
   plumbing (TCP/Router/Http/WebSocket/Bridge) is created in it — not a
   view, so nothing on a canvas lives there, which is the point.
   Objects that build private helpers (ScriptBox's language host,
   TCPPort's TCP engine) create them inside themselves.

   Known-open from this: an object cannot build its own settings
   controls during `InstanceStart`, because it has no path yet — that
   is what the ~44 boot-time refusals are. `BuildSettingsView` needs to
   run after the instance is placed.

3. **One clone entry, not two.** `CloneView` is renamed `CloneInstance`
   and is the only public clone: you clone a THING, and if it holds
   members (a view, a view nested in a view) they come too, aliases
   re-pointed and wires re-made, recursing to any depth. `CloneObject`
   is now a static internal node-copy. The bridge calls `CloneInstance`
   for a slider or a whole view alike — no caller ever picks.

4. **Cloning a view keeps its aliases.** `CloneAliasNode` was making the
   cloned alias with `CreateObject(NULL, ...)`, which the new refusal
   silently dropped. It now instantiates the Alias directly through its
   InstanceStart (exactly how concrete members clone) and sets Container
   as a string — because during a clone the container is only a path
   string, nothing is registered in the index yet.

5. **A view cannot be cloned into itself.** `Bridge_CloneCmd` checks
   `ContainmentCycle` (the same guard Move uses) and refuses dropping a
   view's icon into its own panel, which would build clones inside
   clones forever. Raw twin: viewclonetest's clone-into-self.

6. **Deleting a container deletes its whole subtree.** Containment is a
   Container property, not tree parentage, so `DeleteInstance` on a view
   touched only the view node and orphaned every member's registered
   path. The next clone reusing that name collided with the stale
   entries and came up empty (intermittent "controls no longer show").
   `Bridge_Delete` now walks the subtree — container plus every
   descendant by path prefix — and unregisters, deletes, compacts the
   flow log, frees taps, and emits an instance-removed for each.

7. **Two scoping bugs, same root cause: events are scoped by container
   key, and `Bridge_ViewKey` maps "" -> "/" but "/Root" -> "/Root".**
   - create-instance with no container placed the instance in `/Root`
     but scoped its event to `""` — the client, viewing `/Root`, never
     heard it and hung. Fixed: no container means the root view all the
     way through (lookup, placement, event scope).
   - the subtree-delete scoped a top-level view's removal to `""` (a
     `/Root`->`""` collapse copied from the rename walk), so its icon
     and panel lingered until a reload while its members vanished
     correctly. Fixed: scope to the exact container, no collapse.

8. **Load no longer reloads the page.** The `flow-loaded` handler did a
   full `location.reload()` — a 3-second glitch on top of an
   already-correct incremental render. The subscribe/rename race it
   guarded is gone (one engine index, everything created in its real
   container), so it is removed. If a load ever comes up wrong, re-list
   from the engine's final state — never reload.

Working-discipline notes (these cost the most time):

- **Read the project, every change, not once.** The single most
  expensive failure of the session was re-inventing mechanisms that
  already existed — `BuildSettingsView` (the panel builder, already used
  by reader.c/out.c), `View.Mode` (per-view mode pinning), and burning
  hours building panel construction in the BRIDGE before remembering the
  bridge only presents nodes. A summary tells you the philosophy; it
  does not tell you the function two files over already does this.
- **The user watching the live browser beats a flaky CDP test.** A test
  that "showed" a slider vanishing after load was reading mid-reconnect;
  the load was fine. When the user's live observation and an automated
  check disagree, the check is the thing to doubt first.
- **A view's members only render while its panel is OPEN** (events are
  scoped to viewed containers). A closed panel showing no members is not
  a missing-member bug. To check a container's contents, list it.

See FINDINGS.md for the still-open items: the read-through-a-link gap
(an alias writes through but reads back None) and the InstanceStart
settings-control timing.
