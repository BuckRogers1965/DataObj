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
