# REST as a sibling of the GUI

*Plan and open questions, morning of 14 August 2026.*

Yesterday's reframe was that the GUI is incidental to the dataflow, and that the
claim is untested while there is exactly one window. Today we build the second
one: a REST interface that is a **sibling** of the web GUI, not a layer over it
and not a thing bolted to its side.

The instrument is more useful than the feature. A translator with no eyes cannot
politely work around an assumption; it either can express the gesture or it
cannot, and every place it cannot is a hole we have been walking past because a
browser happened to fill it.

## The expectation

The Bridge was written while the only client was a canvas. It is very likely
that a set of decisions in it are not protocol decisions at all - they are GUI
decisions that were never named as such, because nothing ever asked them a
different question.

Where we expect to find them, in rough order of confidence:

**Viewers.** `connViews` records, per connection, the container keys that
connection is looking at, and `Bridge_InstanceEvent` delivers a creation only to
connections viewing its container. That is exactly right for a canvas - a browser
should not be told about the contents of a view nobody opened - and it is a
strange sentence to say to a REST client, which does not "look at" anything. It
asks, and expects the answer to be complete. Last night this cost a test: my
clone check asserted that a widget's controls were announced, and the design
deliberately does not announce into a container nobody is viewing.

**Session naming.** The bridge keeps an alias per connection and writes session
paths into instances (`Target` gets an engine path from the engine and is then
overwritten with a session alias). A REST client's addresses are URLs, and the
obvious URL is the engine path. So which naming is canonical, and what happens to
a flow file written by one and read by the other?

**Presentation inside the walk.** `Bridge_Internals` lays out a panel while it
walks - `y += mh + 14`, an inset, a `PROP_TEXTBOX` floor for anything unstamped.
Those are a formatter's opinions living inside the traversal. A REST client wants
the same walk with none of them.

**Replay.** The flow log exists so a reconnecting browser can be brought back to
the current state. A stateless client has no reconnection to be caught up on.

## The route not taken: splitting the Bridge

The plan above was to break `bridge.c` in two and let REST inherit the verb
half. We spent the first hour drawing that line from the function list, as step
one said to, and the line came out somewhere that made the whole exercise
pointless.

`Bridge_Set` - the busiest verb in the protocol - is: resolve a path, then call
`SetOrDeliverProp`. That is the verb. Everything around it is Name and
Container re-keying, alias bookkeeping, and a scoped event. `Bridge_Dispatch`'s
twenty-one commands sit on `CreateObject`, `CloneInstance`, `Connect`,
`Disconnect`, `LinkPropertyAs`, `ActivateInstance`, `SetOrDeliverProp`,
`ResolvePath` and `PathOfInstance` - every one of them already public in
`src/object.h`.

**There is no verb half to extract, because the extraction already happened.**
It happened on purpose, and the reason was written down at the time: mechanisms
land in `object.c` as language-neutral calls, and Bridge, Script and MCP are
syntax only. The two-halves plan was a proposal to do work that had been
finished months earlier. Pull the engine calls out of `bridge.c` and you get a
file of thin wrappers on one side, and on the other the entire three thousand
lines still sitting exactly where they were - because those three thousand
lines *are* the session half. The split is not a refactor. It is an observation
about a file that is already one half of itself.

Three further reasons the route was wrong, which only became visible once the
first one was:

**The shape is a stack, not a surgery.** REST is a translator with HTTP under
it and TCP under that. This is precisely the composition the web flow already
is - `TCP -> Router -> Http/WebSocket -> Bridge`, assembled by `Connect()` in
`CreateDefaultApp` with no object aware of its neighbours. A REST object is a
new element in an existing shape. Nothing about placing it there requires
opening the bridge at all.

**A request is a transaction, and that is a different animal.** A REST request
arrives complete - verb, arguments, reply, forget. That is the Script object's
`Cmd` port with no `Evt`, which is why the thing kept feeling like scripting
rather than like a second GUI. The Bridge's job is the opposite one: maintain a
live model of the tree inside somebody else's process and keep it true minute
by minute. Sharing one file between those two jobs would push session concepts
into the stateless path, which is the exact failure this whole exercise exists
to detect.

**The risk is all downside.** Refactoring the bridge means operating on the
thing that runs the GUI, for the benefit of a thing that does not exist yet.
Building alongside it costs nothing, and it leaves the sharing question to be
settled afterwards with evidence instead of prediction. The answer may well be
that they share nothing but the engine - which is the strongest available form
of the sibling claim, not a weaker one.

## What the registry already is

The read side turned out to be built. Every class node carries an `Interface`
property (`PublishProp`, `src/object.c`), and `Widget_Publish` fills it from
the widget's own layout table: one entry per property, carrying `Name`, the
control type, the class default, and the declared `W`/`H`. TCPPort publishes
`In`, `Out` and `StandardPortList` beside those as `PROP_NULL` - real
properties with no control on the panel. `InterfacePropForInstance` joins an
instance's property back to its published entry.

So the catalogue a client needs in order to decide what to do next is already
in the tree, already maintained by every widget as a side effect of declaring
its own panel, and it draws the only privacy line REST needs: published is the
object's face, unpublished is engine internals.

## The interface to the client

Two trees, and the client walks both.

    GET /registry                    the kinds - libraries -> classes -> Interface
    GET /registry/TCPPort            one class's published interface
    GET /tree/Root                   the things - containment from the root view
    GET /tree/Root/Palette/TCPPort   one instance
    PUT /tree/Root/Palette/TCPPort/Send        body: 1

`/registry` changes only when something loads or unloads; `/tree` changes
constantly. A client bootstraps by reading the registry once, decides what it
wants, and then lives entirely in the tree.

An instance is the join of its values against its class's published interface:

    { "path": "/Root/Palette/TCPPort", "class": "TCPPort",
      "container": "/Root/Palette",
      "properties": {
        "Send":      { "value": "0",     "widget": "MoButton", "default": "0" },
        "TxData":    { "value": "hello", "widget": "Textbox",  "default": "" },
        "Connected": { "value": "1",     "widget": "LED" } },
      "children": [] }

The `widget` field says how to read and render a property - Textbox is data
either direction, LED is state to read, Dropdown means a companion
`<prop>List` holds the choices. It is deliberately *not* how a client decides
what to drive: inferring "MoButton means press it" is the REST object being
clever about widgets, and the face below replaces that guess with a statement.

### There is no activate verb

An earlier draft listed `activate` among the verbs. Objects do not work that
way. TCPPort has `Open`, `Listen`, `Close`, `Send`, `ClearTx`, `ClearRx`;
Resolver has `Lookup` and `Cancel`; Filter has no command at all, only `Enable`
and an `In` handler. Each command is an ordinary property with its own handler,
guarded on a rising 1, and the handler arms whatever task its work needs.
`TCPPort_Activate` exists only as the creation-time normalizer - make the
lights agree with the state, act on nothing.

So `PUT /tree/.../TCPPort/Send` with `1` is the press, and `SetOrDeliverProp`
delivers it to `TCPPort_OnSend` exactly as the panel's MoButton does.
`tcpport.c` promised this in its own header before REST was thought of: *a
script can press Open/Listen/Send exactly as the panel's MoButtons do.* REST is
one more thing that presses them. A widget that grows a new command button
grows a new endpoint, with no change here.

### The inspector is one parameter

A property is a node, it lives in a container, and it is a container. So the
URL path is just the node path, all the way down, and
`/tree/Root/Palette/TCPPort/TxData/W` is legal and means what it looks like.
The inspector is therefore not a second feature:

    GET /tree/Root/Palette/TCPPort?depth=3       walk down
    GET /tree/Root/Palette/TCPPort?raw=1         unpublished properties too
    GET /tree/Root/Palette/TCPPort/connections   the Subscriber records

`raw=1` is the debugging view, and it never emits pointer-valued properties -
`local`, `Activate`, `OnMsg` are opaque handles and stay opaque. `connections`
is `Bridge_ListConnections`' walk narrowed to a single instance: every property
carrying `Subscriber` sub-nodes, each already self-describing as
`{Instance, Port}`.

One implementation note, because it is the single place the URL grammar meets a
real limit: `ResolvePath` knows addressable instances only. REST resolves the
longest prefix the trie recognises and walks the remainder with `GetPropNode`.
That one rule is what makes instance-versus-property vanish from the grammar.

### Three rules, fixed now

1. **A GET never conjures.** An unresolved path is a 404 and creates nothing.
   Late binding stays a write-side gesture.
2. **The URL path is the engine path, verbatim.** No aliases, no session table.
3. **Published versus raw is the only privacy boundary.** Not a per-property
   flag - the class has already said what its face is.

## Feeding and waiting: eof is the end of the response body

A `PUT` on a property is fire-and-forget, and two of them - set the input, push
the button - give a client nothing back. What a caller actually wants is one
call that feeds the widget and returns what the widget produced. That cannot be
answered inside the handler the way `http.c` answers a file request. The
connection has to be held, the output subscribed, the result accumulated, and
the reply sent later.

The framing for that already exists and is not new: **`msg_eof` is the end of
the response body.** A Reader sends chunks and then eof, which is exactly a
body followed by "that is all of it". HTTP wants `Content-Length` known up
front - the reason `Http_SendResponse` reads a whole file before sending a byte
- and accumulating until eof is precisely what makes it computable. The two
framings line up one for one: an eof-terminated stream is `Content-Length`, and
a message-per-chunk stream with no eof yet is chunked transfer-encoding, which
is HTTP's own name for the same idea.

So the per-request state is the DNS pattern rather than the Bridge's:

    POST /tree/Root/MyFlow/Filter        body: the input
      -> feed the input
      -> remember { Conn, output property, buffer, deadline }
      -> reply nothing yet; the socket stays open
      ... later, from the subscription callback ...
      -> chunk       -> append
      -> msg_eof     -> build the response, reply to that Conn, forget the record

A `Pending` list keyed by `Conn` and a timeout task - which is what
`objects/dns` already does with `outstanding`, and part of why it was worth
building first. The deadline is not optional: something that never eofs would
otherwise hold a socket forever, and the timeout is the only thing that can
decide the answer is not coming.

## The face: what a class says about being driven

Rather than have REST work out how to drive a widget, the class states it. Four
lines:

    in:      TxData
    trigger: Send
    out:     RxData
    done:    quiet 250

    in:      HostName
    trigger: Lookup
    out:     Address
    done:    Found == 1

With that, `POST /tree/Root/MyResolver` carrying a hostname needs no knowledge
of what a Resolver is: set the inputs, push the button, collect the outputs,
answer.

`done` is where the early send lives, and the useful cases need no interpreter:

- `eof` - the streaming case, collect everything
- `first` - single-shot, answer on the first message out
- `quiet <ms>` - answer once the output stops changing
- `<Prop> == <value>` - **the early send**: watch a property and answer the
  moment it matches. Three tokens - property, comparison, value - evaluated
  against a subscription. When three are not enough, `done` names a property
  that the object's own contained script sets, so the simple case is data and
  the hard case is a language host the framework already knows how to hold.

There is a `fail` line beside it, because a request has three outcomes and not
two: `fail: Error != ""` becomes a 5xx carrying the error text, and the
deadline becomes a 504.

### Three tiers, and most objects stay in the first

The face is an **override**, not a requirement - the same shape
`ReservedIn`/`ReservedOut` turned out to have for `Connect`: a default exists,
and the declaration only exists to disagree with it.

1. **The default.** `in: In`, no trigger, `out: Out`, `done: eof`. Filter,
   Reader, Writer, Out - anything on the plain dataflow shape. Feed `In`,
   collect `Out` until eof. No file, nothing to maintain, nothing to keep in
   step. It lives on `Object` and reaches everything by the ordinary parent
   walk.
2. **`ReservedIn`/`ReservedOut` where declared.** A composite View already
   names the control standing in for it on each side, so the default reads
   those instead of `In`/`Out`. The dot-connect sugar answers the REST question
   too, with no second declaration.
3. **`show/rest/<name>.face`.** Only for what fits neither, which is exactly
   the objects with a button - TCPPort, Resolver, and Queue, whose
   `trigger: Clock` and `done: first` is the clearest statement of why the file
   needs to exist at all: a Queue produces nothing until something clocks it.

### This is MCPSource in reverse

MCPSource reads an external server's tool schemas and generates framework views
so a user can drive that tool from the canvas. The face and `/registry` are the
same translation running the other way: framework objects published as a schema
so an outside caller can drive them. One functor, two directions - which is
also why the inbound half being already proven (MCPSource's generated agent
views build, submit, round-trip and clone) is evidence about this half.

Read that way, `GET /registry` is a tool catalogue and `POST /tree/<path>` is a
tool invocation, and the roadmap's "the framework exposed as an MCP server so
agents can invoke and build flows" is this same work with a different mouth on
it. The face is the shared part; HTTP and MCP are two spellings of it. Which is
worth getting right here rather than re-deriving it later, and it forces two
things:

**The description already exists.** An agent needs to know what a thing does,
not only what its arguments are called - and every widget already ships a
README that its Help row points at (`objects/resolver/README.md`). That is the
tool description: written, shipped, and already read by the panel. The face
does not need a `description:` line, it needs to name that file.

**Inputs are plural.** `in: TxData` is a single argument; a tool with three
needs all three named. Types do not need declaring alongside them - they come
from the published `Interface` entry, which already carries the widget type and
the default. MCPSource's own lesson is the reason this stays small: DataObj and
the MCP protocol are both stringly-typed with automatic conversion, so no
schema layer is needed between them.

The one syntax question left open is how several inputs are named and how a
`POST` body maps onto them - a list (`in: Host, Port, Payload`) with a JSON
object body keyed by property name is the obvious answer, and it is the last
thing in the face that is not yet decided.


### Where the face lives

`PublishShow` (`objects/control/control.c`) does not put `js` and `css` on the
class node. It creates a `Show` property, then a **`web` sub-property beneath
it**, and hangs the payload there. That extra level exists for one reason: web
was never meant to be the only surface. `Show/rest` is one more sub-property
under the same node, and no mechanism has to be invented for it.

The face is therefore an ordinary editable file - `show/rest/<name>.face` -
escaped into the module by `show.mk` exactly as the browser half already is,
and published at ClassStart. A REST face can be added to an existing widget
without touching its C, and it sits beside the browser half as a peer rather
than as an afterthought inside the logic.

`GET /registry/TCPPort` then returns the published `Interface` *and* the face,
so a client is told how to drive the object instead of deducing it - which is
the whole difference between a translator that knows about widgets and one that
does not.


## Open questions

Four of the seven above answered themselves in the course of the reading.
**(2)** A read is a walk of the node joined to its published interface, and it
does not conjure. **(3)** Properties are addressable because they are nodes,
and the URL is the node path. **(5)** An instance as a resource is its values
joined to its class's `Interface`, with raw as a separate view rather than a
different endpoint. **(6)** There is no session: REST addresses by engine path,
which makes the alias machinery a property of the *browser* client
specifically, exactly as suspected.

What is left, renumbered:

1. **Does REST drive the engine, or the Bridge?** Answered in principle - the
   engine - but it stays on the list until the first endpoint has proved it
   needs nothing at all from `bridge.c`.

2. **Are events part of v1?** Still open, still to be decided out loud rather
   than by omission. Polling a property costs one walk; a client that wants to
   watch a value change wants SSE or a webhook. v1 is polling unless something
   forces the issue.

3. **Containment: inline, by link, or on request?** `GET /tree/Root` returning
   every member of every nested view is a session dump; returning names and
   making the client descend is a round trip per level. The likely answer is
   `?depth=` defaulting to 1, which puts the decision in the caller's hands and
   makes it the translator's business rather than the walk's.

4. **Its own port, or a third target on the Router?** v1 takes its own TCP: no
   change to the Router, no risk to the running GUI, and it makes the sibling
   claim literal - if REST needs nothing from the bridge, it can prove it on its
   own socket. Folding it onto the shared port is a later and separate
   decision, and the rule that decides it there is a path prefix, not the
   Router's current header sniff.

5. **Authentication.** Deferred. v1 binds where it is told and that is the
   model, the same as the raw bridge flow.

## How we will know it worked

Not by the endpoint responding. By **building the same flow twice** - once over
the raw JSON protocol, once over REST - and comparing the resulting node trees.
Identical trees mean the translators are interchangeable, and that is a
mechanical proof rather than a claim. That suite is a new category: the existing
ones prove a translator works; this one proves a translator does not matter.

The second measure is subtractive. Every time REST cannot express something the
browser can, that is a hole in the engine or the protocol, not a missing REST
feature - and the fix goes downward, never sideways into the translator.

## Where to start

1. `objects/rest/` on the udp/dns pattern: its own TCP, its own port, and an
   HTTP request parser that handles a method, a path and a `Content-Length`
   body arriving across several recvs. `Http_SendResponse`'s header build and
   its `Conn` tagging are worth copying; the rest of `http.c` is GET-only
   static file serving and does not fit.
2. `GET /registry` first. It is pure read, it touches nothing live, and it is
   the endpoint a client cannot start without.
3. `GET /tree/<path>`, with the interface join.
4. One `PUT`, and puppet an existing widget from `curl` - press a TCPPort's
   Listen and watch the panel's LED change in the browser at the same moment.
   That is the whole of the phase-one proof: two translators, one live tree.
5. Only then clone-into-a-view and connect, and the equivalence test before the
   third verb.

The thing to resist is making REST *work* by teaching it about the GUI's
conventions. If it needs to know what a panel is, we have drawn the line in the
wrong place.

---

# What we actually built

*Added the same day, after the first requests went through.*

## The slice

`objects/rest/` - one file, one Makefile, one README, and no change to any
existing object. It is wired in `CreateDefaultApp` exactly the way Http is,
because it is the same shape:

    TCP (-port + 200) --> Rest --> TCP.In

`/Root/mcp` is created empty at boot beside the palette. What you drag into
it is what is published. Nothing marks an object as exported, because
containment already says it.

Two operations, and between them a script can puppet anything in that view:

    GET  /                 the list - name and class
    GET  /manifest         the same members, fully described
    GET  /Textbox          the member's value, through its face
    PUT  /Button           write the member's face input - the press
    GET  /Textbox/Value    one named property
    PUT  /Textbox/Value    write one named property

The whole write path is `SetOrDeliverProp`. It decides for itself whether a
name resolves to a port - in which case the port's handler runs, exactly as
wired traffic would - or a plain data property, which is a direct write. So
pressing a button and typing into a textbox are one call, and `rest.c`
contains no knowledge of any widget. A widget that grows a new command grows
a new endpoint with nothing to change here, and a class written next year is
drivable the moment it is dragged into the view.

That is the part that felt bigger than the slice. It is not that REST works.
It is that REST needed no adapter, no schema authored by hand, and no
vocabulary to keep in step - because the engine already had one verb that
covers both cases and every widget already publishes its own interface.

## Five things the design got wrong on contact

Every one of these was found by using it, in under an hour, and every one was
the same kind of mistake: the translator knowing something it had no business
knowing, or reporting something the caller could not have asked about.

**The URL leaked the engine path.** The first cut answered at
`/tree/Root/mcp/Port/Listen`. That prefix is where the published view happens
to live, which is the one thing a caller must never have to know. The view IS
the root of the URL space: `/Port/Listen`. Move `ManifestView` and every URL
follows it, because none of them names it. The replies leaked it too - `path`
fields, and an error that helpfully reported the configured location to
someone who could not act on it. The log carries that now; the answer does
not.

**The name in the manifest was the wrong name.** It reported `GetNameStr`,
the node's birth name from its class - so an instance the user had renamed
`bob` listed as `Checkbox`. The address is the `Name` property, which is what
`PathOfInstance` builds paths out of and what a rename changes. Reporting the
name a caller cannot use is worse than reporting nothing.

**The face has to resolve against what the instance HAS.** The default was
written for the dataflow shape - feed `In`, collect `Out` - which is right
for Filter and useless for a Button, whose only data is `Value`. The rule is
a resolution order over what is actually present: a declared
`ReservedIn`/`ReservedOut` wins, then `In`/`Out` if they exist, otherwise
`Value`. No list of class names anywhere, so nothing has to be revisited when
a class is added.

**Reading a container lists it.** A View has no `In`, no `Out` and no
`Value`, so a bare GET on one had nothing to return. Its contents are what
reading it means - and that falls out of the same test, not a class check: if
the face's property is not there, list the members. A TCPPort's `RxData` is
there, so it still reads its output. A sub-view was already addressable
through (`/View_1/knob`), so listing it just completes a grammar that already
worked.

**The body ends in a newline.** HTTP does not ask for one and `Content-Length`
counts it. It matters twice: a shell prompt does not land on top of the
output, and a newline is the frame that line-oriented readers use to turn a
stream into events. That also settles the streaming question before we get
there - when a member produces several messages before `msg_eof`, one JSON
object per line is NDJSON, and the readers that work now keep working.

## The response contract, written down late

The worst of it was not any single wrong field. It was that the response
contract was never written before the code, so it got discovered one
correction at a time: the path leak, then a repeated `"property":"Value",
"value":...`, then an over-correction into raw text that a REST interface has
no business emitting, then the envelope.

The contract is a paragraph, and it would have cost one exchange:

> Every response is JSON. Every response carries `status`, `Success` or
> `Error`. An error carries `error` with a reason a caller can act on. A read
> adds `value` - the one thing the caller asked for and did not already have.
> A write adds nothing, because the caller knows what it sent; it needs to
> know whether it landed and why not. The HTTP status agrees with the body.
> The URL space is the published view, and nothing else appears in it.

The lesson is not about REST. It is that the novel part of a job gets designed
and the conventional part gets generated, and the conventional part is where
the known-correct answer was sitting the whole time.

## The port that took down the harness

Adding the REST flow put the framework's socket at `-port + 1`. `run.sh`
gives each build variant an offset of 1 to 5 from the same base, so every
variant's REST socket landed on the NEXT variant's web port. First one up
wins; the rest fail to bind, their raw bridges never compose, and twelve
suites report "connection refused" as CRASH. A desktop instance running the
old build made it worse - it sat on 8084 by itself and blocked the debug
variant with nothing else running at all.

Two fixes, and the second is the real one:

- `REST_PORT_OFFSET 200`, clear of the variant band, with the reason in the
  comment so nobody sets it back.
- The harness moved to high bases - web 8501, raw 8601, cdp 9501 - because
  the old ones shared a base with the desktop instance. **A framework opens
  more than the port it is named after**, and now that an object can add a
  socket, a port plan that assumes one port per framework is a trap that will
  spring again the next time an object listens.

## Still ahead

`POST` - feed the inputs, wait, answer - which needs the pending record
(`Conn`, output property, deadline) and the completion rules beyond `eof`.
The `show/rest` face file for the objects that need a trigger. Then cloning
into views and connecting, which is where a REST client stops driving an app
and starts building one.
