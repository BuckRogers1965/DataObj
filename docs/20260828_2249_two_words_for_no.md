# Two words for no

Tomorrow is step 2: convert the 239 `return rtrn_dropped` sites in
`objects/` to `rtrn_unhandled` where they mean it, and then let only
`rtrn_unhandled` move a walk on. It is the largest mechanical diff on the
plan and the one with the least new code in it. This is why it is worth a
day, how to do it without spending a week finding out what broke, what it
buys, and - the question that actually decides the shape of the day - what
it does to the harness.

## Why

An object answering a message can currently say three things, and in
practice says two: `rtrn_handled` and `rtrn_dropped`. `rtrn_propagate`
exists for probes. `rtrn_unhandled` was appended yesterday and almost
nothing uses it yet.

Two words is one too few, because `dropped` is doing three jobs:

- **I have never heard of this.** A `default:` in a switch on message id,
  a guard that the message is not one of mine. 58 of the sites.
- **I have nothing to do this with.** `!local`, `!data`, a missing
  instance. 105 of the sites, the largest group by far.
- **I heard it and the answer is no.** A disabled control, an argument
  that is not allowed. Only 2 sites say this today in so many words,
  which is itself the evidence: there is no way to say it, so nobody
  says it.

Those are not the same answer, and the engine has to tell them apart
because the walks it runs turn on exactly that distinction. A class chain
walks out to `Object` looking for someone who claims a message; a handler
chain walks the records on a property looking for the same. Both currently
treat `dropped` as "keep going". So a class that has never heard of a
message and a class that has refused it are indistinguishable, and the
walk carries on past both.

That is not theoretical. It bit twice today. `PuntGesture` walks a gesture
out through the containers of the thing you clicked, and the first version
of it stopped at the first level - because `PuntToClass` reports a whole
class chain that nobody wanted as `dropped`, and I read that as an answer.
A cell's gesture never reached the sheet the cell is in. The fix was to
treat only `rtrn_handled` as an answer, which is the right rule and also a
confession: with two words, "answered" is the only thing you can test for,
because "not answered" has two spellings and one of them is a lie.

The subtler cost is on the storing side, and it is the one that made this
step necessary rather than tidy. The universal default lives at the end of
the class chain: nobody had an opinion, so store the payload on the named
property. But it is guarded by whether a handler EXISTS, not by what the
handler said:

```c
if (!propnode || !HasHandler(propnode))
{
    SetPropStr(instance, prop, val ? val : "");
    return rtrn_handled;
}
```

So a handler that declined because the message was not its message id
suppresses the store exactly as firmly as one that refused the write on
purpose. The engine cannot tell whether a thing was handled by storing it
or handled by deliberately not storing it, so it guesses from the presence
of a function pointer. Three answers collapsed into two, and then the two
inferred from something that is not an answer at all.

## How

Two commits, in this order, because a bisect has to be able to tell them
apart.

**First, convert.** Site by site, with the triage above as the rule:

- a `default:` case, or a "not my message id" guard, becomes
  `rtrn_unhandled`
- a `!local` / `!data` / missing-state guard becomes `rtrn_unhandled` -
  an object with no state has no opinion, it has an absence
- a failure - `fopen` returned NULL, a parse did not - stays `dropped`.
  It was heard, it was attempted, and it did not work. That is not a
  shrug.
- a deliberate no - disabled, not allowed - stays `dropped` and is a
  candidate for the refusal verdict in step 3. Marking these as they go
  past is most of step 3's work done for free.

`tcp.c` (21), `udp.c` (16), `websocket.c` and `dns.c` (11 each) are a
third of the total between them, and they are all the same two shapes.
The 63 in the "other" bucket are the ones to read properly.

**Then flip the walks.** `PuntToClass` and `DeliverToHandlers` stop
continuing on `dropped`; only `rtrn_unhandled` moves a walk on.

This order matters because the flip is what makes a missed site visible,
and it fails closed: a site left as `dropped` stops a walk that used to
continue, so the symptom is one object going quiet in one situation.
Localised, reproducible, and it names itself in the log. The opposite
failure - a walk continuing past something that meant no - is invisible,
and would show up weeks later as a parent quietly answering for a child.

## What it gives

Refusal becomes expressible, and that is step 3, and step 3 is the whole
basis of the security topic: policy as an intercept, deny, administration
separated from access. None of it is buildable while "no" and "not mine"
are the same word.

The trampoline (step 5) needs precisely these three answers. A script
installed on a property has to be able to decline so the compiled handler
underneath still runs, and refuse so it does not. That is the difference
between a script that observes and a script that governs, and it is one
enum value.

The default becomes honest: store because nobody had an opinion, not
store because nobody registered a function.

And the log gets a distinction it does not have today. "Nobody claimed
this" and "somebody said no" print the same line now. They are completely
different bugs.

## What it does to the harness

This is the part to decide before starting, not after.

Most suites should not notice. They assert on behaviour - events, property
values, what renders - and not on verdicts, so a conversion that preserves
behaviour is invisible to them. That is the whole reason step 1 was done
alone and green first.

Three places will notice, and two of them should:

- **Anything around `Enable`.** A disabled control returns `dropped`
  today. After the flip that genuinely stops the walk, where before the
  walk continued. If a test expects a disabled control's property to
  still take a value, that test has been encoding the ambiguity and the
  new behaviour is the correct one. Read each failure as a question about
  what disabled ought to mean, and write the answer down - it is the
  first real use of refusal.
- **Anything that leans on a message reaching `Object`.** If a site that
  should have become `unhandled` was left as `dropped`, the default store
  stops happening. That is the fail-closed signal working, and the fix is
  the site, never the test.
- **Any assertion on a literal return value.** There should be very few;
  `!= rtrn_handled` survives the change, `== rtrn_dropped` may not.

What to ADD, and it belongs in the same day: a test that proves the
distinction exists rather than that behaviour is unchanged. One handler
that declines, so the default still stores. One that refuses, so it does
not. One class chain where the middle class has never heard of a message
and the outer one answers. Without those three, step 2 is only a rename,
and nothing downstream can rely on it.

The rule from the plan holds: this is core dispatch, so it gets its own
full harness run before anything is built on top of it. A broken dispatch
is indistinguishable from a broken everything.
