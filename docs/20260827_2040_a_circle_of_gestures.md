# A circle of gestures

*Control-click a thing, get the verbs that apply to it - and one of them
puts a function on it, 27 August 2026.*

Modes here are session-wide. To alias one control you put the whole window
into Alias mode, do the thing, and put it back. That is fine when the modes
are few and the session is one canvas, and it stops being fine the moment
you want to do something to THIS control and nothing else.

Control-click a control and get a circle of actions around it. The ones
already known - alias, connect, clone, move, delete, options - plus the
ones that have never had a gesture: annotate this property with a display
format, and **put a function on this thing**.

## The circle is an inventory of verbs

That is the useful property of drawing it, before anything is built.

Every wedge has to be one thing a person does, sent as one command
carrying the whole intent. That is not a new rule, it is the rule this GUI
already lives by: the browser translates a gesture into one verb and
renders what comes back. So the menu cannot contain anything the engine
cannot do in a single call - and drawing it immediately shows which wedges
have no verb behind them.

Going round the circle today: alias, connect, disconnect, clone, move and
delete are verbs. "Format" is a `set-property` writing `GUI_Format`, which
the client already reads for masking and validation. Every wedge is
covered except one.

The missing one is the interesting one.

## Insert a function

The engine has exactly one way to make something react to a write, and
every compiled object uses it:

```c
SetPropStr(instance, "Enable", "1");
port = GetPropNode(instance, "Enable");
SetPropLong(port, "OnMsg", (long)Widget_OnEnable);
```

A property carries a handler. A write delivers to it. The handler returns
a verdict. That is the whole mechanism, and there is not a second one for
"scripted" behaviour, because a function pointer in a node property does
not care what it points at.

So a script installs a handler the same way C does: put a trampoline in
`OnMsg`, and put the host and the function name beside it. One trampoline
serves every scripted handler in the session - what varies is the data on
the node it is standing on, exactly like a Subscriber record, where one
shared entry point reads the record rather than there being a function per
wire.

Dispatch learns nothing. It finds a handler and calls it.

And the return value is what makes a scripted handler a citizen rather
than a leaf. The script's answer maps onto the verdicts, so it can say
handled, propagate, or `rtrn_unhandled` - **not mine** - and the class walk
carries on up exactly as if a compiled class had declined. A script can
take one message and leave the rest to its parent. That is what a class
does.

## The recursion is the point

The wedge does not only put a function on the thing you clicked. A script
attached to something can install functions on ITS siblings.

That is not a special power granted to scripts. It is the same call:
find a node, put a handler on one of its properties. A script inside a
view can therefore reach the controls beside it and intercept what they do
- the checkbox that clears the others, the box that reformats what was
typed, the cell that recomputes when its neighbours move.

Which is how a view full of controls becomes a widget. Assemble the
controls by hand, insert a script, let the script put handlers on the
members, and the result behaves like compiled code while its logic stays
editable. It goes in the palette and clones like anything else. Nothing
about it is second class, because there is no boundary for it to be on the
wrong side of - a script host is an object, a class is a node, and making
an instance is a message that something answers.

## Functions on cells, and on anything else

The immediate payoff is the one everybody already understands.

A spreadsheet cell here is a node with a name - `A1`, at a real path. Put a
function on it and you have a formula, in the only sense that matters: the
cell reacts. Sum a range, sum by name, recompute when the cells it read
change - it subscribes to them, and subscriptions now report both ends, so
it also knows when that set changes rather than only when a value moves.

But nothing in that paragraph is about cells. A cell is a node, a control's
Value is a node, a widget's property is a node, a view is a node. The same
wedge on the same circle puts a function on any of them. "Functions on
cells" is what you get by NOT special-casing cells - and the same gesture
gives functions on a slider, on a text box, on a device's output, on a
container.

## Awkward first

The first cut does not have to be a circle. A list of names and a box to
type into settles the question that matters - can a function attached to
one node act on another - and everything after that is presentation, which
is the cheap half to redo. The browser is a projector; making it sleek
later touches nothing that decides whether the thing works.

What it needs from the engine is small, because most of it is standing:
three language hosts, discoverable by `ScriptHost=1`; `pathget`/`pathset`
so a script reaches anything by name; a verb table where a trailing script
function is already an accepted argument; and one generic trampoline that
turns that function into an `OnMsg` like any other.

## Chaining: the original runs because you declined

Installing a function raises the obvious question - what happens to the
handler that was already there? A compiled control's `Value` already has
one, and replacing it outright would mean an intercept could only ever
destroy behaviour, never add to it.

The answer is the rule that already exists, applied one level down.

An intercept installs a RECORD on the property node - the same species as
a `Subscriber`: a node carrying the trampoline and what it needs beside it
(which host, which function). Delivery walks those records newest-first and
then falls through to `OnMsg` as the last link.

What decides whether to keep going is the verdict. `rtrn_unhandled` means
"not mine", so **transferring to the original is not a call anybody makes -
it is what declining does.** An intercept that means to replace the
behaviour returns handled and the original never runs. One that means to
watch, or to adjust the value and let the real thing proceed, returns
unhandled and the chain carries on to `OnMsg`.

That is `PuntToClass`'s rule aimed at a property instead of a class node.
One meaning of "keep looking" in the system, not two.

Chaining falls out of it. Records prepend, so the last intercept installed
runs first and wraps the ones before it; removing yours is removing your
record, with no saved-previous-pointer to get wrong when two things
uninstall out of order.

The change is small. Nine places read `OnMsg` and only three CALL it -
`DeliverMsg`, `SetOrDeliverProp`, and `DeliverToSubscriber`. Those three go
through one helper that walks the records and then the pointer; the other
six are existence tests that become "is there any handler at all". A
property with nothing installed costs one empty walk and behaves exactly as
it does now. And the records carry pointers, so `IsPortableProp` refuses
them - no file can restore a stale trampoline, the same reason a widget's
build re-installs its handlers after a load.

## What it buys

**Behaviour stops being a property of the class.** Today what a Textbox
does when written is decided in textbox.c, once, for every Textbox that
will ever exist. With an intercept chain it is decided per instance, at
runtime, by whoever is looking at it - and the compiled behaviour is still
there underneath, one decline away.

**You can add without owning.** The thing you are extending was not
designed to be extended. There is no hook to register with, no interface it
had to implement, no author who anticipated you. A control written last
year takes an intercept from a script written this afternoon because the
mechanism is the one it already uses for its own handler.

**Fixing something in the field stops meaning a release.** A customer's
widget misbehaves on their data - a format nobody anticipated, a device
that reports a value one decimal out. Today the fix is a code change, a
build, and a deployment. With this it is a function installed on that one
property, in that one session, while it runs - and it can be saved with the
flow, because the flow is the app.

**Composition beats subclassing, and now it can.** A sorted table is not a
subclass of Table; it is a table with a function on it. A radio group is
not a subclass of View; it is a view whose members got a function that
clears the others. Neither needs a class to exist, a name to be registered,
or anything recompiled. The class chain stays what it is - plumbing for
defaults - and the way people actually make things is putting parts
together and inserting behaviour.

**And it composes with itself.** An intercept is just a handler, so a
script can install one on a property that already carries somebody else's
intercept, and the chain grows a link. Two features written by two people
who never met can both act on the same property, in a defined order, each
able to decline and let the other proceed. That is the thing frameworks
usually promise with a plugin API and a priority number, and here it is the
same walk the class chain already does.

The cost of all this is one helper and a record type that looks exactly
like the record type next to it.
