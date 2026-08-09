# The Value You Meant

Drag a slider from 20 to 80 and it produces about sixty values. You meant
one of them.

Every design question in this note follows from that. The sixty are real -
the widget genuinely passed through them - but only the last one is
information. The rest are the path, and nobody downstream asked for the
path.

## State and events are different cargo

A **setpoint** is state. A target moisture of 60%, a window position, an
Enable flag, the text in a field. Only the current value means anything.
Ask again in a second and the answer is complete on its own.

An **event** is something that happened. A button press, a packet
arriving, a peer closing, a pulse edge, end of file. Each one is
information *because it occurred*, and two of them are not the same as
one.

Almost every transport problem I have created for myself came from
carrying the first kind in machinery built for the second.

## The output side: sixty deliveries for one intent

The canvas framework routes property changes as messages. Every write is
an envelope on the scheduler, a dispatch, and a WebSocket frame. So the
drag costs sixty of each, and the browser discards fifty-nine of them
because it cannot paint faster than a frame.

The embedded framework does the opposite, and it is instructive how
little machinery it takes:

```c
void set_id(uint8_t id, float val) { setDirty(id); items()[id].value = val; }
```

One bit per item. A sync pass walks the dirty bits and sends the current
value. Ten writes between passes cost one message carrying the last one.

That single difference produces three properties the message version does
not have:

- **Cost stops scaling with change rate.** A value that updates a thousand
  times a second costs the same as one that updates once, because the
  consumer is what sets the rate.
- **Overload degrades in the right direction.** When the queue is full,
  `if (!fifo_send(msg)) return;` leaves the bit set for next time. You
  lose resolution, never the current value. An event queue under the same
  load either grows without bound or drops something that mattered.
- **It is self-healing.** Miss a state update and the next one is still
  correct. Miss an event and you are wrong forever, with no way to notice.

The fix for the canvas is the same shape one level up: a per-connection
dirty set in the bridge, flushed at 60Hz. Sixteen milliseconds is a frame,
and a frame is the fastest a person can be shown anything - so it is both
the cheapest correct answer and sixty times less work than the main loop
currently does.

## The input side: the values that should never arrive

The same argument runs backwards, and it is where it gets interesting.

Formatting a phone number seems like it belongs in the engine. It does
not, and the reason is structural: **a write cannot be refused.** Messages
here go one way; there is no channel to answer "no, not that". So
validating inward means inventing a rejection path - writes that can fail,
an error branch at every call site, and some way to tell the client why.

Then it gets worse, because typing is intermediate by nature. `555-12`
must be allowed to exist while someone is typing it and must never reach
anything downstream. Engine-side, that is a distinction between an
in-progress value and a committed one, on every property, on the hot path,
so that every write in the system pays for something two text boxes care
about.

The annotation version costs the engine one thing: a string property it
stores and never reads.

```
GUI_Format:  (###) ###-####
GUI_Pattern: ^\d{10}$
```

The client gates the keystrokes where the keystrokes already are, in the
browser's own event loop, at the speed a person types. The engine never
sees a partial phone number. It never sees an over-long one. It needs no
concept of validity at all, because invalid states are absorbed at the
edge that created them.

And because it is an ordinary property set at runtime, a phone field and
an SSN field differ by a value rather than by a widget class or a
recompile. The engine stays ignorant, which is exactly what lets the rule
be arbitrary.

## So why is a final value *better*, not just cheaper?

Cost is the least interesting reason. Four better ones:

**Intermediate values are frequently invalid.** A half-typed phone number
is not a phone number. A slider dragged from 20 to 80 passes through every
value between, and if something acts on arrival, it acts on all of them.
The settled value is the only one that was ever meant.

**One intent should cause one action.** Sixty deliveries for one gesture
is sixty chances to do the wrong thing, and every consumer then needs its
own defence - a settle window, a dedupe filter, a "don't act at creation"
rule. Those defences exist in the canvas framework precisely because the
transport hands over everything.

**Sampling removes the need for debouncing entirely.** The embedded
control task wakes at 1Hz and reads the registry. It does not consume
events; it looks at state. The sixty intermediate values were overwritten
in place before anything looked, so the debounce is a rate mismatch rather
than code. There is no debounce logic in that system because there is
nothing to debounce.

**A late reader is still a correct reader.** State is complete at every
instant. A browser that connects halfway through, a core that fell behind,
a page reloaded after an hour - all get the truth immediately, with no
replay and no history. An event stream has to be caught up.

## The boundary

Coalescing is wrong for events, and the failure is silent.

Two button presses are not one button press. Two packets are not one
packet. A connection that dropped and came back is not the same as one
that never dropped. Every pulse edge means something. `msg_eof` cannot be
merged with the chunk before it.

So the rule is not "coalesce everything". It is: **know which kind of
cargo you have, and use the machinery that fits it.** State gets stored
and sampled - last value wins, drop the rest, sample at the observer's
rate. Events get queued and delivered - each one exactly once, in order,
none merged.

The trouble in the canvas framework today is that both go down the same
wire as messages. A slider's position and a button's press are
indistinguishable to the transport, so it treats the setpoint as sixty
events and delivers every one, and separately gives objects no safe way to
tell a real event from an intermediate state.

Getting that distinction right is what makes the expensive machinery
cheap: events keep their guarantees because there are far fewer of them
once state stops pretending to be one.

## The rule, short

Ask what a subscriber would want if it woke up right now and had missed
everything.

If the answer is "just tell me the current value" - that is state.
Coalesce it, sample it, gate it at the edge, and never spend a byte
describing the path.

If the answer is "tell me what happened, all of it, in order" - that is an
event. Do not touch it.
