# Subscribe to anything

*Why the cheapest thing in this framework is also the thing everything else
is built out of, 26 August 2026.*

Every value in this system lives in a node, and every node can be
subscribed to. Not values that were declared observable. Not values wrapped
in something that makes them observable. Every one of them, because being a
node is what a value IS here.

That sounds like a small uniformity. It is the reason most of the features
in this framework cost almost nothing to build.

## What is normally a layer

In nearly every system I have worked in, "watch this value" is a capability
you have to GRANT. You wrap the field in an observable. You declare a
signal. You register a listener against an object that agreed in advance to
have listeners. You write a notification and then keep it in step with the
value it is notifying about - and the gap between those two is where a
whole genus of bugs lives, because the notification can be forgotten,
duplicated, sent before the write, or sent to someone who has gone.

All of that machinery exists to answer one question: how does a value tell
anyone it changed? An `int` cannot. A row in a database cannot. So a layer
is built whose entire job is to make data able to speak, and then a second
layer to translate what it says into what a consumer wanted to hear.

Here a write IS the event. `SetPropStr` stores the value and fans out to
whatever subscribed (`FanOutSubscribers`, node.c). There is no separate
notification to keep in step, because there is nothing to keep in step
with: one node changed, and the news is that node changing. A repeated
value is a repeated event, deliberately - a button pressed twice happened
twice, and comparing the payload to decide whether anything happened would
be inspecting the one field an event is free to keep constant.

## What it buys, in things that actually shipped

The test of an idea like this is not the paragraph. It is what stopped
needing to be built.

**Debugging is composition.** `objects/out` is a probe: subscribe its `In`
to any property and it prints every message that passes, tagged with its
`Label`. It holds nothing open, schedules nothing, and returns
`rtrn_propagate` because a probe watches rather than consumes. There is no
debug API, no instrumentation hook, no logging framework to integrate with
- you wire an object onto the thing you are curious about, and take it off
afterwards.

**Instrumentation is composition too.** The core counts its own
allocations, the Stats object samples those counters on a timer and writes
them into ordinary properties, and a TextOut wired to `Nodes` is a live
leak readout. Nothing had to expose a metrics interface. The counters are
values; values are nodes; nodes can be watched.

**Lifecycle is composition.** Every object carries an `Enable` property.
Send it 1, it runs; send it 0, it stops. Because it is an ordinary
property, anything can drive any object's Enable - so a server that shuts
itself down after thirty seconds is a Pulse wired to `Enable`, not a
feature anybody wrote into the server. The object that stops does not know
what stopped it and does not need an API for being stopped.

**A panel is composition.** A widget's controls do not observe the widget
through some view protocol; each control's `Value` LINKS to the property it
shows. Writing the property fans out to the control, typing in the control
writes the property, and the two are the same node rather than two things
being reconciled.

**The browser is not a special consumer.** A client subscription is an
ordinary `Connect` into the Bridge's `Taps` property. The GUI watches
values the same way an `out` probe does, through the same records, with the
same fan-out. That is why a second window is free, and why anything the GUI
can watch a script can watch too.

## The spreadsheet, yesterday

I built a table widget this week, and the thing worth reporting is what did
not have to exist.

A cell is a node. So `Total` is not an export and there is no cell-reference
resolver: you alias the cell out and wire it, and something else watches it
change. A grid and a chart on the same table need no synchronisation code
because neither of them owns anything - they are both pointed at the same
nodes. Two windows showing the same sheet are the same story one layer up.

And the piece I expected to be work: a RadioGroup, where setting any member
to 1 clears the others. It needed no notification path, because one already
existed for a reason that had nothing to do with radio buttons.
`RegisterPath` records a new member's path on its CONTAINER as `LastMember`
(object.c:254) - an ordinary property write, which therefore fans out. The
group subscribes to its own `LastMember`, hears anything land inside it,
and subscribes to that member's `Value`. Every creator in the system passes
through `RegisterPath` - a drop, a clone, an import, a script - so one
subscription covers all of them.

That is the shape of it. The feature was two subscriptions and a handler,
because the thing it needed to know was already being said out loud to
anyone who cared to listen.

## Where the idea is currently one-sided

Honesty about the edge of it, because it decides the next piece of work.

A subscriber hears everything. The SUBSCRIBED hears nothing: a node does
not know it has been watched, and does not know when the last watcher went
away. The record lives on the source and names the target, so the target
holds no back-reference.

Two consequences, both real. `DeleteInstance` has to answer "who points at
me?" by sweeping every instance in the registry - twice, once for
subscriptions and once for links - because the only way to find your
watchers is to look at everything. And a viewport that wants to exist only
while something is looking at it cannot be told when to stop existing.

That was fine while the question was asked once per delete. It stops being
fine the moment a table with a hundred thousand cells wants to materialise
only what is on screen, because then "am I being watched" is asked on every
scroll. The fix is not a new mechanism - it is the same one, made
two-sided, so that gaining and losing a watcher is itself an ordinary
property write on the thing being watched.

## The general point

Most frameworks make observability a privilege you grant to particular
values, and then spend a great deal of code on the seams between values
that have it and values that do not. This one grants it to nothing,
because there is nothing to grant: a value is a node, and a node can be
watched. No wrapping, no declaration, no registration against a
cooperating owner.

What falls out is that integration stops being a category of work. A probe,
a timer that disables a server, a live counter readout, a radio group, a
cell wired to a meter - none of those are features anyone implemented. They
are two things and a wire, and they work because the only thing that ever
had to be true is that a value can be watched.
