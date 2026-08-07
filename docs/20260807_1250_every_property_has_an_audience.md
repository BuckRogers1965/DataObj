# Every property has an audience

Two features landed close together and turned out to be the same idea wearing
different clothes. A ScriptBox holds `Source` and `Language`, and the engine
does nothing with either - it hands them to a language host. A Textbox holds
`GUI_Format` and `GUI_Pattern`, and the engine does nothing with those either
- the browser reads them and masks a phone number.

Neither is a special case. They are two instances of something the tree has
been doing all along without anyone naming it: **a property has an audience,
and the engine is only one of the possible audiences.**

| property | who reads it | what the engine does |
|---|---|---|
| `Value`, `Enable`, `Interval` | the object itself | interprets |
| `Source`, `Language` | a language host | stores, hands over |
| `GUI_Format`, `GUI_Pattern` | the browser | stores, transports |
| `Widget`, `W`, `H`, `LabelPos` | the browser | stores, transports |
| `ReservedIn` / `ReservedOut` | the client, to draw a dot | nothing - says so in its own comment |
| `File` / `Class` / `Major` / `Minor` | the loader | reads once, at load |
| `UUID`, `Company` | humans, deployment | never |

Most of that right-hand column is already "not the engine's business." `GUI_`
did not introduce the idea. It gave a name to something that was already true
of half the tree.

## Naming it is not cosmetic

The evidence that naming matters is a bug we shipped and then found the hard
way. `W` and `H` on a property node are GUI annotations wearing no badge -
indistinguishable, by inspection, from data the object uses. When the options
panel changed from walking the class interface to walking the instance, the
`Widget` lookup was taught the new cascade:

```c
int widget = ipro ? GetPropInt(ipro, "Widget") : GetPropInt(prop, "Widget");
```

and the size lookup, four lines below, was not:

```c
int mw = GetPropInt(prop, "W");
```

Every member fell through to the 272x30 default, so a code box declaring
400x180 came up as a one-line row. Nobody noticed because nothing announced
that `W` and `H` had the same audience as `Widget` and therefore needed the
same treatment. An unprefixed annotation cannot be reasoned about as a class;
you have to know, name by name, who cares.

The reserved-name rule is the same lesson from the other direction: a widget
must never call a data property `Mode`, because the view already claims that
name for interaction mode. Two audiences, one name, no badge, and a dead panel
with no error to explain it.

## Extensibility without a plugin API

The reason this is worth formalising rather than merely noticing: it is how the
framework extends without growing hooks.

A new consumer - a printer, an agent, a report generator, a compiler, a second
GUI - does not need the engine to add anything. It reads properties nobody else
reads. And it inherits the whole machine for free, because the engine's
handling of a property it does not understand is identical to its handling of
one it does: annotations save with the flow, survive clone and export, arrive
on the birth event, appear in the options panel, and can be written by a script
with one verb. Not one line of that was built for `GUI_Format`.

There is exactly one way to lose this, and it will be proposed in good faith:
the first time someone argues the engine should *validate* `GUI_Pattern`, "just
to be safe." The moment the engine has an opinion about an annotation, it stops
being an annotation, and the neutrality that made every one of those mechanisms
free is gone.

## Audiences as a testing discipline

Here is where it earns its keep, because "who reads this?" turns out to be a
question with testable answers.

**1. The audience says which tier owns the test.** An engine-audience property
is testable over the raw protocol with no browser. A GUI-audience property is
only observable in a browser. A loader-audience property needs a load-order
test and nothing else. Testing at the wrong tier is how suites become slow and
flaky, and we have a live example: guitest's `lazy: the GUI only holds what it
can see` drives a browser to assert something about *what the bridge sends* -
which is a raw-protocol claim. As a rawtest it would be deterministic and
instant. As a CDP test it involves clones, renames, panel geometry and two page
loads, and it was silently broken for weeks because it forgot to close the view
it was testing.

**2. A property with no audience is dead, and that is mechanically detectable.**
If nothing reads it, it is either a leftover or an unfinished feature. There is
one in the tree right now: `local->active` in scriptbox.c is set to 1 and
cleared only at InstanceStart - written, never read. It looked like a state
flag, which is why "just ignore Run while active" seemed like a fix until
somebody checked who reads it. A sweep for published properties with no reader
would find these.

**3. A property with two audiences is where the bugs live.** Single-audience
properties are boring. The dangerous ones are read by the object *and* relayed
to a client, or written by a client *and* interpreted by the engine. `W` and
`H` are declared by a widget table, relayed by the bridge, rendered by the
client, and used for panel layout - four readers, one of which was reading the
wrong source of truth. The test that would have caught it asks each audience
separately: does the interface entry carry 400x180, does the member the bridge
builds carry it, does the DOM element end up that size?

**4. Audience membership generates round-trip tests.** For any annotation the
assertions are derivable rather than invented: set it, and it must survive
clone, survive export/import, survive save/load, arrive on the birth event, and
appear in the options panel. Five assertions per annotation, and you did not
have to think of any of them - they are just "every path by which an audience
could receive it." The palette round-trip suite is this idea applied to every
class at once.

**5. Audience halves the search during a hunt.** When the phone mask did
nothing, the question was not "where is the bug" but "which audience is not
getting the data." The engine's copy read back correctly over the raw protocol,
so storage was fine and the fault was transport or client - and it was
transport, because nothing on a canvas ever subscribes to `GUI_Format`. The
move bug went the other way: the client sent one correct `move-instance` verb,
so the fault was upstream of the client, in the engine. Both searches collapsed
to half the system in one question.

**6. The half-landed change is the signature bug of a multi-audience system,
and audience enumeration is the checklist.** Three bugs in one night had
identical shape:

- `Widget` learned the new cascade; `W`/`H` did not.
- `Bridge_Move` copied the alias out of the shared buffer; twelve other call
  sites did not.
- One call site defended itself against the recycled buffer with a fourteen-line
  comment; the other twelve were never told.

Every one is "this data has N readers and only some were updated." A change to
a mechanism should come with the question *who reads this, and did they all get
updated?* - and a half-landed change is nastier than one that does not land at
all, because everything still appears to work.

That third one deserves its own rule: **a comment teaching callers a rule they
cannot see is a bug report.** It had even recorded the symptom - "seen live:
oldAlias came out reading as some descendant's NEW path" - and sat in the file
for three weeks while the other twelve sites stayed broken.

**7. A prefix converts a convention into a machine-checkable set.** This is the
practical argument for `GUI_` over a list of blessed names. Because the prefix
is the rule, the engine can enumerate the set in four lines:

```c
if (!name || strncmp(name, "GUI_", 4) != 0)
        continue;
```

and so can a test. "Every `GUI_*` property on every instance reaches the
client" is a real assertion that holds for annotations nobody has invented yet.
The same test is impossible to write for `Widget`, `W` and `H`, because you
cannot enumerate an unbadged convention - you can only enumerate the names you
already thought of, which is exactly the list the prefix exists to avoid.

**8. Write the negative test: assert the engine does *not* react.** For a
client-only annotation the interesting claim is absence. Setting `GUI_Format`
must produce no behaviour change engine-side - no extra tasks scheduled, no
allocations beyond the property itself, no message traffic. The alive-counters
make this checkable the same way leaktest already checks that a create/destroy
cycle nets zero. Nobody writes that test, and it is the one that would fail
loudly on the day someone gives the engine an opinion.

**9. Garbage in, degrade not crash - per audience.** A malformed regex should
gate nothing (it does). A mask with no `#` in it. A `GUI_` property on an
object no GUI will ever render. A `Source` on something with no language host.
An audience that is absent must be indistinguishable from an audience that is
satisfied, because the whole design rests on annotations being inert to
everyone except their reader.

## The implication: standard handlers, and behaviour without a recompile

There is a bigger consequence sitting in this, and it starts from something
already true at the bottom of the stack. Every delivery in the system passes
through this:

```c
onmsg = portnode ? GetPropLong(portnode, "OnMsg") : NULL;
if (onmsg) { onmsg(toInstance, message, chunk); return; }
SetPropStr(toInstance, port, value);
```

A property carrying `OnMsg` runs code; a property without one stores. That is
already universal - every property, every object, no port type, no opt-in. The
hook is not missing. What is missing is that **the only handlers that exist are
hand-written, one per object.** `Writer_OnIn`, `ScriptBox_OnIn`, `Filter_OnIn`,
`Textbox_OnEnable` - fifty variations on the same half-dozen behaviours, every
one of them C, every one of them requiring a rebuild to change.

Give the system a set of STANDARD handlers and a property configures its own
behaviour the same way it configures its own presentation - with sub-properties
on the node, one level down:

```
Value
├── OnMsg      -> the standard gate
├── Pattern    "^[0-9]{10}$"
└── Widget     PROP_TEXTBOX
```

Three things fall out of that.

**It shortens a great deal of code.** The obvious set - gate on pattern, clamp
to a range, dedupe against last seen, coerce a type, scale, rate-limit, log -
is mostly already written, scattered as one-offs across objects that each
needed one. Filter's four modes (`all`, `change`, `ones`, `zeros`) are standard
handlers that today cost an instance and two wires.

**Behaviour changes without recompiling anything.** This is the real prize.
Changing what a property does becomes a property write - which means a script
can do it with one `pathset`, a flow file can carry it, and a running system
can be re-gated without a build. The framework's claim has always been that
shipping a different product means shipping different objects and a different
flow, never a different binary; this extends it inside the object, where
today the only way to change how a property reacts is to edit its C.

**It puts the rule where every writer meets it.** A gate on the node holds for
a script, a second client, a `Connect()`ed source and a flow load alike - which
is exactly the enforcement asymmetry that `GUI_Pattern` has and cannot fix from
inside a browser.

The hard part is durability, and it is a problem the registry already solved
once. `OnMsg` is a `long` holding a pointer, and `IsPortableProp` deliberately
skips LONG-valued properties because they are runtime addresses - so a handler
attached that way evaporates on save/load. The durable form has to be a NAME:
the property carries `Handler="Gate"` as an ordinary string, and load resolves
the name to a pointer through the registry, exactly as `ClassStart` and
`InstanceStart` are stored and recovered. The pointer becomes a cache of the
name rather than the truth.

### The danger: this is invisible

Worth stating plainly, because it is the cost and it is not small.

A `Filter` in the wire is a *thing you can see*. It is an instance on a canvas
with two connections; the graph shows that messages are being screened, and
double-clicking it shows how. A handler stamped on a property shows nothing.
The canvas draws a wire that silently drops half of what crosses it, and
nothing on screen says so.

That is a direct hit on what makes this system comprehensible. The flow diagram
has been the documentation - everything is a node, so everything is visible, so
what you see is what runs. Moving behaviour onto property nodes breaks that
unless the GUI is taught to project it, and right now it is not. The failure
mode is a support call that cannot be answered from a screenshot: *the value
isn't arriving*, and the wire looks perfect.

So the mechanism should not land before the projection does. At minimum a
gated property needs to look gated - a badge on the property, the handler and
its configuration shown in the options panel next to `Widget` and `W`/`H`,
`list-connections` reporting gates alongside wires, and the `WIRE` debug
category tracing a gate's verdict the way it already traces every wire made and
removed. Invisible behaviour is the one thing this framework has never had, and
it would be a poor trade to buy no-recompile flexibility with it.

### Security is a gate on access to data

Which leads somewhere this was not aiming. Strip the word of its ceremony and
**security is a gate on access to data** - a decision, taken at the moment of
a write, about whether this particular writer may change this particular thing.
That is the same handler, with one more input. `Writable="admin"` sits beside
`Pattern` and `Widget` as an ordinary sub-property, and the palette seeds -
which today anything at all may scribble on - become protected by stamping a
handler on them.

This is also the argument that settles where gates belong, because access
control is the one rule that is *worthless* anywhere else. A check in a browser
is advice. A check on the node is the only thing that holds for a script, a
second client, a raw TCP peer and a flow load at once. Everything above about
enforcement asymmetry was a preference; here it is the whole point.

**The missing input is identity, and the fabric already has the shape for it.**
A delivery is `onmsg(instance, message, data)` - there is no "who" in it. But
messages already carry metadata as properties on the message node: the TCP
object tags every message with a `Conn` identifying its connection, and the
bridge stamps `SetPropLong(chunk, "Conn", connId)` on what it sends. A security
node on the message is that same move. Not a new mechanism - one more property,
on a node, read by a handler.

Two things have to be true of it, and both are the sort of thing that is
obvious in advance and invisible afterwards:

**The principal is stamped by the transport, never declared by the sender.**
The bridge sets it from the authenticated connection. If an inbound command can
set its own identity field, the entire mechanism is decoration with extra
steps.

**A gate on `OnMsg` covers deliveries, not direct writes.** `SetPropStr`
updates the value and fans out to subscribers; it does not invoke that
property's own handler. Only the delivery paths do - `DeliverToSubscriber` for
a `Connect()`ed source, `SetOrDeliverProp` for the bridge. So a gate protects
everything arriving from outside the object and nothing the object's own C does
to itself. That is defensible - an object owns its own state - but "protected"
then has a precise boundary, and anybody reasoning about it who does not know
that boundary will assume more than is true.

And it turns the visibility problem from a nicety into a requirement. A hidden
formatting rule costs a support call. A hidden *permission* cannot be reviewed
at all: you must be able to ask "what on this system is gated, by what, for
whom" and get an answer out of the tree, without reading C. A security model
you cannot enumerate is not a security model.

## The short version

The node tree is not the engine's private data structure. It is a shared
substrate, and each property is addressed to somebody. Write down who, and you
get: the tier a test belongs in, a detector for dead properties, a map of where
bugs concentrate, a generator for round-trip assertions, a way to halve a
search, a checklist that catches half-landed changes, and a set you can
enumerate in a loop.

And once a property can carry its own handler by name, the audience list grows
one more entry - the property itself - with behaviour that changes without a
compiler and, unless the GUI is taught to show it, without anyone seeing it.
Follow that one step further and the last entry in the column is a principal:
who is allowed to write this. Security turns out not to be a subsystem at all,
just the same gate asking a different question.

Not bad for a phone number.

## Postscript: the twin failure

Leaving the system is one way to pay. There is a second, and it cost more
hours than the first.

The web bridge minted a "tap" object for every client subscription - a node
whose entire job was to know which property a delivery came from, because the
handler could not otherwise tell. It turned out `DispatchMsg` had that
information the whole time. The source property node is `env->outPort`; it is
how the subscriber list gets found in the first place, and it was dropped one
line before the handler was called:

```c
DeliverToSubscriber(sub, env->message, env->data);
```

Meanwhile the *other* delivery path, `FanOutSubscribers`, does pass its origin.
One path carried the source, the other discarded it, and an entire species of
object grew in the gap.

**The substitute then hid the affordance.** Nobody fixed the dispatcher,
because the tap made it unnecessary. That is the loop: an unused affordance
produces a workaround, and the workaround removes the pressure that would have
revealed the affordance.

It is not a rare shape here:

- `DeliverToSubscriber` already hands a callback the ORIGINAL data - put there
  deliberately for this kind of dispatch - so a proxy object per source was
  never needed
- `DelNode` recursing props and children is a garbage collector; the tap sat
  outside it and got a hand-written reaper instead
- `DeleteList` existed in sched.c and was never declared in sched.h, so a test
  leaked its list because the API looked absent
- `Widget`, `W` and `H` were annotations on property nodes three times over;
  `GUI_` did not invent annotations, it named them
- messages already carry `Conn`; a generation stamp for time rather than origin
  is the same move, still unmade
- quiescence already is the fixed-point predicate - it is the shutdown rule; a
  settle-and-release barrier is that same predicate scoped to a region

The tell is distinct from the bypass tell. A bypass announces itself with a
helper whose job is to *remember*. This one announces itself with **a new
species whose only job is to carry information something upstream already
had.** The tap carried identity the dispatcher held. The card panel carried a
property list the internals view held. A per-object `OnChange` carries
behaviour a standard handler could.

And the reason it happens is actionable rather than a matter of attention: the
affordance was **invisible from where you would use it**. Reading
`DeliverToSubscriber(sub, message, data)` tells you nothing about `data` being
the source for one of its two callers - you would have to go read the caller.
An affordance that cannot be seen from the call site is functionally absent.

Which is why the fix was not a new capability. `MsgFromNode()` creates no
information; it gives a name to information that was already flowing past. The
dispatcher passes what it always knew, the handler can ask, and the tap stops
being a thing that has to exist.

So the closing law has a twin:

> Leaving the system costs you a second system to maintain, and you will
> forget. Not knowing the system costs you a second mechanism to maintain -
> and that one is worse, because it looks like architecture.
