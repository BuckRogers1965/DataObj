# Pressing Run twice on a ScriptBox wedges the engine

**Status:** open, not fixed. Found 2026-08-07 while rewriting `testharness/jstest.py`.
**Severity:** a client - or a user with a mouse - can spin the engine to
gigabytes with ordinary protocol commands. No crash, no sanitizer report, no
error in the log. It just stops answering and grows.

## How to trigger it

Two activates on a ScriptBox whose inner host already exists. Over the raw
protocol:

```json
{"cmd":"create-instance","class":"ScriptBox","as":"/Root/X/Box","container":"/Root/X"}
{"cmd":"set-property","instance":"/Root/X/Box","prop":"Language","value":"JSScript"}
{"cmd":"set-property","instance":"/Root/X/Box","prop":"Source","value":"print('hi');"}
{"cmd":"activate","instance":"/Root/X/Box"}
{"cmd":"activate","instance":"/Root/X/Box"}
```

From the GUI it is simpler than that: **open a ScriptBox panel and press Run
twice.** The Run button is wired straight to `Activate`
(`Widget_Ctl`: `Connect(c, "Value", target, "Activate")`), so the second press
is the second activate.

Setting `Language` is what makes it reachable, because that is what builds the
inner host - see below. A box that has never had a host takes its first
activate as the quiet deferred build, so the trigger needs the language set
first, which every real ScriptBox has.

## What it looks like

Nothing, for a while. Then:

| elapsed | framework RSS |
|---|---|
| 12 s | 11 MB |
| 68 s | 178 MB |
| 90 s | 251 MB |
| ~25 min | 3.5 GB |

100% CPU throughout. The process never dies on its own and never logs another
line - the loop runs entirely through paths that carry no `DebugPrint`, so even
`-v 3` shows only the panel build and then silence. Under ASAN it produces **no
report at all**, because nothing is corrupt: this is a live, well-formed message
loop, not a memory error.

Five of these at once (one per harness variant) took a 64 GB machine to 846 MB
free with swap exhausted.

## Where it is

A stack from the spinning process (release build, `gdb -p`):

```
#3  DeliverToSubscriber ()
#4  SetPropStr ()
#5  DispatchMsg ()
#6  ExecTasks ()
#7  main ()
```

Shallow, not recursive - so it is not runaway nesting. It is an endless stream
of *queued* messages: `ExecTasks` dispatches one, delivery writes a property,
that write fans out and queues the next. Every hop is a **changed** value, so
`SetPropStr`'s change gate (node.c:702, the thing that makes two-way control
bindings self-terminating) never fires. The memory growth is the message
traffic itself, which is why it creeps instead of exploding.

## Why the second activate is different from the first

`ScriptBox_Activate` (scriptbox.c) decides what to do from whether the inner
host exists:

```c
firstCall = !local->host;
Widget_BuildOnce(instance, ScriptBoxPanel);
if (!local->host)
        ScriptBox_SwapHost(instance, GetPropStr(instance, "Language"));
if (firstCall || !local->host)
        return rtrn_handled;            /* came up quiet, did not run */
local->active = 1;
SetPropInt(instance, "State", Running);
SetPropStr(instance, "Output", "");
... ScriptSetSource(local->host, text); ScriptRun(local->host);
```

Setting `Language` calls `ScriptBox_OnLanguage` -> `ScriptBox_SwapHost`, which
builds the host **before any activate arrives**. So `firstCall` is already false
on the first Run, and that first Run executes the script - correctly, and the
panel gets built in the same call.

The second Run then re-runs the source into a context that is still live from
the first: `Widget_BuildOnce` is a no-op now, the panel controls exist and are
two-way bound, and `SetPropStr(instance, "Output", "")` is a write into that
live binding rather than a write that happens before the wiring exists. The
previous run's registered handlers (`oninput`, and whatever else the source
installed) are also still attached to the old context.

The precise hop-by-hop cycle has not been traced yet. What is established:

- one activate is always clean (10 MB, script runs, `Output` reads back)
- two activates always wedge, in every variant, at the same point
- it happens with a script that only calls `print()` - no input, no wiring of
  our own, no subscription needed to start it
- `testharness/scriptboxtest.py` has always activated exactly once, which is
  why this sat undiscovered

## The obvious fix does not work

"Ignore an activate while the box is already running" cannot be built on the
flag that exists. `local->active` is set to 1 at scriptbox.c:295 and cleared
only at `InstanceStart` (:343) - nothing else ever touches it. Gating on it
would not eat *a Run while running*, it would eat **every Run after the first**
for the life of the instance: edit the Source, press Run, nothing happens.

A re-entrancy guard (set a flag around the run, clear it after) would not fire
either. The two activates are separate protocol commands, and the first has
fully completed - with one activate the script runs to completion and `Output`
reads back. The box is *idle* when the second Run arrives. This is not a
concurrency bug.

## Candidate fixes

1. **Run = restart.** Tear the inner host down and bring it back up before
   running, instead of re-running source into a context that already has the
   previous run's handlers registered. That is `ScriptBox_SwapHost` with the
   same language - a path already proven, because it is exactly what the
   Language dropdown does. Keeps Run pressable repeatedly, which is the point
   of the button.
2. **Run = ignore while live.** One-shot per instance; re-running then needs a
   Stop, which does not exist yet.

Both are confined to `objects/scriptbox/scriptbox.c`.

Worth noting either way: whatever the cycle turns out to be, a property write
landing in a two-way control binding should not be able to produce unbounded
queued traffic. The change gate is what normally stops that, and something here
defeats it by making every hop a real change.

## What was done instead, for now

The three test suites that were driving the double activate were changed to
activate once, matching the shape `scriptboxtest.py` always used - `js_box` in
`jstest.py`, `build_widget` in `widgettest.py`, `press_run` in
`scriptedwidgettest.py`. With that, jstest 4/4, scriptboxtest 5/5, widgettest
4/4, scriptedwidgettest 8/8, all with the server at 10-13 MB.

The tests no longer trigger it. The engine can still be made to.
