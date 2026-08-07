# Two languages, one vocabulary

*6 August 2026, evening*

Tonight a JavaScript agent ran on QuickJS, in a system that had been running Lua
a minute earlier, without a rebuild or a restart. The source was pasted into a
text box and the language was picked from a dropdown. The object it belongs to
does not know what a language is.

That last sentence is the whole post.

---

## The thing that was wrong

Two scripting hosts shipped in this framework — Lua and QuickJS — and they were
supposed to be interchangeable. They weren't, and nothing could have told you:

| | Lua | JSScript |
|---|---|---|
| `getprop` / `setprop` | yes | yes |
| `send` | yes | yes |
| `sibget` / `sibset` | **yes** | no |
| `print` (to a port) | no | **yes** |
| `cmd` (bridge) | no | **yes** |
| runaway guard | **none** | 500 ms |

A script written for one host would not run on the other. Worse, the ScriptBox
widget wired itself to the host's `Print` property — which Lua did not have — so
Lua's `print()` had been going to the server's stdout and the Output box had
been empty since the day it was written. Nobody noticed because nobody had a
reason to compare them.

Two implementations of one role, no written contract, already drifted. That is
what a shared class is *for*, and there wasn't one.

There was also an "Inner": ScriptBox created its host as a named, path-
registered child instance and reached into its properties. So did the MCP agent
generator, with a child called "Runner". ASAN had already caught the
consequence — the language swap deleted the child and then read a node
belonging to it, a use-after-free in a build nobody runs instrumented.

---

## What replaced it

**A host is opaque.** No properties, no ports, no name, no path. Its entire
interface is a header:

```c
ScriptSetSource(h, text)   ScriptRun(h)   ScriptIn(h, data)
ScriptStop(h)              ScriptBudget(h, ms)

SCRIPT_PRINT · SCRIPT_OUT · SCRIPT_ERROR      /* base + ordinal, to the owner */
```

A driver creates one through that class's own `InstanceStart`, hands over
`{Owner, MsgBase, Port}` — the owner picks the base, so one owner can hold
several hosts and tell their answers apart — and from then on sends messages.
Nothing in the session can find the host, wire it, save it, or reach into it.
The use-after-free class does not exist rather than having been fixed.

**Everything a script can do lives in one table.**

```c
typedef struct ScriptVerb {
    char    *name;
    int      argc;             /* DataObj arguments */
    int      takesCallback;    /* trailing arg is a function in the script */
    DataObj (*fn)(NodeObj self, DataObj *argv, long cbHandle);
} ScriptVerb;
```

Fifteen verbs, implemented once in C: `getprop`, `setprop`, `sibget`, `sibset`,
`pathget`, `pathset`, `send`, `print`, `log`, `create`, `destroy`, `activate`,
`connect`, `disconnect`, `exists`. They are the *engine's* verbs — the same
calls the bridge translates and the flow interpreter replays — not a vocabulary
invented for scripting. A script is a translator like they are.

A host never names a verb. It walks the table and registers every entry through
one trampoline. Add a verb to the table and every language has it that day,
with no host touched. That is the property that makes the drift above
impossible rather than merely fixed.

Arguments and results are DataObjs, so a script handing a number where a string
is wanted simply works. Conversion is the data object's job — not the binding's,
and not the script author's.

**`connect` is one verb with two forms**, because the engine already worked this
way:

```lua
connect("/Root/View_1/Slider", "Value", function(v) sibset("Readout", v) end)
connect("/Root/A", "Out", "/Root/B", "In")
```

`Connect` records `{Instance, Port, Callback}`, and a callback is exactly what a
script function is. So there is no `subscribe` — and nothing new in the engine.
The host hands over an opaque handle (a `luaL_ref` in Lua, a duplicated
`JSValue` in QuickJS); the shared layer stores it with the subscription and
hands it straight back. Neither side needs to know what the other's handle is.

**The guard is shared.** Lua had none. It has one now, and it uses real
wall-clock rather than the framework's cached time — deliberately, because the
case it exists for is a script that never yields, which also never lets the main
loop refresh that cache.

The hosts came out much smaller: Lua 475 → 393 lines and seven own bindings → one;
QuickJS 504 → 416 and eight → two. What is left in each is: make a context, walk
the table, marshal, run source. The two survivors in each are `oninput` and
`onevent`, which are lifecycle, not verbs.

---

## Where the source lives, and why that is the interesting part

If the host is unaddressable, it is also unserializable. So what happens to the
code when you save?

It was never the host's. The **widget** owns the source — a ScriptBox's `Source`
property, an MCP agent's — and the host is rebuilt from it on demand. That one
decision is why everything else works:

- a **language swap** keeps the code, because the code was never in the host
- a **clone** comes up working, because the widget is what gets cloned
- a **save/load** round-trips, because the widget is what gets serialized
- and the host can be thrown away and rebuilt at any moment with no ceremony

The agent generator used to keep its logic in a child instance called "Runner",
deliberately left clonable with a comment explaining that it must survive a
clone. It does not exist any more. The logic is a property, and properties
survive everything by default.

---

## Three bugs that fell out on the way

**Clone and export disagreed about what data is.** `CloneObject` copied the
properties in the class's *published Interface*; the serializer walked the
instance's *actual* properties. Those are different sets, so a property added to
one instance survived export/import and was silently dropped by clone. An agent
cloned from the palette came back with no logic at all. The core now has one
predicate for "is this portable data" — skip runtime pointers, skip stale
shadows — and clone asks it.

**The Options panel had the same split.** It built its rows from the class
Interface too, so a property added to an instance was invisible. Which meant the
new `Language` property existed, mattered, and could not be seen or changed —
making the language a one-time decision. It walks the instance now, and consults
the interface only for how a row should draw.

**A fallback was hiding a missing fact.** The agent had no `Language` property at
all; the host code defaulted to Lua. That was *correct* — the generator only
emits Lua — and it was still wrong, because being right by coincidence is not
being right, and the moment the source became editable it would have handed
JavaScript to a Lua interpreter. The generator now states the language where it
is known, and the host refuses to guess. The first attempt at this got caught
exactly as it deserved: I removed the guess without making the language visible
or settable, which locked every agent to Lua forever and made QuickJS
untestable.

---

## What it means

Nothing in that chain is MCP-specific. `Source` and `Language` are ordinary
properties. The panel renders whatever an instance carries. The host is created
by any owner that can hand over a callback address. Which means "add a script to
a Reader, or a TCP, or a Pulse, and edit it in that object's Options panel" is
now one stamping function away — not a feature to build, an application of
things that already work.

And a fifth hand-maintained list dissolved tonight: `ScriptHost=1`, the marker
each language host set so the dropdown could find it. Which classes are language
hosts is now a question about the class tree — whose parent is `Script` — and
the answer maintains itself.
