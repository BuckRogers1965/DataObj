# A core you can change daily

*Three weeks of one major change a day, and the milestone that ends them, 17 August 2026.*

Since 27 July this framework has taken at least one core mechanism change
per day, most days two. Not features on top of a core - the core itself,
the parts a normal project freezes first. Here is the list of things that
were load-bearing when they were deleted:

- directions, and In/Out ports as a species of thing
- In/Out on every control (three copies of one value, stepping on each other)
- load and save as recorded update replay
- the Alias object
- function-pointer fan-out
- the fabricated `/Root` prefix
- the bridge's private alias table
- the `PropertyBinding` and `ActivateBinding` adapters
- `Inner` and `runner` in the script hosts
- the WatchableProp gate
- 23 hand-rolled registry scans
- the deferred panel build, its `PanelBuilt` flag, its per-widget build
  task, and clone's separate creation path
- silent name lookups that could not tell a probe from a fault

On the far side of every one of them, the program did the same thing it did
before. That is the claim, and the test matrix is what makes it a claim
rather than a hope: twelve suites across five build variants, sixty engine
runs, taken to green before each of those went in.

## The number that says the most

`src/node.c` was 1455 lines on 6 August. It is 1467 lines today.

**Twelve lines.**

In that same window the registry became a class tree, unhandled messages
grew a ladder that punts up the class chain to Object, addressing moved
onto a namespace trie, aliasing became an engine verb, clone collapsed into
the one creation path, and every widget in the system changed how it comes
into existence.

The deepest data structure in the framework absorbed all of that without
moving, because it does not know anything. A node has a name, a value, a
property list, children and siblings. No mechanism is spelled into it, so
no mechanism change can reach it.

Other numbers from the same three weeks: `web/app.js` went 2478 -> 1778
lines while gaining features. Single commits deleted 2072 lines from
`object.c` and 877 from `bridge.c`. Yesterday's widget rework touched 30
widgets plus `widget.c` and `widget.h` for a net **+484 / -348** in code -
removing a mechanism from every widget in the system cost about a hundred
lines, and most of the addition was the assertion API that replaced it.

## Why the pace was possible

In most codebases a core mechanism is a *shape* that every call site has
been compiled against. Change it and you are editing every consumer, and
the consumers are where the risk lives. That is the real reason cores
freeze: not that they are hard to write, but that they are expensive to
change.

Here a mechanism has one implementation site and no type surface. Nothing
downstream is written in its terms, because everything downstream is
written in nodes and messages. `Connect()` does not know what it connected.
`DispatchMsg` does not know what it delivered. A widget does not know how
it got built. So a mechanism can be replaced underneath all of them.

That is also what makes "did the behavior change" a *decidable* question.
An application here is objects plus wiring, and the harness asserts over
the JSON protocol on what the system observably does. So "does it still
work" is answerable independently of how it works - which is the only
reason a core change a day is survivable rather than reckless.

## The other half of the pace

None of it would have been possible without something that could tell,
instantly and without argument, that behavior had moved. That is the
harness, and it is the reason this is an achievement rather than a story
about a lucky streak.

Twelve suites. Five build variants. Sixty engine runs in a full pass, plus
the library's own `unit_test`. Around 6900 lines of Python driving 81 test
functions against a real running framework over the real protocol - nothing
is stubbed, nothing is mocked, every suite starts an engine and talks to it
the way the browser does.

The five variants are five different opinions about the same source:

    debug     -O0 -g3 -fno-omit-frame-pointer -fno-inline
    release   -O3 -march=native -flto=auto
    asan      -fsanitize=address
    ubsan     -fsanitize=undefined -fno-sanitize-recover=all
    gcov      --coverage

Each gets its own build directory, its own framework, and its own port
offset, so two variants can never quietly measure each other's engine. The
suites run simplest-first and the run stops on the first failure, because a
broken `connectiontest` makes every later suite unreliable signal for the
same root cause. Core dumps land in the variant's own directory next to its
logs.

What "the slightest change in behavior" actually means in practice:

- **`leaktest`** reads the core's own alive-counters - `NodeCount`,
  `DataCount`, `EnvelopeCount`, `TaskStructCount`, `BuffCount`,
  `QueueCount` - through the Stats object and asserts that a create/destroy
  cycle nets **exactly zero**, and that a 50-message burst costs exactly its
  one activate log record. Not "no big leaks." Zero. A counter that grows by
  one is a behavior change, and it is named by its type.
- **`flowdiff`** builds a thing, exports it, imports it, exports it again,
  and structurally compares the two dataflows. That is what caught
  `Port.AccumulateRx: '1' -> ''` - a live link overwritten with a dead
  string on import, in a suite set that was otherwise entirely green.
- **`guitest`** drives an actual browser over CDP. The projection is tested
  as a projection: click the real control, assert the engine moved.
- **`asan`** caught a use-after-free in `RegisterPath` that passed in four
  variants out of five, because a freed block still holds its bytes right up
  until something else wants them.

That is the machine that made a core change a day safe. A mechanism could
be deleted in the morning and the answer to "does the program still do what
it did" arrived the same afternoon, in the form of sixty runs and a report
file, instead of a week of squinting at it by hand. A week of hand-checking
per change is arithmetically the same as not making the change - which is
exactly why most cores freeze.

And the one place the harness could not see is the one place a bug lived
for weeks. Widget help was never testable: the runner staged
`objects/*/*.object` flat into each variant directory, and a widget's help
lives at `objects/<name>/README.md`, read relative to the cwd. The file
simply was not there under test. The blind spot and the surviving bug were
the same square inch. That is not an argument against the harness; it is the
sharpest possible argument for it, and staging the READMEs closed both at
once.

## Why every change made it smaller

None of these were additions dressed up as refactors. Each was a
recognition, and the post titles are the record of it: *an alias is a
redirect*, *aliasing is a verb*, *classing is message dispatch*, *one kind
of thing*, *a thing is built in a location*. Every one of them found that
some invented species was an existing mechanism used slightly differently,
and deleted the species.

That compounds. The count of distinct kinds of thing went down monotonically
for three weeks, so each change had fewer cases to satisfy than the one
before it. Yesterday's fix reached 30 widgets and stayed tractable
*because* alias had already collapsed, clone had already collapsed, and the
registry walk had already become one API. Three weeks ago the same fix
would have been a month.

## The milestone

Yesterday's change was the last one of that kind that was owed.

A widget's constructor is now handed its place and its name, so it builds
its own panel at creation, in one call, from a table:

    RegisterInstance(class, instance);
    Widget_Place(instance, data, TCPPortPanel);

There is no build flag, no deferred task, no second entry point, and no
separate path for a clone, a load or an import. `Widget_Place` reads the
container and name off the place node, registers the path, and walks the
table. Everything a widget needs from the framework is seven calls in
`widget.h` - `Widget_Place`, `Widget_Port`, `Widget_Ctl`, `Widget_SubPanel`,
`Widget_Reflect`, `Widget_AddHelp`, `WidgetCtl` - and 29 of the 61 object
modules already include it.

That is the milestone: **the remaining work is no longer core work.**

## The new cadence

So the pace changes, deliberately, and it changes to something boring:

**One widget's `Activate` a day.** Every widget still carries init work in
`Activate` that predates constructors having a place and a name - that work
belongs in the constructor now, and only the *action* stays in `Activate`.
That is a per-widget judgement call, one at a time, with the harness green
in between. No core involvement.

**A new widget a day.** A widget is a table, a handful of port declarations,
a README, and its actual logic. Nothing in the core has to learn about it.
It clones, saves, exports, imports and shows its help because the framework
already does those things for everything.

**A new I/O object a week.** These are the slower ones, and they are slower
for honest reasons - a protocol takes as long as it takes. TCP client mode,
serial, the async DNS finally joining the build. The pattern is settled:
send a request carrying your callback, catch the reply as a message, and
keep the properties for configuration and state.

None of that touches `object.c`, `node.c`, `sched.c` or `bridge.c`. That is
what three weeks bought - not a finished framework, but one where the
interesting work has moved out to the edges and the middle can be left
alone.

## The part that is actually novel

The problem domain is systems whose behavior has to change after they ship.
The usual answer is a fixed core with plugins, configuration and scripting
layered on top: three separate extension mechanisms, each with its own
vocabulary, wrapped around the one part you are not allowed to touch.

This says there is no boundary. The registry, the wiring, the
configuration, the UI skin and the function pointers are one structure, so
a change to the core is a change to *data* - and data changes do not carry
the blast radius that interface changes do.

The corollary is the thing nobody designs for on purpose: **the rate at
which a core can change is itself a property of the design.** This one made
that rate high and then kept it high, because every change that landed
removed a reason the next one would have been hard.

Three weeks. One to two core mechanism changes a day. No behavior
regression survived to a commit. And the engine has fewer kinds of thing in
it than it started with.
