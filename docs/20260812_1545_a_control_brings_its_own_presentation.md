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
with no `presentation/web` renders a visibly wrong placeholder naming the class,
logs to the console, and logs on the engine side. It must never quietly fall
back to a Textbox — that is the exact failure this work exists to end.

## The one open decision: how the blob is served

The intent (stated 2026-08-11, recorded in the ROADMAP): **the Bridge walks the
registered classes at instance start, concatenates every `presentation/web` it
finds into one JS blob and one CSS blob, and holds them in RAM.** The palette
shows one of everything, so the page needs all of it anyway — selecting a subset
would be work done to save nothing.

The obstacle is that `Http_Serve` (objects/http/http.c:134) answers a request by
`fopen`ing a path under `Root`. It has no way to ask anyone for a body. Two ways
to close that, and this is a decision to make before Step 3, not during it:

- **(a) Http learns to ask.** A path that is not on disk goes out as a message,
  and a subscriber may answer with a body. This is the message-interface shape
  the I/O objects already use — send a request carrying your callback, catch the
  reply — and it makes Http generally useful, not just useful here. Cost: it is
  an object-level change to http.c, and responses become genuinely asynchronous,
  so "nobody answered" needs a defined outcome.
- **(b) The Bridge writes the blob once at start**, into the directory Http
  already serves, and Http serves it as an ordinary file. Zero new mechanism.
  Built once rather than per request, which was the point of "cached in RAM".
  Bonus: the assembled file is on disk and can be read when a control's fragment
  is malformed — for a build step, that is a feature, not a leak.

**Recommendation: (b) for this work, (a) as its own piece later if wanted.** (b)
is not an interim mechanism that has to be unwound — the assembling code is the
same either way; only the last two lines differ. But it is your call, and (a) is
closer to what you described.

## The steps

Each step ends green on all five variants, and each is separately revertable.

**Step 0 — the golden snapshot.** Before anything moves: capture, per palette
class, the DOM structure, computed size and class list of its rendered element.
Store it as the expected shape. This is the only thing that can prove "identical"
rather than "still passes". *Proof: a new guitest assertion that compares live
rendering against the snapshot, passing on today's code.*

**Step 1 — the `.value` contract, in place.** Give every control class a `.value`
get/set on the element it already builds, and rewrite `updateLiveControl` /
`updateReadout` to use it. No files move, no loading, nothing new — this is a
refactor inside app.js that deletes two ladders. *Proof: the golden snapshot is
unchanged, all 43 still draw, guitest green.*

**Step 2 — the registry, still inside app.js.** Introduce `register()` and the
class map; convert each existing maker into a registration at the bottom of
app.js. Still one file, still no loading. At the end of this step the host reads
from the map and has no class-name branch left. *Proof: `grep -c "=== '[A-Z]"`
on app.js goes 27 → 0; snapshot unchanged.*

**Step 3 — the delivery path, empty.** Bridge assembles a blob from zero
`presentation/web` directories, serves it, `index.html` loads it before app.js.
Nothing has moved yet, so the blob is empty and the page is identical. *Proof:
page loads, snapshot unchanged. This step proves the plumbing alone.*

**Step 4 — one control moves.** Checkbox: `objects/checkbox/presentation/web/
checkbox.js` gets its registration, app.js loses it. One control, one commit.
*Proof: Checkbox still draws and still toggles under gesture; the other 42
untouched.*

**Step 5 — the rest, in small batches.** Grouped by shape, simplest first:
LED/Label/TextOut (display only), Slider/Knob (input), then Markdown/HTML/Image
(renderers), then MenuButton/Dropdown and the Buttons (their own gestures), then
the panels. *Proof each time: 43 drew, snapshot unchanged, no page errors.*

**Step 6 — CSS follows.** The 13 control-specific rules move to their controls'
`presentation/web/*.css`; the blob gains a stylesheet half. *Proof: computed
styles in the snapshot unchanged.*

**Step 7 — delete the scaffolding.** `WIDGET_CLASSES`, `INPUT_WIDGET_CLASS`,
`DISPLAY_WIDGET_CLASS`, `READOUT_WIDGET_CLASSES`, `buildValueControl`,
`makeReadoutEl`, the maker functions. Only once nothing references them. *Proof:
the file shrinks by ~350 lines and everything is still green.*

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

It unlocks the rest of that ROADMAP section: `presentation/rest`,
`presentation/macos`, `presentation/mcp` become directories a control can grow,
because `presentation/web` stopped being special by being the only one. And it
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
