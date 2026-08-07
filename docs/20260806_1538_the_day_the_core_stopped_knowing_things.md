# The day the core stopped knowing things

*6 August 2026*

`object.c` started this morning at 4,013 lines and ended at 2,010. Half of it
left. Nothing it does was lost — the palette still builds, sessions still save,
flows still replay — but the core no longer knows *how* any of that works.

The interesting part is not the deletion. It is what had to exist first before
deleting anything was possible.

---

## A palette with two of everything

The day started with a bug report I could not reproduce by reading: the palette
was laying out its controls, then leaving a 300-pixel gap, then laying out the
widgets. Nothing in `BuildPalette` had changed in five commits. `widget.c` was
byte-identical to the day before. `app.js` hadn't been touched.

Adding a debug line to the placement loop showed it immediately:

```
palette pass=0 slot=0  Button   at X=10  Y=10
...
palette pass=0 slot=15 VUMeter  at X=10  Y=340
palette pass=0 slot=16 Button   at X=90  Y=340    <- Button again
```

Every class was registered twice. `InstallObjects` scanned recursively down from
the working directory for `*.object`, and the test harness I'd built the night
before hardlinks all fifty modules into each variant's directory — inside the
repo. So a framework started from the project root found every module once in
`objects/` and again in each run directory, and `dlopen` treats a second pathname
as a second library.

Two fixes. Scan `objects/` rather than `.`, and — the one that matters — make
`RegisterClass` refuse a name that is already taken:

```
Error  class 'Button' is already registered - refusing the second one
```

A class name is the entire address space for creating one. Two claimants meant
`CreateObject`, the palette and every translator silently picked whichever the
walk hit first. Now it says so at every verbosity.

Worth noting what the bug *didn't* do: with sixty extra libraries loaded and
every class registered twice, the framework still booted instantly. Registration
is a few node inserts. Nothing would ever have told you by feel.

---

## Dependencies, and why a version is not a number

The framework already had dependency-ordered class loading — a `Dependencies`
property listing other libraries by name, swept repeatedly until everything that
could start had started. It worked, and it was about to stop being enough.

Three things were wrong with it:

**It named libraries, not classes.** A widget's layout table names *classes*:
`Textbox`, `LED`, `MoButton`. But a class cannot be looked up before its own
`ClassStart` has run — which is precisely the thing being ordered. So a
dependency has to name **both**: the file, which the loader can act on now, and
the class, which gets verified once that file is up. They differ more often than
you'd think. `objects/network/` builds `tcp.object`, which provides `TCPSocket`.

**It was a packed string in a 256-byte buffer.** Adding file plus class plus
version to a comma list means inventing a second separator and pushing that
buffer past overflow — and truncation there doesn't fail loudly, it silently
stops enforcing whatever fell off the end. It became a node list: one child per
entry, carrying `File`, `Class`, `Major`, `Minor`. No parsing, no separator, no
limit. Properties are nodes; this is what that is for.

**There was no version at all.** Now every class carries one, and the rule is
major equal, minor at least what was asked. Major and minor stay separate values,
because a version is a tuple and not a number: `"1.10"` sorts below `"1.9"` as a
string, and the framework's own automatic conversion turns it into 1.1 as a real.

The point of the version isn't tidiness. Every module declares `Object 1 0`
against the core, so a module built against an older core refuses to load into a
newer one instead of loading and misbehaving. Proved it both directions:

```
Registering class 'UDP'                                   <- asking for Object 1 0
Error  'UDP' needs 'Object' version 2.0, found 1.0        <- asking for Object 2 0
```

In the second case the class simply never starts. The old behaviour — a warning,
then load everything in registration order anyway — is gone. A module whose
dependency is missing or wrong is exactly what the gate exists to stop.

---

## Object, Control, Widget

With ordering and versions in place, the class tree could be real.

The framework had no formal class system, so behaviour that belonged to a *kind*
of thing had accumulated in `object.c` for want of anywhere else. There was no
Control class, so every control was its own lone object and anything common to
all of them went in the core. Object became the control class by default.

The tree we settled on:

```
Object                    the core provides this one, and it ends the chain
├── Control               a name, a place, a size; serialized
│   ├── View              a Control whose panel is a container
│   ├── Widget            a bag of Controls, and the behaviours driving them
│   └── Textbox, LED, Checkbox, Button, ...  the sixteen atoms
└── Script                what every language host has in common
    ├── Lua
    └── JSScript
```

Two clarifications from the author that changed what I was about to build. I had
Presentation as a layer between Object and Control; Control *is* the presentation
class. And I had View as a possible layer above Widget; "view is just a control
whose panel is a container."

`Control` and `Widget` are not in the core. They are loadable `.object` files
like everything else, which means fixing what is common to every control ships
one file rather than a new framework. The core provides exactly one class.

Two markers dissolved on contact with this. `ScriptHost=1`, a property each
language host set so the ScriptBox dropdown could find them, is now just "whose
parent is `Script`". And `Panel`, a flag set on any class that publishes from a
layout table, was only ever standing in for "Control or Widget".

---

## Fifty-five modules

Every module needed three declarations: what it is, what version, and every class
it uses. Most of that is mechanical, and the mechanical part should be derived
rather than typed — a widget's dependency list is exactly the set of classes its
layout table names, so a script reads the table and writes the list. It cannot
drift from what the widget actually builds.

What the script could *not* see was the interesting part: engines created in
code. `tcpshim` owns a `TCPSocket`, `bridge` creates `Alias` and `View`,
`tplink` creates a `TCP`. Those four went in by hand, found by diffing "classes
named anywhere in the source" against "classes declared".

The converter refused three modules rather than half-convert them — `queue`
(which registers both `Queue` and `Stack`), `mcpsource` (`MCPSource` plus the
`MCPAgent` view it generates), and `msg` (which registers no class at all). Each
got a hand conversion. A tool that stops when the shape is unfamiliar is worth
more than one that guesses.

---

## Reaching another module without linking to it

Then the actual work: moving code out.

A module must never link against another `.object` — that's the deployment
story, one file you can email. So how does a widget call `Widget_Publish` when
the implementation lives in `widget.object`?

The framework already answered this and I hadn't noticed: `ClassStart` and
`InstanceStart` are function pointers stored as properties on class nodes. So the
entry points get published the same way, and the header turns them back into
ordinary calls:

```c
static inline void Widget_Publish(NodeObj class, WidgetItem *table)
{
    void (*fn)(NodeObj, WidgetItem *) = ... WidgetEntry("Publish");

    if (fn)
        fn(class, table);
}
```

`WIDGET_IMPL`, defined only by the implementation, selects real prototypes
instead. Module source does not change at all — the call site still says
`Widget_Publish(...)`, and the version gate now protects that entry point.

One improvement the pattern needed: a wrapper whose class is absent used to
no-op silently. Every one of them now logs an error naming the class and entry
point it could not reach. A missing capability has to say so.

With that, five moves in an afternoon:

| moved | to | lines |
|---|---|---|
| four tests, and `KnownClasses` with them | `unit_test.c` | 324 |
| skins | `skin.object` | 118 |
| the flow interpreter | `flow.object` | 220 |
| import/export, and the JSON parser | `serializer.object` | 1,072 |
| presentation, the palette, the chrome | `control.object` | 390 |

The serializer one is the one I'd been looking forward to. Export was *already*
an object — a task-driven walker emitting JSON out its `Out` property — while import
was a hand-rolled parser in the core, `\uXXXX` escapes and all. One format, two
homes, which had to agree byte for byte with nothing enforcing it. Now
`libframework.so` exports zero of `ExportView`/`ImportView`/`LoadViewAsync` and
contains no JSON at all.

And `ExportView` itself turned out to be 51 lines of *composition* — create a
Serializer, create a Writer, wire them, activate both. That's a flow written in
C, and it is the best argument yet for the boot flow that's coming.

---

## Four ways to be wrong, all of them mine

**Brace counting doesn't work on a JSON parser.** My first extractor reported
`ImportNode` as 2,210 lines instead of 275, because the function is full of
string literals containing `{` and `}`. Writing a scanner that skips strings and
comments took ten minutes and would have saved an hour.

**Multi-line comments have plain-text continuation lines.** Walking up from a
function to grab its doc comment, I only caught lines starting with `*` — so the
last line of each comment came along and the opening stayed behind, unterminated,
swallowing the next one. The compiler said `"/*" within comment` seven times. I
reverted rather than patching over it.

**A clean build proves nothing about symbols.** Object modules link with raw
`ld -shared`, which permits undefined symbols and resolves them at `dlopen`.
Moving `PublishPosition` out of the core produced a completely clean `make` and
then:

```
./build/framework: symbol lookup error: .../dropdown.object: undefined symbol: PublishPosition
```

Eighteen modules needed `#include "control.h"`. And even after adding it, some
still failed — a new header isn't in the modules' `makedepend` output, so nothing
rebuilt. `nm -u` per module is the check that actually works.

**A variable cannot cross a module boundary.** `Widget_Adopted` is a flag set by
`Widget_Create` and read on the very next line by its caller. A class node can
publish a function pointer; it cannot publish a global. The flag became private
and the question — `Widget_WasAdopted()` — became the entry point.

---

## The second host earns its keep again

Last night's work built `unit_test`: a second executable on the same library,
whose whole purpose is to be a host that is not the app.

Today it caught a bug I would not have found otherwise. I had put
`RegisterCoreClasses()` — which registers `Object` — inside main.c's
`InstallObjects`. But `unit_test.c` is a *copy* of main.c and has its own
`InstallObjects`, which didn't have the call. Result: modules loaded, no `Object`
class existed, every module's dependency on it went unmet, and **not one class
started**. Silently.

The fix is the lesson. Registering the core's own classes is mechanism, not host
policy, so it moved into `ObjSetRegObjList` — the moment the core learns where
the registry is. Now any host gets it by handing over a registry, and no host has
to remember.

That is the second time in two days that writing a program which wants the
library for different reasons than the app has exposed something the app could
never see.

---

## The client stops guessing

One more, small and satisfying. `web/app.js` had this:

```js
const WIDGET_CLASSES = new Set(['Checkbox','Textbox','Slider','Knob','Label','LED',
  'TextOut','VUMeter','Button','MoButton','MenuButton','Dropdown','Markdown','HTML','Image']);
```

Fifteen names, and anything not in the set rendered as a panel. Write a new
control and it looked wrong until someone edited the client.

The `instance-created` event already carries the class's published interface, so
it now carries the class's **parent** too, and the client asks:

```js
if (classParent === 'Control') { ... }
```

Verified over the protocol: eighteen Controls, twenty-nine Widgets, classified
by descent. A control written tomorrow renders correctly today.

That's the fourth list to dissolve this week — `KnownClasses`, `Panel`,
`ScriptHost`, and now this one. They were all the same thing: a question the type
system couldn't answer, written down by hand somewhere it would go stale.

---

## Where it landed

```
object.c    4,013 -> 2,010 lines
core        registration, addressing, messaging, lifecycle, counters, interface
classes     56, in 55 loadable modules
```

Still to go: clone (485 lines) becomes export-then-import once the serializer
owns both directions, and `CreateDefaultApp` becomes a boot flow — after which
the host has no composition calls left in it at all.

The thing I keep coming back to is how little of this was *invention*. The
function-pointer-on-a-class-node trick was already how `ClassStart` worked. The
dependency sweep already existed. The version tuple matched what `version.h` had
been doing since 2009. Parent is one more property on a node.

Nearly everything today was noticing that a mechanism already in the building
answered a question I was about to write new code for.
