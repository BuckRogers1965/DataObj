# A property the engine never reads

Tonight a Textbox learned to format a telephone number. You type `5551234567`
and it fills in as `(555) 123-4567`. Type too few digits and it outlines red
and refuses to send. A second box does social security numbers, `123-45-6789`,
by the same means.

No C was written for either. The Textbox object does not know what a phone
number is, has never heard of a mask, and was not recompiled. What made it
happen is a property called `GUI_Format` that exists on exactly one instance
and that the engine never reads.

## Presentation is data, and data already has a home

The temptation, when a widget needs to format its contents, is to give the
widget a format. Add a `Format` property to textbox.c, teach the object to
apply it, ship a new `.object`. That works, and it is wrong twice over: the
engine gains an opinion about telephone numbers, and every future annotation
- a pattern, a colour, a unit, a placeholder - is another property on another
object and another recompile.

The framework already had the answer, three times over. When the bridge builds
a property row, it reads the presentation off the property node itself:

```c
int widget = ipro ? GetPropInt(ipro, "Widget") : GetPropInt(prop, "Widget");
int mw = GetPropInt(prop, "W");
int mh = GetPropInt(prop, "H");
```

`Widget`, `W` and `H` are annotations the engine stores and never interprets.
It carries them; the client draws from them. That is exactly the shape a
format wants. Everything is a node, properties are nodes, so anything can be
annotated - and annotating is not a new mechanism, it is the only mechanism.

So `GUI_Format` is an ordinary property. `SetPropStr` creates one that does not
exist yet, which means the act of setting it is the act of creating it:

```json
{"cmd":"set-property","instance":"/Root/format/telephone number",
 "prop":"GUI_Format","value":"(###) ###-####"}
```

or, from inside a scripted widget, one verb:

```js
pathset('/Root/format/telephone number', 'GUI_Format', '(###) ###-####');
```

It lands on that one instance and nowhere else. It shows in that instance's
options panel. It saves with the flow, and it survives clone and export,
because it is real data - which a rule living only in a browser would not be.

## The prefix is the rule

The engine has to carry these to the client, and the obvious way to do that is
a list: bridge.c learns the names `GUI_Format`, `GUI_Pattern`, and grows a line
every time a new annotation is invented. A list is how you find out your design
is wrong.

`GUI_` is the whole rule instead. Anything whose name starts with it is the
client's business and the engine's storage:

```c
if (!name || strncmp(name, "GUI_", 4) != 0)
        continue;
```

That is the entire filter, and it means a new annotation costs zero engine
changes forever. It also reads correctly in a node dump: you can see at a
glance which of an instance's properties are decoration.

They ride the `instance-created` event as a `gui` object, because the first
attempt only worked after opening the options panel. That surface is where
properties get enumerated and subscribed; a control sitting on a canvas has no
panel open and nothing asking for its annotations, so it never learned them.
Birth is when a client learns everything else about an object; annotations
belong there too. An instance with none sends `{}`.

## A mask, not the name of a format

`GUI_Format` is `(###) ###-####`. A `#` consumes one digit; every other
character is punctuation the box supplies. Punctuation only appears once
there is a digit to justify it, so a half-typed number reads `(555) 12`
rather than `(555) 12)-`.

That choice matters more than it looks. The alternative was `GUI_Format =
"phone"` - a named format, which is a list again, and a list in the client
this time. A mask is a rule, so `##/##/####` is a date and `#####-####` is a
zip+4 with no further code anywhere.

A mask also carries its own validation: `(###) ###-####` says ten digits, so a
value that does not fill it is not a phone number. Too few is refused; too many
cannot be typed, because the mask stops consuming. `GUI_Pattern`, a regular
expression, composes on top for anything a mask cannot say.

**The engine stores the digits.** The mask is display only, and `5551234567`
is what goes over the wire. Formatting that changed the stored value would not
be formatting - it would be a second copy of the data, and two clients with
different masks would disagree about what the property is. So `GUI_Pattern`
tests the raw digits too, and a mask and a pattern can never contradict each
other.

The outline behaves the way a person expects rather than the way a validator
does: a box that has never held a complete number is only half-typed, so it
stays plain while you fill it in. Once it has been good once it is armed, and
from then on it reports live in both directions - clears on the last digit,
returns the moment one is deleted.

## Where this goes

The interesting part is not phone numbers. It is that a script inside a view
can now write its own controls' presentation:

```js
pathset(box, 'GUI_Format',  '(###) ###-####');
pathset(box, 'GUI_Pattern', '^[0-9]{10}$');
```

A scripted composite widget already had editable logic. Now its look and its
input rules are editable the same way, by the same verb, with no compiled
object knowing anything about it.

There is a line, and it is worth naming. Formatting, colour, an outline: pure
projection, free. **Gating what propagates is different in kind.** The browser
refusing to send is a rule that only exists where a browser is attached - a
script writing the same property bypasses it, a second client bypasses it, a
headless host has no rule at all. That is fine for input validation at the
edge, which is what this is. If a constraint has to hold for everyone, it
belongs on the wire as a Filter, in the engine, where every writer meets it.

## The tests, and what they were hiding

The scripted-widget suites still built the old shape - a bare language-host
instance wired to a Bridge - from before hosts became opaque. Rewriting them
around the ScriptBox was mostly mechanical, and then jstest took the machine
down: five frameworks at 3.5 GB, swap exhausted, 846 MB free on a 64 GB box.

The cause was two lines of test code. A helper activated each ScriptBox twice,
on the reasoning that the first activate comes up quiet and the second is Run.
That is true only when the inner host does not exist yet - and setting
`Language` builds it, so the first Run already ran and the second re-ran a box
that was still live. The result is an unbounded queued message loop:
`ExecTasks -> DispatchMsg -> SetPropStr -> DeliverToSubscriber`, every hop a
changed value, so `SetPropStr`'s change gate never fires.

Three things made it hard to see, and all three are worth remembering. It never
crashed. ASAN produced no report, because nothing was corrupt - a live,
well-formed message loop is not a memory error. And at `-v 3` the log showed
the panel build and then silence, because the loop runs entirely through paths
that carry no `DebugPrint`.

The suites now activate once, which is the shape scriptboxtest always used, and
all four pass with the server at 10-13 MB. But the engine defect is still there
and is written up separately: **the Run button is wired straight to Activate,
so pressing it twice on a live ScriptBox does this from the GUI.** The tests no
longer trigger it. A user still can.

One other test failure turned out to be nothing to do with the code. flowtest
hardcoded port 8095 for its TCP echo server. The harness gives each variant
`RAW_BASE + offset`, which is 8092-8096 - so 8095 is ubsan's own bridge port.
Every variant's echo client was connecting to another variant's bridge, sending
`hello, flow`, and getting back `could not parse command`. It was a fine port
when there was one server; the variant scheme moved a bridge on top of it. The
echo port derives from the run's own port now.

## The move bug, and a whole class of bad things

Then the thing that mattered. Dragging a textbox into a view renamed the view,
buried the textbox inside itself, and lost it on the next reload. The client
log named the bug precisely:

```
from = /Root/View_1        to = /Root/View_1/Textbox_1
```

`Bridge_Rename` builds `to` as `newContainer + "/" + baseName`, where
`baseName` is the text after the last `/` **of `from` itself**. For `to` to end
in `Textbox_1` while `from` reads `/Root/View_1`, the string must have said one
thing when the basename was taken and a different thing when the event was
written. No consistent string does that. The buffer changed underneath.

It had:

```c
static char *Bridge_AliasForInstance(InstanceData *local, NodeObj inst)
{
        static char bufs[4][300];
        static int rot = 0;
        ...
```

A four-slot rotation. The string you were handed stayed valid until four more
aliases were resolved - and nearly everything in bridge.c resolves aliases,
every scoped event send included. Hold the pointer across any real work and you
are reading a buffer that now belongs to someone else.

The consequences cascade exactly as observed. The wrong path gets unregistered.
`Bridge_RepathSubtree` then runs with the wrong prefix, matches the instance it
just moved as its own descendant, re-paths it a level deeper, and rewrites its
`Container` to something that does not resolve. On reload the view rebuilds
from its own Name and Container, so its name comes back - and the textbox is
orphaned under a container nobody can find.

It had been there since 2026-07-17, three weeks. It fires only when four alias
resolutions land between capture and use, which depends on how many
subscriptions and windows a session has. Fine for a long time, then suddenly
not. That is what a latent buffer-reuse bug feels like from the outside, and
it is why the first instinct - "what did we change today?" - was wrong.

**The code already knew.** One call site carried a fourteen-line comment
explaining that it copies the string "out of Bridge_AliasForInstance's ring
IMMEDIATELY", ending with *"seen live: oldAlias came out reading as some
descendant's NEW path."* Someone had hit this, diagnosed it correctly, and
fixed their own call site. The other twelve were left to find out.

That is the lesson worth keeping: **a comment teaching callers a rule they
cannot see is a bug report.** The fix is never to copy the defense to the other
sites. It is to remove the sharing. `Bridge_AliasForInstance` takes a caller
buffer now, all eleven call sites own their storage, the rotation is gone, and
so is the workaround that had been quietly holding one site together.

Or, put the way it was put at the time: *do not recycle things that are in
use.* It is insane to reuse a buffer whose contents are still on screen.

There was a second half. With the engine re-keying correctly, the browser still
sent the old path and got back `unknown instance`. `onInstanceRenamed` re-keys
ten maps, but a control's commit is a closure - `(v) => send({instance: alias,
...})` - and no amount of re-keying reaches inside a closure. Every deferred
gesture had the same flaw: the textbox commit, alias-atom writes, view
open/close, MoButton, Button activate, both Dropdowns. They resolve the current
name at the moment they fire now, through one indirection, rather than trusting
what they captured when they were drawn.

Two independent bugs, one visible symptom, and either one alone still loses the
box. Names change under a running page; anything holding one has to be told.
