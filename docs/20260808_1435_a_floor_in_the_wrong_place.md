# A Floor in the Wrong Place

There is a line in the main loop that decides how long to sleep. It asks the
scheduler when the next task comes due and sleeps exactly that long — capped
at one millisecond.

The cap has a good reason. All I/O in this framework is polled, and a long
sleep is a hard floor on input latency. If the loop can sit idle for 50ms,
a slider being dragged in the browser updates at 20Hz and feels like mud.
One millisecond is invisible to a hand. So the cap went in, dragging felt
smooth, and nobody looked at it again.

Today five instances were sitting in `top`, one per build variant, and the
interesting number wasn't the memory:

```
RES 10.5–11.5MB    SHR ~5MB    CPU 0:02.8 over a five-minute run
```

Subtract the shared pages — `libframework.so`, libc, the loaded `.object`
files, counted once no matter how many processes are running — and the
marginal cost of the next instance is about 6MB. On a 16GB box that is
somewhere north of two thousand of them. An entire node tree, scheduler,
object registry and web bridge for the price of a couple of browser tabs.

But look at the CPU column. Those processes were idle. They had no
connections, no flows running, nothing to do, and they still burned nearly
three seconds of CPU each. That is the cap: a thousand wakeups a second,
every second, whether or not anything wants waking. At five instances it is
noise. At five hundred it is a quarter of a million wakeups a second spent
discovering that there is nothing to discover.

## The cap is not a design decision

The obvious framing is "1ms is a tuning parameter and idle instances should
use a bigger one." That framing is wrong, and it took a conversation to see
why.

Ask who actually knows the right interval.

The TCP object knows how often its sockets need looking at, because it knows
how many connections are in its ring and what they are doing. The bridge
knows whether a human is attached and dragging something, because it is
holding that session. A hardware-facing object knows its own sample rate,
because that is a property of the hardware. Every one of these has the
number.

The main loop has none of it. It cannot see connections, sessions or sample
rates. So any constant it picks is simultaneously too slow for the slider
and absurdly fast for the idle tenant. One millisecond was that constant. It
was never a decision about timing; it was a decision to guess on everyone's
behalf.

And the scheduler already has the correct answer sitting right there.
`SchedNextWakeMicros()` returns when the next task is due. If every object
that needs polling arms a task at its own honest interval, then the minimum
over all due tasks *is* the right sleep, exactly, with no policy anywhere.
The bridge arms a millisecond task while a session is attached and stops
re-arming when the last client detaches. The TCP object polls at whatever
its ring needs and schedules nothing at all when the ring is empty. Nobody
writes a check for "are we idle" — an idle instance has no tasks, so there
is nothing to take the minimum of, so the loop sleeps until something real
happens.

The fix, in other words, is a deletion. The core loses a constant and gains
nothing.

## What the constant was really hiding

Here is the part that made this worth writing down.

A cap on sleep means an object that never declares its cadence still gets
woken a thousand times a second. So it works. Its polling is serviced, its
sockets get read, its tests pass. There is no failure to trace back to the
object that skipped arming its task, because the core quietly covers for it.

That is the definition of hiding a symptom. The omission does not surface as
"this object forgot to schedule itself." It surfaces as CPU on an idle
process — which reads like a scheduler problem, or a platform problem, or
just the cost of doing business. The evidence has been laundered through the
one component that isn't at fault.

Worse, it is self-reinforcing. Once the core guarantees a 1ms wake, the next
polled object has no reason to arm a task either, because not doing so is
indistinguishable from doing so. The discipline erodes and the workaround
looks more load-bearing every time something new leans on it. Eventually you
cannot remove the cap without auditing every object, which is exactly the
position it puts you in now.

The general shape: **a constant in the core that makes a missing discipline
invisible will accumulate dependents until it looks like architecture.**

## The payoff is not the microseconds

Deleting the cap saves an idle process a thousand syscalls a second, which
is nice and not the point.

The point is what it does to the deployment story. A tenant costs 6MB
resident. If an idle tenant also costs approximately zero CPU, then leaving
one resident is cheaper than tearing it down and rebuilding its flow on the
next request, and a process per logged-in user becomes ordinary rather than
extravagant. That buys real isolation: one user's runaway script or segfault
cannot reach another user's canvas, because it is not in the same address
space. No session multiplexing, no tenant IDs threaded through the engine,
no shared-state bugs that only appear under concurrent load.

That isolation was already affordable on memory. It was the wakeup tax that
made hundreds of instances look expensive, and the tax was being levied by a
constant that no object asked for.

The instances that pay it should be exactly the ones with someone watching —
which is also the only place the millisecond ever bought anything.
