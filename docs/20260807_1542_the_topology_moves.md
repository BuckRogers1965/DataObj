# The topology moves

An object is a shape. Its properties are the axes data can enter and leave on,
its behaviour is what it does between them, and a wire joins one coordinate to
another. A flow is not a program with an execution order - it is an
arrangement, and what the system does is a consequence of how the shapes are
joined.

That much is a static picture, and it is only half true. The interesting half
is that **the arrangement is rewritten by the traffic running through it.**

## Two graphs

Everything below is about the relationship between two structures over the
same nodes.

**T**, containment. A tree. Nodes own their properties and their children, and
`DelNode` is subtree deletion. This is the ownership and addressing structure.

**G**, flow. A digraph whose vertices are property nodes and whose edges are
`Subscriber` records, each carrying a transfer function - a handler, or the
identity when delivery just stores the value.

The invariant that binds them:

    V(G) ⊆ V(T)

Every endpoint of every wire is a vertex of the containment tree. That is
"everything is in a container", written as a type rule, and it is checkable in
one linear pass.

## The traffic rewrites the arrangement

The framework has no separate "wiring phase". Messages arriving are what
changes the shape of the system:

- a script's `create` and `connect` verbs add vertices and edges from inside a
  running flow
- `Connect` **creates the source property if it is absent** - so wiring adds an
  axis to a shape that did not have one
- stamping a property adds a dimension: a Textbox carrying `GUI_Format` is a
  different shape than one without, and that difference is data, not code
- putting a handler on a property changes an edge's transfer function - same
  graph, different semantics
- deleting an instance removes a subtree of T and every G-edge incident on it
- a client subscribing adds an edge; **the observer changes the graph by
  observing**
- a clone duplicates a subgraph; an import drops one in

So G is not a circuit diagram. It is the current state of a graph-rewriting
system, and the rewrite rules are the verbs.

## The real object is a trajectory

Which means a single snapshot of G is not the thing to reason about. The system
is a path through graph-space, and the state is a triple:

    (T, G, Q)

with Q the scheduler's queue. A step pops one message, delivers it, and that
delivery may change values, may change G, and may change T.

This reframes what the scheduler *is*. It is not an implementation detail
underneath the dataflow - it is the thing that chooses the path. Breadth-first
dispatch means the topology transforms in layers rather than depth-first down
one branch. The 1 ms wake cap is the sampling rate of the transformation. The
same-timestamp insert hint means a burst lands as one layer instead of a
staircase.

## Where the moving data meets the moving topology

This is the only place the two can disagree, and every hazard we have hit lives
exactly there.

`SndMsg` queues; `ExecTasks` dispatches later. Between the send and the
delivery, the graph can change - the sink can be deleted, renamed, re-wired.
The engine already takes a position on this, and it is worth reading as a
statement about time rather than a caching note:

> DispatchMsg re-reads the property's live Subscriber list at delivery time,
> not a snapshot.

That is a decision about **which moment's topology a message is delivered
against**: the one at arrival, not the one at departure. Everything else in
that family follows from the same question asked at different points:

- `CancelPendingSends` - a message in flight whose *sender* died
- `ScrubRegistrySubscriptions` - edges whose endpoint died while traffic was
  queued against them
- a tap orphaned when the thing it watched was deleted: an edge whose sink left
  T while G still pointed at it
- "ignore messages in flight", the rule the Sum widget wants - a pull is a
  reading of the topology at an instant, deliberately blind to what is in
  transit

None of these are bugs in the shape. In every one of them the arrangement was
correct; the disagreement was about *when*.

## Quiescence is a topological statement

The framework's shutdown rule turns out to be exactly this idea, and it was
written long before anyone framed it this way. When the task list empties,
`MainLoop` sets Main's State to Stopping and the process exits.

Read it as geometry: **no queued messages means no pending rewrites, which
means the topology can no longer change.** The program is not "finished"
because some function returned. It is finished because its shape has stopped
moving. Emergent shutdown is a fixed-point condition.

## A flow file is a path, not a picture

The same thing shows up in serialisation, and again it was already built this
way. A flow is not a dump of the graph. `NewFlow` opens a recording,
`FlowCreateObject` / `FlowSetProp` / `FlowConnect` / `FlowActivateInstance`
append actions, and `LoadFlow` **replays them** into fresh instances.

So the on-disk representation of a system's topology is *the sequence of
rewrites that produces it*. A trajectory, serialised. Which is why load can
remap names onto whatever replay mints - you are not restoring coordinates, you
are re-walking a path.

## What becomes provable

Splitting the problem this way splits the verification with it.

**Inductive invariants over the rewrite rules.** `V(G) ⊆ V(T)` holds initially
and is preserved by every verb: `CreateObject` places into a container,
`Connect` joins two existing vertices, `DeleteInstance` removes a subtree and
its incident edges. Prove it once per verb and it holds for every reachable
state, forever. No global analysis needed.

And this is the formal statement of the thing that actually bit us: **the
invariant is preserved by construction if you only use the rules.** A tap built
with `NewNode` instead of `CreateObject` is not a rule application. Neither is
a shared static buffer, nor a hardcoded constant that must agree with a
derivation elsewhere. Every failure worth remembering was a step taken outside
the rewrite system, and the invariant has no obligation to survive one.

**Snapshot geometry, on any single G.** Take strongly connected components -
Tarjan, linear. Any non-trivial SCC is a feedback region, and it is safe iff
every edge inside it carries an idempotent transfer function. Default delivery
is identity, so an all-default cycle provably converges: that is precisely why
a two-way control binding terminates. A cycle containing a value-transforming
handler does not, which is the ScriptBox wedge, stated as an algebraic property
rather than a mystery. Handlers drawn from a known table can declare
`idempotent` / `monotone` / `opaque`; an SCC containing an opaque edge is
*unproven*, not condemned, and that is a short and honest list.

**Value-level contracts, where annotations allow.** If a sink carries a pattern
P and its sources carry patterns Q, then language containment is decidable for
genuinely regular expressions - a proof that a wire can never deliver a value
its sink rejects. Masks are finite languages, so mask-to-mask compatibility is
a finite check. The checker must refuse what it cannot model rather than assume.

**Queue-aware reasoning, for the rest.** Ordering, re-entrancy and in-flight
state are properties of the transition system, not of any one graph. That needs
reasoning over (T, G, Q), and it is where the longest-lived bugs live - the
double-activate wedge had a correct shape from beginning to end.

The boundary is not "structure is geometric, time is not", which is the clean
version and it is wrong. Because dispatch is breadth-first and every hop costs
exactly one layer, *relative* timing is computable from the shape alone - see
depth and skew below. What escapes geometry is ordering within a layer,
re-entrancy, and what is in flight at a given instant.

## Depth is time

There is a temporal property that falls straight out of the shape, and it is
the oldest problem in this system rather than a new one.

Every hop is exactly one queued task. `SndMsg` inserts, `ExecTasks` dispatches
breadth-first, and downstream work never nests inside the sender's call stack.
So one edge traversed is one dispatch layer, which means **depth in the graph
is time** - as long as every edge costs the same. A value fanning out along a
two-edge path and a five-edge path recombines three layers apart, and that
number is readable off the picture without running anything.

That equal-cost assumption holds for local delivery and collapses the moment a
wire leaves the process. Hold it for this section; the next one takes it away.

The answer to it is a sync widget: an object whose job is to hold data until
the paths agree, because each route takes a different time. That is path skew,
and it is a geometric quantity here - for any vertex
with in-degree greater than one, compare the path lengths back to the common
fork. Equal, and the join is aligned. Unequal, and that node sees values from
different generations forever, by construction, and no amount of testing will
shake it out because nothing is wrong.

Three answers exist, and this system already reaches for two of them:

- **align at the join** - buffer arrivals until the set is complete, then
  emit. The sync widget. A barrier.
- **pull at an instant** - do not align at all; read every input's resting
  value at one moment and ignore what is in transit. That is exactly the Sum
  widget, and it is why "ignore messages in flight" is not a caveat but the
  entire mechanism. Sum dodges skew rather than correcting it.
- **pad the paths** - insert pass-throughs until every route is the same
  depth. The systolic answer; unbearable by hand, trivial once depths are
  computable.

All three need the inbound edge list. A barrier cannot know how many inputs it
waits for, Sum cannot gather, and a depth calculation cannot walk backwards
from a join. That is three distinct features blocked on one missing back-edge.

## Not every hop costs the same

One dispatch is microseconds. A hop through an Http object to a web service and
back is milliseconds at best, seconds routinely, and never at worst. Those live
in the same graph, drawn with the same line.

And an external hop is not simply an edge with a large weight - it is an **exit
and a re-entry**. The framework already models it that way: an I/O object takes
a request as a message and delivers the answer later as another message, so the
outbound and inbound halves are two separate edges with a gap between them that
the graph does not describe. Causality leaves the topology, spends an unbounded
interval somewhere the model cannot see, and comes back in at a different
vertex.

So the honest model is a **weighted** digraph, with weights spanning something
like nine orders of magnitude, and three distinct kinds of edge:

| class | cost | behaviour |
|---|---|---|
| **local** | one dispatch, microseconds | deterministic, always completes |
| **timed** | a Pulse or Timer period | known, periodic |
| **external** | unbounded | may be slow, may fail, may never return |

A path's character is set by its worst edge, not its average.

Three things follow, and they are not small.

**The light cone becomes metric rather than combinatorial.** "What could this
change have reached by now" stops being a ball of radius *k* in hops and
becomes a ball of radius *t* in latency. Dijkstra, not breadth-first. Two paths
of equal hop count can be a thousand layers apart in arrival.

**Two of the three alignment strategies die or weaken.** Padding paths to equal
length is finished - you cannot insert enough pass-throughs to match a network
round trip, and if you could the answer would change tomorrow. Counting inputs
at a barrier weakens badly, because a reply that never comes means a barrier
that never releases. What survives is release-on-stability, and even that needs
a deadline, because in a graph containing external edges **quiescence is not
guaranteed to arrive**.

**The picture stops telling the truth on its own.** Two diamonds drawn
identically behave completely differently if one branch contains an Http
object. Skew is no longer readable off the shape, which means the shape has to
carry the weight: an edge's latency class belongs in the rendering - a
different stroke for an external hop - or the diagram is lying by omission in
exactly the way an unbadged annotation was.

There is one piece of luck. The system can already tell when an external hop is
outstanding, because a polling I/O object keeps its task armed while it waits.
An armed poll task is the engine saying *time is still passing here, do not
call this region settled*. That is the difference between "quiescent" and
"waiting on the outside world", and it is already visible in the task list -
which is also why an open connection correctly keeps the program alive instead
of letting it exit into a fixed point that was never reached.

## A vocabulary for timing

The geometry needs words for this, and they should be the ones the machine can
actually compute:

| term | what it is |
|---|---|
| **layer** | one dispatch round; the unit in which propagation is measured |
| **depth** | path length from an originating change to a node, in layers |
| **skew** | difference in depth between two paths into the same join |
| **generation** | the set of values derived from one originating change |
| **settling** | the layers during which a region's values are still moving |
| **quiescent** | no pending delivery whose destination lies in the region |
| **alignment point** | a vertex where generations are made to agree |
| **contraction** | the algebraic property that makes settling terminate |
| **weight** | an edge's real cost, not its existence |
| **edge class** | local, timed, or external - a path inherits its worst |
| **deadline** | the bound a barrier needs, since quiescence may never come |

Note that the last one is not new: it is the idempotence condition from the SCC
analysis above. A region settles for the same reason a cycle converges, so
"does this stabilise" and "is this feedback safe" are one question asked at two
scales.

## Wiggle, settle, release

Which suggests a better barrier than counting inputs.

A change entering a region does not arrive everywhere at once - it propagates
layer by layer, and while it does, the region's values are inconsistent. Some
nodes hold the new generation, some still hold the old, and a join reading in
that window reads a mixture. The shape is wiggling.

So do not release on a count, and do not release on a timer. **Release on
stability**: hold at the alignment point until the region is quiescent for that
generation, then let the whole block through at once.

This is the same move a digital simulator makes with delta cycles - settle the
combinational mesh, commit at the clock edge - and what a sync widget is
reaching for. It is more robust than the alternatives because it
needs no fan-in count and no equal-length paths; it only needs to know when the
wiggling stopped.

And this framework can already answer that question. Global quiescence is
*already* the shutdown rule: the task list empties, nothing more can change,
Main goes to Stopping. A release barrier is the same predicate evaluated over a
region instead of the whole session - and `CancelPendingSends` proves the
machinery exists, since it already walks queued envelopes examining their
destinations. "Is anything still in flight toward my region" is a question the
scheduler can be asked.

The missing piece is naming the generation. A message is a node, and TCP
already stamps `Conn` on every one to say which connection it came from; an
epoch stamp is the same move for time rather than origin. It also turns skew
from silent into *detectable*: a join receiving two different generations can
say so, which is the difference between a subtly wrong number and an error
message.

## Why this is unusually tractable here

Formal methods normally mean building a model beside the code, and the model
drifts. Here **the graph is the live data structure.** T and G are both just
nodes. A verifier is an object that walks the tree and reports - no extraction
step, no parser, nothing to keep in sync, and it can run continuously instead
of at build time.

The first thing worth building is small: an object that checks `V(G) ⊆ V(T)`
and runs Tarjan over the subscription graph, flagging any SCC that contains a
non-idempotent or unclassified edge. One linear pass over data that already
exists, and it covers the orphaned tap, the delete-scrub obligation, and the
feedback wedge - three real bugs from one walk.

The larger point is the framing. A static picture of the objects tells you what
*can* flow. It is the trajectory - the topology transforming under its own
traffic, one scheduled delivery at a time - that tells you what the system
*is*.
