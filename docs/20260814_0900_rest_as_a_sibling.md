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

## The likely shape: two halves

The Bridge may need to break in two, along the seam that already exists in
`app.js`:

- **The verb half** - create, connect, disconnect, set, clone, move, delete,
  activate, internals, save/load, bind-port. Engine-neutral, no notion of who is
  asking, already language-neutral because the mechanisms live in `object.c` and
  the bridge only spells them.
- **The session half** - connections, viewers, aliases, replay, the panel
  formatter. Everything that exists because a long-lived stateful client is
  watching.

If that split is real, REST is the verb half plus an HTTP mouth, and the web
bridge is the verb half plus the session half. If it is not real, we will find
out by trying to draw the line and discovering something in the middle that
neither half can own.

## Open questions

These are the ones we cannot answer from the armchair, listed so they can be
answered on purpose rather than by accident.

1. **Does REST drive the engine, or drive the Bridge?** An object that speaks
   HTTP and calls the same engine verbs is the cleaner statement of "translators
   are syntax-only". A client that speaks the existing JSON protocol over HTTP is
   less code and proves less. The first is the honest test.

2. **What is a read?** `GET` must be side-effect free, and today the only way to
   read a property over the protocol is `subscribe` - which, through late
   binding, *creates the property it was asked about* if it does not exist. That
   is correct and deliberate for wiring; it is a disaster for `GET`. So a read
   verb that does not conjure is likely the first missing primitive, and it may
   be missing from the JSON protocol too.

3. **Do properties have addresses yet?** `GET /Root/Filter/Mode` is the whole
   argument for the promoted roadmap item. If a property is not addressable, REST
   has no resources below the instance.

4. **Are events part of v1?** A canvas needs a stream. A REST client may be
   content to poll, or may want SSE or a webhook. Deciding "no events in v1" is
   allowed; deciding it silently is not.

5. **What is an instance as a resource?** The published Interface is already a
   schema of shape. Units, ranges, read-only-ness, ordering and grouping are just
   more properties on the property - so the question is what `GET /Root/Filter`
   returns, and whether it is `NodeToJson` restricted to one instance or
   something narrower.

6. **Is there a session at all?** If REST is stateless and addresses by engine
   path, the alias table has no role in it. That would make the bridge's alias
   machinery a property of the *browser* client, which is a much smaller claim
   than it currently makes for itself.

7. **What does a REST client do about containment?** A canvas asks for the
   contents of one view because it is drawing that view. Does REST return an
   instance's members inline, by link, or only on request - and does that
   decision belong to the translator or to the walk?

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

1. Draw the line between the two halves on paper, from the function list, before
   moving a single line of code.
2. Answer question 2 - the read verb - because everything else needs it.
3. Stand up the smallest thing that can create an instance and read it back.
4. Write the equivalence test before adding a third verb.

The thing to resist is making REST *work* by teaching it about the GUI's
conventions. If it needs to know what a panel is, we have drawn the line in the
wrong place.
