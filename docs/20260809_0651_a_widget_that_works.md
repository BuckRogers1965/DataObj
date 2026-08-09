# A Widget That Works

A ScriptBox running Forth, with three controls that mean exactly one thing
each:

- **Activate** compiles the source. Nothing else.
- **the LED** is lit when it compiled, dark when it didn't.
- **In** steps it once per arrival, and only while the LED is lit.
- **Enable** off puts the light out, stops stepping, and makes the interpreter
  drop what it compiled.

Twelve presses of In gives 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144. The
state lives in two ordinary properties on the box, so it survives a save, and
setting one of them back to 0 restarts the sequence.

That took all night, and almost none of the time went on Forth.

## What the bugs actually were

Every single one was the same shape: **one fact stored twice.**

**The output box.** `Output` was cleared before each run and appended to during
it, so a run put two values into circulation. The control's re-announce is
queued, so the stale one landed after the box had moved on, wrote itself back,
and from then on the two values regenerated each other - about 2,200 laps a
second, each lap dragging the whole web flow along. On screen: "hello"
blinking. 6,700 events from a single run, measured. The fix was to stop
clearing and stop appending: a run sets the value it produces, once.

**`Out` and `Output`.** Two properties for one thing, the widget writing both.
Deleted `Out`; `Output` is what the box shows and what a wire carries.

**A latched `Running` state.** Set on activate, never cleared, so the light
said "running" forever. Then worse: a *separate* `State` alongside the LED, so
the light and the state could disagree. Now the LED's value **is** the state -
there is nothing else to be out of step with it.

**The LED's own second lamp.** The LED class published its lifecycle `State` as
a second `PROP_LED` defaulting to `"1"`, so every LED carried an extra light,
lit from creation, unrelated to the value it exists to display. Removed
entirely: the value is the state.

**Zero could not be stored.** `SetValueInt` read
`if (!node || !value) return;` - so writing 0 was a silent no-op, and anything
that turned something *off* through it simply didn't happen. That is why a
light could be lit and never darkened. Worse, the behaviour had been written up
in the project notes as if it were a deliberate convention ("this is why pulse
edges travel as the strings 1/0"), which turned a bug into documented
behaviour and had objects working around it. Zero is a value. Fixed in
`SetValueInt` and `SetValueLong`; the string setters keep their NULL check,
which is a different thing.

**Enable did nothing.** `ScriptBox_OnEnable` required `msg_send`, and a
checkbox wired to that property delivers `msg_change` - a property's own
fan-out. So clicking Enable ran none of the handler. It now acts on the
presence of a value rather than on which route delivered it.

**A compile error came up green.** The host reports errors synchronously while
running, so the error handler put the light out - and then the line after
`ScriptRun` set it green, painting over the failure. Light it first, check the
return, let the error win.

**Creating the widget ran it.** `Widget_DeferBuild` calls the object's own
`Activate` one tick after creation. The palette builds one of every class at
boot, so every widget's "go live" ran on a catalog entry nobody asked to run.
For a ScriptBox that meant compiling and lighting up at startup. For an object
driving something physical it means the thing moves. The quiet variant exists
precisely for this and this widget now uses it - but twenty other objects still
call the loud one, and `BuildPalette`'s own comment promises the opposite:
"None of the bootstrap instances are ever Activated - they exist to be
inspected and cloned from."

## What was wrong in the language host

**A verb that returns nothing must push nothing.** The trampoline pushed an
empty string for verbs with no result, so `print` left an address on the stack,
and two words later `over +` added a heap pointer to a number. The sequence
read 1, then 22872272872.

**Compile once, then execute.** The driver protocol is `SET_SOURCE` + `RUN`,
which is a Lua/JS shape: throw the context away, re-evaluate the text, cheap
and idiomatic there. Forth is stateful by design - the source's job is to fill
the dictionary and running means executing a word in it. Re-evaluating per step
re-allocated every buffer and definition and overflowed the interpreter's heap
after a dozen presses. The reflex was to raise the heap to 5MB. The correct
answer was to stop recompiling: Forth runs in kilobytes on spacecraft, and
needing megabytes was a symptom, not a requirement. Activate compiles, In
executes; the heap went back to 16K cells.

**Atlast's own conventions, which cost hours:** a primitive's name needs a
leading flag byte and must be upper case (`{"0PRINT", fn}`, because `lookup`
compares `wname + 1` and upper-cases the token); string literals are the
C-like `"text"` and ANS Forth's `s"` does not exist; `STRINT` leaves
`endptr value`; `STRFORM` pops three. And `WORDSUSED` writes a flag byte into
the *name* of a word - a string literal - so it segfaults inside `atl_init()`
on any compiler since about 2005. The release anticipated that:
`-DREADONLYSTRINGS` copies the names.

## The lessons

**One fact, one place.** Not "two copies kept in step" - one. Every bug above
is a duplicate somewhere: a value mirrored between a property and a control, a
state beside the light that shows it, a shadow copy of the current language, a
clone that copied a value where a link belonged. A mirror looks like it works
right up until the two sides disagree about whether they've converged.

**Suppressing an unchanged write hides the bug rather than preventing it.**
Clearing an already-empty `Output` was a no-op, so the first run looked
innocent and only the second detonated. Difference-based suppression made the
failure conditional, and conditional failures are the expensive kind.

**One write per property per delivery.** Clear-then-set and read-modify-write
are both two changes to one thing, and with queued re-announce the earlier one
can land last.

**Don't force accumulation on a consumer.** The producer emits a value; whoever
displays it decides whether to keep history. Accumulating on the producer makes
it read back its own output - which is how the loop started - and makes every
message contain all of history, so "did this change?" is always yes.

**Measure; don't infer.** Asked whether a function returned, the answer was one
`DebugPrint`. I gave a paragraph of call-chain reasoning instead, then built a
second theory on top of the first. The loop was found by tracing every
delivery and reading the repeating cycle; the doubling was settled by counting
events; the LED colour was settled by reading the CSS. Every wrong turn came
from treating my own reading of the code as evidence.

**A fix that keeps growing is a missing distinction.** Two ids, then eleven
handlers, then eighty-one, then the core's delivery path - that escalation was
the signal, and it was already written down in these docs from earlier the same
day.

**The code is what's wrong, not a person.** Hours went on which change broke
what and whose it was. `SetOrDeliverProp` renaming a property on the way in had
been sitting in the clone path since July; it didn't fit "which of my changes
did this," so I skipped past it twice.
