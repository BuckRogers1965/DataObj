# The First Lesson You Have To Unlearn

Every embedded tutorial starts the same way:

```c
digitalWrite(LED, HIGH);
delay(500);
digitalWrite(LED, LOW);
delay(500);
```

It works, it takes ten seconds to understand, and something blinks. It is
also the wrong model, and every lesson after it is spent working around
the idea it planted: that **time is something your program spends** rather
than something it schedules.

## Watch it break

**One LED.** Fine. Three lines.

**Two, at 500ms and 300ms.** The delays cannot coexist - the first one
blocks the second. So the student is taught the millis() pattern:

```c
if (now - lastA >= 500) { lastA = now; toggleA(); }
if (now - lastB >= 300) { lastB = now; toggleB(); }
```

Better, and genuinely a step forward. But notice what just happened: the
timing state became globals, and the dispatch became one shared function
that every device must be edited into.

**Four, at 6004, 7003, 8002 and 9001ms.** Now it is four near-identical
blocks and four `last` variables. The student is hand-writing a scheduler,
badly, without having been told that is what it is.

**Ten.** Unmaintainable, and the usual rescue is "you need an RTOS." So
they buy threads with per-task stacks, a couple of KB each, to get back
concurrency that a twelve-byte task entry already provides. On a part with
264KB of RAM, ten threads can cost a tenth of the machine to solve a
problem that was never about threads.

The progression looks like learning. It is mostly recovering.

## What the first example should be

```c
int blinkLED(struct _task_entry_type* task, int mesgid, int led) {
    digitalWrite(led, mesgid == 1 ? HIGH : LOW);
    AddTaskMilli(task, 500, &blinkLED, mesgid == 1 ? 2 : 1, led);
    return 0;
}
```

Read what that already does. One function handles **any number of LEDs at
any number of rates**, because the pin and the phase both ride in the
task's own arguments. There is no shared state to collide, no common tick
to derive, no if-ladder. A tenth LED is one more line at setup:

```c
AddTaskMilli(CreateTask(), 500, &blinkLED, 1, LED_BUILTIN);
```

Nothing existing changes. That is the property the delay version cannot
have at any level of skill, because its structure - not its style -
forbids it.

And here is the part that settles the argument: **the correct version is
shorter.** One line at setup against three in the loop, and it does not
grow when the second LED arrives. The only reason it is not taught first
is that a scheduler has to exist before you can call it, which is a
packaging problem, not a pedagogical one. Ship the scheduler with the kit
and the easy first lesson is also the true one.

## The lie gets expensive when there is a radio

`delay()` teaches that your code owns the CPU. The moment there is a
network stack on the chip, that is false - something has to service the
driver, answer the DHCP renewal, run the TCP retransmit timer. The patch
students are then given is `yield()`, sprinkled where things stall.

But `yield()` is not a new idea. It is an admission that the first lesson
was wrong: **you never owned the CPU, you had a turn.** A cooperative task
that re-arms itself is that truth expressed directly, instead of a
blocking model with escape hatches cut into it.

The same realisation arrives a third time when a sensor read blocks for
milliseconds and the web server stutters. On a dual-core part you can hide
it by putting the blocking work on the other core - which is what a
weather station running on this model does today, deliberately. But
noticing *why* that works requires already thinking in turns rather than
in delays.

## What the model is actually teaching

Three things fall out of "a task re-arms itself" that never fall out of
"delay and try again":

- **Cadence belongs to the thing that knows it.** Each sensor arms itself
  at its own interval, read from the item being polled. Nobody picks a
  global tick, and choosing 6004 / 7003 / 8002 / 9001 rather than round
  numbers keeps them from bunching onto the same wake. In the delay model,
  every interval is everyone's business.
- **Stopping is free.** A task that does not re-arm costs nothing. There
  is no disabled state to represent, no flag to check, no loop iteration
  spent skipping it. Idle is the absence of work rather than work that
  does nothing.
- **The loop is not yours.** `loop()` belongs to the framework, which
  pumps due tasks and returns. That is the same shape as an event loop, a
  GUI main loop, or a server accept loop - so the model transfers to every
  system the student will meet later, instead of being embedded-specific
  folklore.

That last one is why this matters beyond microcontrollers. The identical
scheduler runs a web canvas on a Linux box: `CreateTask`, `AddTaskMilli`,
callbacks that re-arm, a single-threaded pump. Three orders of magnitude
apart in memory, same twelve-byte task entry, same discipline. A student
taught delays has to unlearn them to write either one. A student taught
tasks has already written both.

## The honest objection

Blink-with-delay survives because it needs nothing. No library, no
concepts, no infrastructure - paste four lines and a light comes on. That
immediacy is a real teaching virtue and it should not be waved away.

The answer is not to make the first lesson harder. It is that the
scheduler is small enough - a list, a time comparison, a function pointer
and two arguments - to be part of the kit rather than a later topic. Then
the first example is *one line*, the second LED is another line, and the
tenth is the tenth line. Nothing is unlearned, and the student's
intuition, once formed, is correct at every scale they will ever work at.

Teach the turn, not the delay.
