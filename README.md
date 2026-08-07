# GrokThink

**A dataflow framework where the application is data, not code.**

The executable is a hollow host: 21 KB, and it does nothing on its own. Every
capability — the widgets, the network objects, the scripting hosts, the code that
saves your work, even the class that defines what it means to be a *control* —
is a separate file discovered on disk at startup. An application here is a set of
objects plus their wiring. Shipping a different product means shipping different
objects, never a different binary.

Four things it is built on are described below.

---

## Three rules, and what they actually buy you

**Everything is a node.** One tree holds the registry, the configuration, the
wiring, and the widget layout. Properties are nodes too, so anything can be
annotated with anything.

**Everything is a message.** Objects interact only by messages routed through
subscriptions on nodes — in-process pointer passing, queued through the
scheduler. No copies, no serialization, no thread handoffs, and no downstream
work nesting inside the sender's call stack.

**There are no ports.** This is the one that matters. There is no port type, no
in/out direction, no sink, no distinction between a "compiled port" and a plain
property. `In`, `Out`, `Enable`, `Clock` are just *names* objects happen to give
properties. Any node can be subscribed to.

Delete the concept of a port and a surprising amount of software stops needing to
be written:

| you want | what you do | code required |
|---|---|---|
| tap a live connection to see what's flowing | subscribe a second consumer to the same property | none |
| shut an object down from a timer | wire a Pulse to its `Enable` | none |
| a rate limiter | wire a Pulse to a Queue's `Clock` | none |
| a server that runs for 30 seconds | `Pulse(Interval=15000, Count=1)` → `TCP.Enable` | none |
| a live leak readout | wire a TextOut to the Stats object's `Nodes` | none |
| quit the process | write `0` to `/Main`'s `State` property | none |

That last one is worth dwelling on. The main loop has run "while State is
non-zero" since 2003. Because `/Main` is an ordinary path and `State` is an
ordinary property, that twenty-three-year-old line became a remote shutdown
command the day the JSON bridge existed — with nobody implementing a quit verb.
Mechanisms compose here because there is only one kind of thing to compose.

**And nobody calls exit.** When the last object stops scheduling work, the task
list empties and the process ends. Shutdown is emergent, not coded.

---

## Why you might want it

**You can rewire a running system.** Open the browser canvas, drag objects out of
the palette, wire them by clicking, and drive them through live instrument
panels — sliders, LEDs, VU meters, text boxes. Every drag and every value change
is a real message through the same object graph the C side runs on. There is no
client-side simulation and no build step; the thing you are looking at is the
thing that is running.

**A fix is one file.** Modules average 30 KB, are isolated behind the message
fabric, and carry their own `UUID` and `Company`. Support has meant emailing a
single `.object` — drop it in the scan path, restart, done. No module ever links
against another, so a replaced object cannot break its neighbours.

**It embeds in five calls.** `ObjSetRegObjList`, `ObjSetTaskList`,
`InstallObjects`, then pump `TimeUpdate`/`ExecTasks` from whatever event loop you
already have. Anything that can `dlopen` a file and call five functions gets the
whole object system — another C program, a GUI app, a plugin host, a language
runtime through FFI. `main.c` is best read as the reference host, not the program.

**Types never need negotiating.** A DataObj converts on every read, so a producer
that emits a string and a consumer that wants an integer simply work. This is
also why an MCP server's schema could be translated into palette classes in a
day: both sides are stringly-typed, so there was no impedance layer to write.

**Scripts are peers, not plugins.** A Lua or JavaScript host is just another
object that speaks the same JSON protocol the browser does. A script can create
instances, wire them, subscribe to events — everything the GUI can do, because it
is using the same interface.

**It watches itself.** Allocation counters at every choke point are published as
ordinary properties by the Stats object, so a text box wired to `Nodes` is a live
leak detector. The fabric is its own instrumentation.

---

## What's actually here

- **A 2,000-line core.** Registration, addressing, messaging, lifecycle,
  allocation counting, interface publication. That is all of it. Presentation,
  serialization, flow interpretation and the palette are loadable objects.
- **56 classes in 55 modules** — controls, instrument panels, dataflow pieces,
  TCP/UDP, HTTP/WebSocket, a JSON bridge, Lua and QuickJS hosts, and integrations
  with Ollama, ComfyUI, Stable Diffusion and MCP.
- **A real class system.** `Object → Control → Widget`, declared per module with
  versions, and dependency-ordered at load. A module built against an older core
  refuses to start rather than misbehaving.
- **A browser canvas** with save/load of the whole session to a flow file.
- **A test harness** that builds the same source five ways — debug, release,
  ASAN, UBSAN, gcov — and runs every suite against all five in parallel.

---

## Where to look next

| | |
|---|---|
| [`objects/README.md`](objects/README.md) | the class tree, versioning, and why everything is optional |
| [`objects/skeleton/`](objects/skeleton/README.md) | write your own — three templates, one per kind |
| [`docs/MANUAL.md`](docs/MANUAL.md) | using the canvas |
| [`docs/readmefirst.md`](docs/readmefirst.md) | read before touching the GUI code |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | where this is going |
| [`CLAUDE.md`](CLAUDE.md) | the full architecture writeup |

---

## The four inventions

Each one exists because the obvious alternative does not hold up:

1. **The node tree** — one uniform structure for registry, config, wiring and
   skins, instead of four separate mechanisms.
2. **The intelligent data object** — automatic type conversion at every read, so
   producers and consumers never negotiate.
3. **The subscriber model** — fan-out routing instead of a single owner callback.
   This is what makes probes, taps and Enable lines free.
4. **Non-blocking DNS** — a worker thread quarantines the blocking resolver
   behind a sentinel the main loop polls, keeping the fabric single-threaded.
   (Written, not yet wired into the build.)

---

## Operating it

### Build

```
make depend          # once, or after adding a source file
make                 # libframework.so, framework, unit_test, and every object
make clean           # empties build/ and every module's .o
make debug           # the same build with -O0 -g3, no inlining (a readable backtrace)
```

Everything lands in `build/`: the core objects, `libframework.so`, the
`framework` executable, and `unit_test`. Each module builds in its own directory
to `<name>.object`, which is where the loader finds it.

To build one module on its own — the usual loop when writing one:

```
make -C objects/textbox
```

### Run

```
./framework.sh                       # the usual way
./framework.sh -h                    # options
```

`framework.sh` sets `LD_LIBRARY_PATH` for you and stops whatever framework is
already holding the port this one wants — only that one, so a test run's
instances on their own ports are unaffected. The canvas is then at
`http://localhost:8083`.

Note that the port option is `-port 1234`. `-p` is a different flag entirely —
print the node tree on exit — and takes no argument.

| option | |
|---|---|
| `-ip <address>` | what to bind the web GUI to. Default `0.0.0.0` — reachable from the LAN. `127.0.0.1` for this machine only. |
| `-port <n>` | web GUI port, default 8083 |
| `-d` | daemonize |
| `-l <logfile>` | write the debug log to a file |
| `-p` | print the node tree on exit |
| `-t` | run the core's own self-tests |
| `-v <0-9>` | verbosity. 1 is the default; 2 adds registration and command-line detail; 3 adds the wiring, placement and import traces. |

The framework must be started from the project root, because it scans `objects/`
for modules and serves the client out of `web/`.

There is also a raw JSON control protocol for driving the same object graph
without a browser — the harness composes one at test time (`ensure_raw_bridge`
in `testharness/rawtest.py`), and a script or an agent uses the identical
interface.

### Stopping it

Write `0` to Main's `State` and it winds down the way it does when a flow
finishes — closing files and sockets, then exiting when the task list empties:

```json
{"cmd":"set-property","instance":"/Main","prop":"State","value":"0"}
```

That matters beyond tidiness: a signal kills the process before LeakSanitizer's
exit-time check can run, so an instrumented build only reports leaks if you ask
it to leave rather than killing it.

### Testing

```
./build/unit_test                # the core's tests, from a host that is not the app
./build/unit_test -t             # the same tests as libframework itself runs them
./testharness/run.sh             # everything, against five builds at once
```

`run.sh` builds the same source five ways — **debug, release, ASAN, UBSAN,
gcov** — and runs every suite against all five in parallel, each with its own
build, its own framework on its own ports, its own browser, and its own logs. It
never touches your desktop instance on 8083.

```
./testharness/run.sh -v                          # verbose suites
VARIANTS=debug ./testharness/run.sh              # one variant
SUITES="unit_test rawtest" ./testharness/run.sh  # a few suites
WEB_BASE=8500 ./testharness/run.sh               # somewhere else entirely
```

Results land in `testharness/tests/<timestamp>/`:

```
report.txt              the summary table - suites down the side, builds across
log/                    the shared build log
<variant>/build/        that variant's .o, .so and executables
<variant>/log/          one log per suite, plus the framework's own
<variant>/saved/        the live session, serialized - at the end, and on each failure
```

The report distinguishes three outcomes, because they mean different things: a
**count** is that many failed assertions, **CRASH** means the suite never got to
measure anything (and that variant stops there rather than reporting the same
corpse eleven more times), and **LEAK** means it passed but LeakSanitizer found
something at exit. The exit status is failures plus crashes plus leaks.

```
suite               debug    release  asan     ubsan    gcov
unit_test           0        0        LEAK     0        0
rawtest             0        0        0        0        0
tcpporttest         12       CRASH    0        0        12
failures            12       0        0        0        12
crashed             0        1        0        0        0
leaked              0        0        1        0        0
```

### Writing an object

```
objects/skeleton/newwidget.sh Counter           # a widget
objects/skeleton/newwidget.sh Gauge control     # one thing on screen
objects/skeleton/newwidget.sh Resolver object   # plain function, no panel
make -C objects/counter
```

Restart, and it is in the palette. Read
[`objects/skeleton/README.md`](objects/skeleton/README.md) first.
