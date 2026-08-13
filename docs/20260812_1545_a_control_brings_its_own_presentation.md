# A control brings its own presentation

*2026-08-12. A plan, not a change. Nothing in `web/app.js` has been touched.*

The next piece of work is to stop `web/app.js` knowing what a Checkbox is.

Today a control is defined twice: once in `objects/checkbox/checkbox.c`, which
is what it IS, and once in `app.js` — an entry in `INPUT_WIDGET_CLASS`, a `case`
in `buildValueControl`, a `className ===` branch in the update path, sometimes a
whole bespoke maker function. Adding a control means editing the host. Forgetting
the client half fails **silently**: the lookup misses, the switch falls through,
and the control renders as a textbox with no error anywhere.

That is not a hypothetical. Every rendering failure on 2026-08-11 was one of
those ladders missing a rung, and each fix was another rung rather than a reason
to stop building ladders. The design this replaces it with is already argued in
ROADMAP.md under **"Presentation belongs to the control — and there is more than
one surface"**; this document is the route from here to there.

## The invariant

One sentence, and every step below is judged against it:

> The app gets the exact same information it always had, and the page looks and
> behaves exactly as it does today.

This is a **relocation, not a redesign**. No new protocol fields, no new event
types, no new engine mechanism. If a step needs one, the step is wrong.

## What is actually in the way — measured, not guessed

`web/app.js` is 2693 lines. The part that knows about specific controls:

| site | lines | what it does |
|---|---|---|
| `buildValueControl` | 449–502 | `switch` over class name, builds the element |
| `makeReadoutEl` | 503–539 | the display half of the same switch |
| `renderMarkdown` | 540–581 | one control's renderer, inline in the host |
| `makeMenuButtonEl` | 582–671 | a whole control, bespoke |
| `updateLiveControl` | 958–1006 | `widgetClass === …` ladder on every update |
| `makeSelfControl` / `makeSelfDisplay` | 1007–1028 | picks a maker by kind |
| `makeMoButtonEl` / `makeSelfActivateButton` | 1029–1086 | two more bespoke makers |
| `updateReadout` | 1462–1469 | four more `=== 'Class'` branches |
| the constant block | 40–57 | `PROP_*`, `WIDGET_CLASSES`, `INPUT_WIDGET_CLASS`, `DISPLAY_WIDGET_CLASS`, `READOUT_WIDGET_CLASSES` |

About **350 lines, 13% of the file, and 27 branches on a class-name literal**,
plus **13 rules in `style.css`** that name a control. The other 87% — wires,
panels, drag, modes, the event loop, addressing — is genuinely generic and is
not part of this work.

The palette already instantiates **43** classes at boot. So the target is
concrete: those 350 lines and 27 branches move out to 43 owners, and the host
ends up with none.

## The contract

Two decisions, and everything else follows.

**1. A control registers itself.**

```js
register('Checkbox', {
  create(ctx) { … return el; },     // ctx: {alias, prop, defaultValue, commit}
  css: '…',                          // optional, this control's rules only
});
```

One map, keyed by class name, filled by the controls themselves. No list of
known classes anywhere in the host — the same promise the engine already keeps
for `.object` loading.

**2. Every control answers `.value`.**

This is the part that stops the ladder moving instead of leaving. Splitting out
*creation* is only half the job: if the host still needs a `case` per class to
**update** a control, nothing has been fixed. So the element a control returns
must answer `.value` — get and set — and the host's entire update path becomes:

```js
el.value = incoming;
```

An LED's setter swaps its class name; Markdown's renders and assigns `innerHTML`;
Image's sets `src`; Checkbox's maps `'1'`/`'0'` to `checked`. Each of those is
three lines living with the control that needs them, and `updateLiveControl`,
`updateReadout` and `makeReadoutEl` all cease to exist.

**The precedent is already in the tree.** The Textbox is a contenteditable div
with exactly this shim (`app.js:487`), written for a different reason and
proving the shape works:

```js
Object.defineProperty(el, 'value', {
  get() { return this.innerText.replace(/\n$/, ''); },
  set(v) { this.innerText = v || ''; },
});
```

This is also the Pico W lesson from the ROADMAP note, in the idiom this codebase
already uses: one uniform accessor over every value-bearing element, so the
update path is written once.

**3. A missing presentation is loud.** The bug being retired is silence. A class
with no `show/web` renders a visibly wrong placeholder naming the class,
logs to the console, and logs on the engine side. It must never quietly fall
back to a Textbox — that is the exact failure this work exists to end.

## Where the presentation lives: a property on the class node

Not a directory the Bridge scans. **The control loads its own `.js` and `.css`
onto its class node, once, at `ClassStart`** — the same moment and the same way
it already publishes `Value`, `Enable`, its parent class and its version.

    ClassSelf
      Show
        web
          js      <- this control's registration code
          css     <- this control's rules

Properties are nodes and nodes hold properties, so this needs no new mechanism
whatsoever: `PublishProp` already puts things on a class node and the Bridge
already walks the registry. `web` is a sub-property rather than a prefix because
`rest`, `xml` and `macos` are the same shape later, and none of them is
privileged. (Names are provisional — they are yours to set.)

Three things fall out of it, and they are why this beats a directory convention:

- **Nothing scans anything.** The Bridge does not look at the filesystem, needs
  no path convention, and needs no notion of which classes have a web half. It
  walks the classes it already walks and reads a property, the way it already
  reads `Parent` and the published interface.
- **Once, at class time.** Not per instance, not per request, not per page load.
  A class is registered once per process, so the read happens once.
- **It ships inside the `.object`.** Customer support can still mean emailing a
  single file, and now the browser half travels in it. Exactly the deployment
  promise the engine half already keeps.

**How the text gets into the module.** Editing JavaScript inside a C string
literal would be miserable, so it should not be done. Keep
`objects/led/show/web/led.js` as an ordinary editable file and have that
object's Makefile generate a C string from it at build time (`xxd -i`, or three
lines of shell) for the module to publish at `ClassStart`. Source stays a `.js`
file with syntax highlighting; the shipped artifact is self-contained. That is a
per-object Makefile addition, which is where per-object build rules already live.

## How it reaches the browser

Three hops, and only the first is new work:

1. **The class holds it** (above).
2. **The Bridge collects it** — at instance start it walks the registry,
   concatenates every `Show/web/js` into one blob and every `…/css` into
   another, and holds them. One walk, one time; the same registry walk that
   already builds the palette and discovers script hosts.
3. **The web layer loads it, once.** Either the Bridge hands over the list or the
   client derives it from the classes it already knows — either way one time at
   page load, before the first `instance-created` is rendered.

The one remaining mechanical question is how the bytes get out, since
`Http_Serve` (objects/http/http.c:134) answers a request by `fopen`ing a path
under `Root` and cannot ask anyone for a body. Either Http learns to ask (a path
not on disk goes out as a message and a subscriber may answer — the
message-interface shape the I/O objects already use, and useful beyond this), or
the Bridge writes its assembled blob once at start into the directory Http
already serves. The second needs no new mechanism and can be swapped for the
first later without touching a single control, because the assembling code is
identical either way.

## The steps

The shape is the one the alias conversion had: **make one control work through
the general mechanism, then the rest are that control again.** Nothing is built
generically ahead of a working case.

Each step ends green on all five variants and is separately revertable.

**Step 0 — the golden snapshot.** Before anything moves: capture, per palette
class, the DOM structure, computed size and class list of its rendered element.
The only thing that can prove "identical" rather than "still passes". *Proof: a
guitest assertion comparing live rendering to the snapshot, passing on today's
code.*

**Step 1 — one control, end to end. The LED.** Easiest by measurement, not by
impression: its entire client footprint is **two executable branches**
(`app.js:527` sets `node-led state-0`, `app.js:1463` sets `node-led state-<v>`),
one entry in `DISPLAY_WIDGET_CLASS`, and five CSS rules. No gesture, no commit
path, no editing — it only displays. So it exercises the whole chain (class
property → Bridge → browser → renders → updates on a value) with the least
possible else in the way.

At the end of this step the LED renders from its own `.js`, its two branches are
gone from app.js, and all 42 other controls still work through the old path.
Both paths live at once, deliberately — that is what makes this one control's
worth of risk.

*Proof: the LED draws and changes colour on a value; the snapshot is unchanged
for all 43; guitest green.*

**Step 2 — the `.value` contract, proven on the LED.** Its element answers
`.value`, its setter swaps the class name, and the host's update path for it
becomes `el.value = incoming` with no branch. Prove the contract on one control
before asking 42 others to honour it.

**Step 3 — the rest, in batches, simplest first.** Label and TextOut (the LED's
shape), then Checkbox/Slider/Knob (input and commit), then Markdown/HTML/Image
(renderers), then MenuButton/Dropdown and the Buttons (their own gestures), then
the panels. Each batch is the LED again with more in it. *Proof each time: 43
drew, snapshot unchanged, no page errors.*

**Step 4 — CSS follows the same route.** The 13 control-specific rules move to
their controls' `Show/web/css`. *Proof: computed styles unchanged.*

**Step 5 — delete the scaffolding.** `WIDGET_CLASSES`, `INPUT_WIDGET_CLASS`,
`DISPLAY_WIDGET_CLASS`, `READOUT_WIDGET_CLASSES`, `buildValueControl`,
`makeReadoutEl`, the maker functions — only once nothing references them.
*Proof: ~350 lines and 27 class-name branches gone, everything still green.*

## What must not change

- **The protocol.** No new event, no new field, no new command. If a control
  needs something to render, the engine already sends it (`interface`,
  `classParent`, `reservedIn`/`reservedOut`, `gui`) or the control asks by
  ordinary means.
- **The engine.** No change to object.c, node.c, sched.c or widget.c for this.
  The Bridge assembling a blob is Bridge behaviour; a control's presentation is
  that control's business.
- **`.object` granularity.** A control's web half ships inside that control's
  directory. No shared bundle-of-everything in the source tree — the built blob
  is an artifact, not a source file.
- **Appearance.** Pixel-identical is the acceptance criterion, which is what
  Step 0 exists to make checkable.

## How we will know early that this is going wrong

The signal from ROADMAP.md's **"The asymmetry, and using it as a signal"**
applies directly, and this plan is deliberately shaped so it can fire early:

- If a step takes **minutes and is quiet**, the mechanism was already general.
- If a step is **loud and spreads**, something is being special-cased — most
  likely a control that needs the host to know something specific about it. The
  answer is to find what the host is actually being asked for and give *every*
  control that, not to add a branch for the one.

The most likely place for that is a control whose *gesture* the host still owns
(MenuButton's dropdown, the wire dots, the drag handles). If those resist, the
honest move is to stop and record the boundary rather than push the ladder into
a new file.

## What this unlocks, and what it deliberately does not

It unlocks the rest of that ROADMAP section: `show/rest`,
`show/xml`, `show/macos` become directories a control can grow,
because `show/web` stopped being special by being the only one. And it
settles what "adding a control" means end to end — drop the `.object` in the
scan path, restart, and its browser half is in the blob the Bridge serves. The
same deployment story as the engine half, at the same moment, with no host edit.

It does **not** do the other surfaces, and it does not touch the `toTop`
z-order compression, the naming question, or the both-ends wire record — all of
which are separate open items in ROADMAP.md and none of which this work needs.

## The net that is under it

This plan is only safe because of what the harness now asserts, as of today:

- **43 palette classes render**, each named individually when it does not — not
  a count. The palette is the enumeration, so a control added tomorrow is
  covered without anyone maintaining a list.
- **The client holds every palette member the engine placed**, comparing what
  arrived on the wire against what the client built. That is the invariant of
  this whole document, as an assertion.
- **Uncaught errors and promise rejections fail the run**, armed before the page
  runs a line and attributed to the test that caused them. Under fragment
  loading, this is the difference between a named failure and an unrelated
  timeout three tests later.

Known gap, worth saying plainly: **nothing diffs appearance.** All 43 render at
the right size, but only 8 classes get real gestures, and no test compares CSS
or z-order. Step 0's snapshot is what closes the first half of that, and it is
Step 0 for exactly that reason.

---

## What actually happened (appended 2026-08-12, evening)

The plan above was written before any code moved. This is the result, the work
left, and the one thing that went sideways.

### Done

**Sixteen classes own their browser half.** Fourteen controls, then View, then
Control itself. Each carries `show/web/<name>.js` (and `.css` where the rules
are that control's alone) on its class node, published at `ClassStart`, turned
into C strings at build time so the browser half ships **inside** the
`.object`. **1292 lines now live with the classes that own them**, and
`web/app.js` went from 2693 lines to 2102.

**Two shared pieces, hoisted only after the LED proved the shape:**
`objects/show.mk` (one make include, one line per control) and
`PublishShow(class, renders, js, css)` beside `PublishPosition` in control.c.
Neither was written in advance - the LED did it the long way first, and the
repetition, not the plan, said what the shared version should look like.

**The shape decision is gone.** No `classParent === 'Control' ? atom : view`
anywhere. The host walks the chain - class, then parent, then View - and asks
whoever answers first. A class that renders itself takes over its whole
branch, which is what makes a different base class possible without touching
the host.

**Failure is loud.** A class with no registration renders a visible
`?ClassName` and logs. The silent fallback to a textbox - the original disease,
the thing that made a missing rung look like a working control - is gone.

### What the tests caught, which is the part worth reading

Three real defects, none of which would have been found by looking:

- **The class-renders-type bug, caught before it shipped.** The Bridge first
  INFERRED which property type a class renders from that class's own `Value`
  type. It worked for the LED and was wrong in general: `mobutton` publishes
  its `Value` as `PROP_LED` and `button` as `PROP_NULL`, so MoButton would
  have stolen the LED's type and Button would have claimed every plumbing
  property. Now the class STATES it. The rule this is an instance of: a fact
  about a class belongs to the class, and inferring it from a neighbouring
  fact is a coincidence waiting to be found by someone else.
- **A functional regression the golden snapshot named in one line.** Atoms
  built from a module never subscribed to their own `Value` - an LED atom
  would never have lit. It surfaced as `LED.kids` differing by one CSS class,
  because the old code's update WIPED that class and the new code never got an
  update to wipe it with. Nobody would have thought to assert that.
- **A CSS extractor that split a multi-line compound selector**, orphaning its
  first line and swallowing the next rule. Every atom went 190px wide. The
  snapshot named all 43 at once; the fix took two minutes instead of an
  afternoon.

Two test READS also had to change - they asserted through `selfDisplays[...]`,
a host bookkeeping map, rather than through what is on the page. Which map a
rendering is filed under is the host's business and changes when a control
brings its own presentation; what the user sees does not. Assert on the page.

### Phase 2 - what is left, and whose it is

`app.js` is 2102 lines. Roughly 600 of them are genuinely the host's: the
socket, `send`, event routing, the loader, mode relay, one mount point.
Everything else has a name on it:

| lines | what | owner |
|---|---|---|
| ~300 | the wire layer - `drawWire`, `updateWire`, `completeWire`, `onConnected`, anchors, dots | **View** - it already draws per-view wire layers |
| 220 | `startDrag` | **Control / View** - a drag is a write to X/Y |
| ~130 | the `gui*` family - mask, validate, reformat | **Control** - an annotation on the data a control carries |
| ~120 | `openFlowDialog` / `closeFlowDialog` / `onFlowFile` | **Serializer** - they are its verbs |
| 89 | `makeMenuButtonEl` | **MenuButton** |
| 32 | `dropTargetAt` | **View** - where a drop lands is containment |
| 152 | `registerAliasAtom` / `renderAliasControl` | **nobody - deleted** (below) |

That lands the host near 600 lines: socket, verbs, routing, loader, mount. At
which point "someone deploys a different base class and gets a different web
client for their model" stops being an argument and becomes a deployment.

One correction to the plan above, found by doing it: `renders` currently sits
under `Show` because it was added for the web assembly. If the CORE needs it -
and the alias work below says it does - then it is not presentation. It is a
class fact that presentation happens to use, and it belongs on the class.

### How the alias derailed us

Control and View both moved cleanly: cut the functions out, drop them in the
class's `show/web`, register, done, green. Alias is a control, so its
rendering was next, and the same move - the same script, the same shape - was
applied to `registerAliasAtom` and `renderAliasControl`.

It failed. The Alias instance was created, `registerAliasAtom` demonstrably
ran (its X/Y/Container subscriptions arrived), there were no page errors, and
`aliasAtoms[...].target` never populated. Nothing in the logs explained it. It
was reverted, including a CSS rule that had moved out with it - which no test
covered, so it would have shipped as a silent visual regression.

Then the questions that mattered arrived, and they were not about the move:

> *"I don't understand what alias does in the browser, it is not a visual
> thing."*
> *"There is nothing that is an alias control visible."*
> *"The controls are just controls, they redirect internally."*

All true. The engine already resolves the link on every read and write
(`ResolvePort` under `Bridge_Subscribe` and `SetOrDeliverProp`), so the browser
never needs to know a redirect is there. What you see is a slider. The 156
lines being relocated were a client-side reimplementation of something the
engine does for free - and:

> *"Aliasing is a core thing, not loaded as an object. You can alias any
> object."*
> *"That is not dead code, that is where it lives, that is why it exists."*

`AliasProperty` and `CreateAlias` have sat in `object.c` with no callers since
before this work started. Not dead - **the seam**. The Bridge went around them
and did the job itself with `CreateObject(home, "Alias")`, which is where a
loadable class, a hard dependency, a `Widget` stamp persisted into saved flows,
a `strcmp(className, "Alias")` in the serializer, and 156 lines of client
rendering all came from. One detour, five consequences.

**The lesson, and it generalises the signal in ROADMAP.md.** Two relocations of
the same shape had just worked in minutes. The third failed inexplicably.
The temptation was to debug the move. The answer was that **the thing being
moved had no business existing** - and no amount of care with the relocation
would have found that, because a relocation cannot tell you a thing is
imaginary. So: when a move that should be mechanical is not, suspect the
THING, not the move. An hour of failing to relocate something is evidence
about what it is.

Filed as its own roadmap entry, deliberately not attempted at the end of a
long session: it is a deletion touching `Bridge_CreateAlias`, the
internals-view builder, the import pass and the client at once. And it is
smaller than it looks - not "write a core verb" but "delete three private
copies of one."
