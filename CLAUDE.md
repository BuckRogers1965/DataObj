# CLAUDE.md

This is a C framework for a node-based dataflow object system ("GrokThink framework").
Loadable object modules are discovered on disk at startup, loaded with `dlopen`, and
register themselves into a live node tree. Everything — metadata, state, wiring, even
function pointers — is stored as properties on nodes.

Provenance: this is a ground-up rewrite of VNOS (Singlestep Technologies,
early 2000s) — `objects/network/TCPObject.c` is kept as the reference
implementation from that system. Four pieces are new inventions, lessons from
problems the original never solved: the **node tree** (one uniform structure
for registry, config, wiring, and skins), the **intelligent data object**
(automatic type conversion at every read, so producers and consumers never
negotiate types), the **subscriber model** (fan-out routing instead of VNOS's
single owner-callback, which is what makes probes, taps, and Enable control
lines free), and the **non-blocking DNS** (`async-dns/` — a worker thread
quarantines the blocking resolver behind a sentinel flag polled from the main
loop, keeping the fabric single-threaded; not yet wired into the build, it
joins when TCP client mode does).

Three governing principles:
- **Everything is a node**: one tree structure holds the registry, configuration,
  and wiring; properties are nodes, so anything can be annotated.
- **Everything is a message**: objects interact only by messages routed through
  subscriptions between ports. Delivery is in-process pointer-passing (function
  pointer call with a node handle — no copies, no serialization, no thread
  handoffs) **queued through the scheduler**: `SndMsg` costs one task insert,
  and each hop is dispatched breadth-first from `ExecTasks`, so downstream work
  never nests inside the sender's call stack and the decoupling is essentially
  free.
- **The app is an empty view**: the executable is a hollow host; the objects ARE
  the functionality, and an *application* is nothing but a set of objects plus
  their wiring (eventually a flow file — `CreateDefaultApp()` hard-coding is interim
  scaffolding). Shipping a different product means shipping different objects and
  a different flow, never a different binary. Corollary for all future work:
  features never go in main.c or the host — if a feature is tempting there, it's
  an object.

## Build

```
make depend
make          # builds libframework.so, the `framework` executable, and all objects/*
make clean
```

Run with:

```
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:.
./framework          # or ./framework.sh
```

CLI options (parsed in `main.c:ProcessCmdLine`): `-h` help, `-d` daemonize,
`-ip <address>` web GUI bind address (127.0.0.1 local-only, default 0.0.0.0),
`-l <logfile>`, `-p` print node tree on exit, `-port <n>` web GUI port (default
8083), `-t` run unit tests, `-v <0-9>` verbose level. The ip/port land as `ip`/
`port` properties on Main; CreateDefaultApp feeds them to the web flow's TCP as
`LocalAddr`/`LocalPort`.

### Build structure — two sides, one library

- **Core library**: all root-level modules (`data.c`, `node.c`, `object.c`, `sched.c`,
  `libload.c`, `dirscan.c`, `list.c`, `timer.c`, `DebugPrint.c`, `deamon.c`, `dyn/*`) are
  compiled into `libframework.so`.
- **Executable**: `main.c` is a deliberately tiny program linked against the library.
  The intent (documented in main.c's header comment) is that many different small
  executables can define different *behavior* on top of the same library *function*.
  More than that: **the library is an embeddable core, by design**. The entire
  hosting contract is five calls — `ObjSetRegObjList()`, `ObjSetTaskList()`,
  `InstallObjects()`, then pump `TimeUpdate()`/`ExecTasks()` from any event loop
  the host already has. Anything that can load a .so and call five functions —
  another C program, a GUI app, a plugin host, a language runtime through FFI —
  gains the whole object system, and loaded `.object` modules bind to the same
  shared fabric because the registry and task-list globals live inside the
  library itself. main.c is best read as the reference host, not the program.
- **Object modules**: each subdirectory of `objects/` with a `Makefile` builds a
  `<name>.object` shared library, also linked against `libframework.so` — this keeps
  modules in the 10–20 KB range. **That size is a deployment feature, by design**:
  in the VNOS era customer support meant emailing a single object file — drop it in
  the scan path and the fix is installed. Objects are isolated behind the message
  fabric so a replaced object can't break its neighbors; "install" is "copy the
  file"; the `UUID`/`Company` properties on every library node are provenance for
  exactly this. Preserve this granularity — never introduce cross-object link
  dependencies or a shared "objects blob".

**Important linking detail**: object Makefiles link with raw `ld -shared` (not gcc).
This skips the C runtime startup objects (crti/crtn), which is what allows each module
to define its own `_init()` / `_fini()` entry points. Modules rely on the legacy
`_init`/`_fini` mechanism, not `__attribute__((constructor))`. Don't "fix" an object
Makefile to link with gcc without understanding this.

Deactivated modules: `objects/pulse` (Makefile renamed to `.back`/`.bak`) and
`objects/network` (`Makefile.deactivated`) are excluded from the build.
`objects/out`, `objects/msg`, `objects/pulse`, `objects/reader`, `objects/writer`
are the active ones (`pulse` was reactivated July 2026 with a rewritten
`pulseobj.c` on the working lifecycle: Interval/Count properties, emits "1"/"0"
edges out its `Out` port, finite trains end with `msg_eof`, Count=0 pulses
forever and intentionally holds the program open).

`objects/network/TCPObject.c` deserves special mention: it is **the reference
implementation** — a complete, working object from the predecessor system (VNOS /
Singlestep Technologies, 2003, same author). It cannot compile here (it targets the
VNOS API: `VOBJ`, `MsgFunc`, `SendOMessage`, `InitVnosLib`, `ActivateFunc` state
machines, headers not in this tree) but it is the closest-to-working model of what a
finished object should do. When building new objects, port its structure onto the
new framework's mechanisms:

| TCPObject (VNOS)                                  | This framework                          |
|---------------------------------------------------|-----------------------------------------|
| `ObjectMessageFunc` message switch                | `Handle_Message`                         |
| `INITCLASS_MSG` / `STARTUP_MSG` / `END_MSG`       | `_init` / `ClassStart` / `ClassEnd`      |
| `INITIALIZE_MSG` / `DESTROY_MSG`                  | `InstanceStart` / `InstanceEnd`          |
| `SETVARIABLE_MSG` / `GETVARIABLE_MSG` + var ids   | node properties + intercepts             |
| `SendOMessage(owner, msgID + CALLBACK_OFFSET, …)` | propagation out a property (Connect)     |
| `TCP_REMOTE_CONNECTION_CLOSED_CALLBACK`           | the EOF-on-Out pattern                   |
| instance rings + `SetTaskSleep(ST_SLEEP)` on empty| stop rescheduling tasks → system quiesces|
| `DefaultMessage(superclass, …)` chaining          | roadmap: object subclassing              |

Its lifecycle discipline is the model for the cat flow: instances register into an
active ring on start, the polling task sleeps itself the moment the ring is empty,
and everything goes quiet without anyone calling exit.

## Runtime object loading and registration (the core mechanism)

Startup sequence in `main.c:main()`:

```
main() → ProcessCmdLine() → Init() → InstallObjects() → LoadDefaultApp() → MainLoop until Stopping
```

### 1. The registry is a node subtree

`Init()` creates a `RegObjList` property node on the Main node and hands it to
`object.c` via `ObjSetRegObjList()`. All registration at every level is just
`AddChild()` into this tree:

```
Main
└── RegObjList                      (property node on Main)
    └── library node                (one per loaded .object, added by RegisterLibrary)
        └── class node              (added by RegisterClass, during loadClasses phase)
            └── instance node       (added by RegisterInstance)
```

There is no separate registry data structure. Function pointers are stored on these
nodes as long-typed properties (`SetPropLong` / `GetPropLong`) and cast back to
`msgobj` when called.

### 2. Discovery

`InstallObjects()` calls `ScanDir(".", ".object", LoadObject, 8, 0, PreOrder)`
(`dirscan.c`): a recursive scan **down from the current working directory**, max depth
8, invoking `LoadObject` for every regular file whose name ends in `.object`. This is
why the framework must be run from the project root (or wherever the .object files
live below).

### 3. Load — registration happens as a dlopen side effect

`LoadObject()` (`libload.c`) does only two things:

1. `dlopen(<abs path>, RTLD_LAZY)` — this runs the module's `_init()`, and *that* is
   where registration happens. The module's `_init()` builds a NodeObj describing the
   library — Name, Company, UUID, `State=1`, and `ClassStart` / `ClassEnd` / `ClassMsg`
   function pointers as long properties — and calls `RegisterLibrary(node)`, which
   parents it under RegObjList.
2. `dlsym(handle, "Handle_Message")` as a validity check. If the symbol is missing,
   the library is `dlclose()`d, which runs its `_fini()` → `UnregisterLibrary()`.

The dlopen handle is intentionally not stored anywhere; the module stays resident
because it stays open. `UnregisterLibrary()` currently only marks the node
(`State=0`) rather than deleting it, so unloaded modules remain visible in a `-p`
node dump.

### 4. Two-phase class initialization

Loading and class bring-up are deliberately separate passes. Only after **all**
`.object` files have been scanned does `InstallObjects()` call `loadClasses()`
(`object.c`), which walks RegObjList's children, reads each library node's
`ClassStart` property back out with `GetPropLong`, and invokes it as
`ClassStart(libraryNode, 0, NULL)`.

A module's `ClassStart` (see `objects/reader/reader.c` for the fullest example)
creates a class node carrying `InstanceStart` / `InstanceEnd` function-pointer
properties and calls `RegisterClass(libraryNode, classNode)`. Instances are created
later via `InstanceStart`, which registers a populated instance node with
`RegisterInstance(classNode, instNode)`.

The deferred second phase exists so that dependency-ordered loading (classes
subclassing classes from other modules) can be added — see the "improvement" notes in
main.c. `UnloadClasses()` is the symmetric teardown (walks libraries, calls
`ClassEnd`).

Note: the simpler modules (`out.c`, `msg.c`) currently register `Handle_Message`
itself as the `ClassStart` property, while `reader.c` implements the full
ClassStart/ClassEnd/InstanceStart/InstanceEnd protocol. `reader.c` reflects the
intended pattern.

### 5. Anatomy of an object module

Every `.object` module follows this shape:

```c
int Handle_Message(NodeObj instance, MsgId message, NodeObj data);  // required — dlsym'd by loader

void _init()  {  /* build library node, RegisterLibrary() */  }
void _fini()  {  /* UnregisterLibrary() */  }

int ClassStart / ClassEnd (NodeObj, MsgId, NodeObj);      // called by loadClasses/UnloadClasses
int InstanceStart / InstanceEnd (NodeObj, MsgId, NodeObj); // pointers stored on the class node
```

Message handlers return one of (`callback.h`): `rtrn_handled` (consumed),
`rtrn_propagate` (forward to subscribers), `rtrn_dropped` (unhandled, don't forward).
Message ids: `msg_change`, `msg_update`, `msg_initialize`, `msg_send`.

Per-instance C state (file handles etc.) is malloc'd and stashed on the instance node
as a `"local"` long property — see `InstanceData` in `reader.c`. UI-facing properties
get sub-properties (properties on properties) carrying a `graphics` widget type
(`PROP_TEXTBOX`, `PROP_LED`, `PROP_BUTTON`, `PROP_CHECKBOX` from `object.h`), an
`OnChange` callback pointer, and the `local` pointer — see `SetSubProp()` in reader.c.

## Core data model

- **DataObj** (`data.c/h`): opaque typed value (STRING/INTEGER/HEX/REAL/LONG) with
  automatic conversion between types on get/set.
- **NodeObj** (`node.c/h`): opaque tree node with a name, a DataObj value, a property
  list, children, and siblings. **Properties are themselves nodes**, so properties can
  have properties (used by the sub-property/widget mechanism above). This is the
  single data structure the whole system is built on.
- **Scheduler** (`sched.c/h`, `timer.c`): a TaskList of delayed/immediate callbacks,
  **microsecond resolution** (task_entry stores seconds+micros; `AddTaskDelay`'s
  third arg is micros, `AddTaskMilli` converts). Every SndMsg rides through here,
  so it's tuned for bursts: a task_entry freelist (`GetTask`/`RemoveTask` recycle
  instead of malloc/free per message) and an `insertHint` making same-timestamp
  inserts O(1) amortized. `MainLoop()` updates time, runs due tasks, and sleeps
  via `SchedNextWakeMicros()` — exactly until the next due task, **capped at 1ms**
  because all I/O is polled (a longer sleep is a floor on input latency; the cap
  is what makes dragged sliders feel smooth). When the task count hits zero,
  Main's `State` is set to `Stopping` and the process exits.
- **Object layer** (`object.c/h`): registration callbacks described above plus
  `CreateContainer` / `CreateObject` / `Connect` / `SndMsg` / `DeleteInstance` —
  the user-facing composition API, all working.
- **DebugPrint** (`DebugPrint.c/h`): all logging goes through
  `DebugPrint(msg, __FILE__, __LINE__, type)` with types `PROG_FLOW`, `ERROR`,
  `CMDLINEOPTS`, `REGISTER`, `OBJMSGHANDLING`. The `-v` level gates output
  (`DebugPrintSetLevel` applied at the top of Init from the parsed `loglevel`):
  `-v 0` errors only; `-v 1` (default) adds PROG_FLOW and OBJMSGHANDLING;
  `-v 2` adds REGISTER and CMDLINEOPTS; `-v 3+` adds the library node dump on
  unregister. Thresholds live in `TypeThreshold()` in DebugPrint.c.
## Testing is built into the modules themselves

There is no separate test tree. Each code module carries its own self-test function,
compiled into the module and declared in its header as part of its interface:
`DataTest()` (data.c), `NodeTest()` (node.c), `SchedTest()` (sched.c), `BuffTest()`
(dyn/bufftest.c), `NameSpaceTest()` (namespace.c). The tests ship inside
`libframework.so` alongside the code they exercise.

- **Aggregated at runtime**: `./framework -t` → `PerformTesting()` in main.c calls
  each module's test function in sequence. When adding a module, its test gets added
  to that list.
- **Print-based verification**: tests print their results for visual inspection
  (e.g. DataTest prints the full type-conversion matrix) rather than asserting.
  Standardizing test return values so results can be collected mechanically is an
  open design question noted in main.c's comments.
- **Standalone test builds**: some modules (`sched.c`, `namespace.c`) also contain
  `#ifdef TESTBUILD int main()` blocks so the module can be compiled by itself into
  its own test executable. `schedtest.c` is an additional standalone scheduler
  harness.
- **Intended extension**: main.c's comments sketch loadable `.object` modules
  registering their own test entry points through the registry (like `ClassStart`),
  so `-t` would eventually exercise loaded objects too, not just the core library.

## Scripting: language hosts as bridge clients (July 2026, roadmap Phase 7)

A scripting language is one more loadable `.object` that speaks the JSON
protocol as a BRIDGE CLIENT — "the web bridge is the pattern to follow."
Two hosts exist: `objects/script` (Lua, the first) and `objects/jsscript`
(QuickJS/JavaScript, `objects/jsscript/quickjs/` vendored — MUST include
libbf.c or `dlopen` fails on `bf_context_init`). A host: one interpreter
per instance; `Activate` (re)runs the `Source` property in a fresh
context; ports `In`/`Out` (dataflow), `Print` (output + loud errors),
`Cmd`/`Evt` (wire to a Bridge = full protocol: create/connect/subscribe/…
with session naming and events, a script is a peer of the browser); a
runaway guard (QuickJS interrupt handler with a wall-clock budget — Lua
still lacks one); `ScriptHost=1` on the class node = the runtime-discovery
marker. Details and the full pattern: the `design-language-host-bridge`
memory. Twin tests: `testharness/jstest.py`.

**Scripted composite widget** (Phase 5 + 7 together): a View made into a
black-box widget. `bind-port` (bridge command -> `LinkPropertyAs`) makes
a container's OWN port a transparent link to a child's port (container
ports = the existing alias mechanism, not a new record type) - wiring
to/from the container port resolves through to the child (`ResolvePort`).
So a View.In bound to an inner input control, a View.Out bound to an
inner output control, plus a script INSIDE (a language host wired to a
Bridge = a protocol client) that subscribes to the input control and
drives the output control by path = a widget that behaves like compiled
code but whose LOGIC is editable script. `testharness/widgettest.py`
builds and proves one. This only works because of path addressing (the
script reaches its siblings by path) - the reason Phase 1.5 came first.

`objects/scriptbox` (ScriptBox) is the script WIDGET shell: it holds no
interpreter, it CONTAINS a language-host instance and drives it. Its
panel (the engine's internals view) shows a `Language` dropdown
(`PROP_MENU`, options from the companion `LanguageList` — a registry walk
of `ScriptHost=1` classes), a `Source` box and an `Output` box (both the ONE
`PROP_TEXTBOX` — there is no separate textarea widget; the Textbox
displays text of any size, and an object declares its own box size as
`Rows`/`Cols` annotations on its published Interface entry — properties
are nodes, so no new mechanism), and Run = Activate. Picking a Language SWAPS the inner host
(the code carries over) — Lua works as an inner language with zero
changes to script.c because the inner host is created with `CreateObject`
and wired with `Connect()` like any two objects. `testharness/scriptboxtest.py`.

## Addressing (July 2026, roadmap Phase 1.5)

The ENGINE owns path -> instance: `RegisterPath`/`UnregisterPath`/
`ResolvePath` (object.c) over the namespace trie (namespace.c) — O(path
length) at any session size, and retired names really reclaim their keys.
The reverse is derived, not stored: `PathOfInstance` builds
Container + "/" + Name and verifies by resolving back (anything unnamed
or mid-rename has no path — capture a path BEFORE mutating Name/
Container, see Bridge_Set). The bridge keeps NO alias table — it
registers/resolves against the engine index, and enumeration (listing,
repath walks) is a registry walk + PathOfInstance. Consequence: all
translators share one namespace — instances created over the raw port
are addressable from the GUI and vice versa.

**The root is a real View, and there is no fabricated `/Root` prefix
(July 2026).** The top of the containment hierarchy used to be a fiction:
the bridge glued `/Root/` onto anything with no container and compared
against the literal `"/Root"` to mean "nowhere". Now `CreateRoot`
(object.c) makes an ordinary View with no container — the one thing
allowed to have none, because it is what locations are measured from —
and the app builds `/Root` at boot (CreateDefaultApp), with the palette
and the menus (`ModeMenu`/`FileMenu`) as ordinary instances IN it. No
chrome category, no palette category. A Bridge is handed the root view
and walks views; the client renders `/Root` as its canvas. `Main` is its
own place, `/Main` (the web-flow plumbing lives there, not on a canvas).
Corollary now enforced in `CreateObject`: **everything is created in a
container** — a NULL/unaddressable location is refused and logged (the
`PLACE` debug category at `-v 3`), never silently dropped in the root.
"No container" from a translator means the root view, all the way
through — lookup, placement, AND the event's container scope (events are
scoped by container key; `Bridge_ViewKey` maps `""`→`/` but `/Root`→
`/Root`, so a create/delete scoped to the wrong one silently reaches
nobody — the class of bug that hung create and lingered deleted views).

## Allocation accounting (July 2026)

The core counts what it allocates: plain static alive-counters at every
alloc/free choke point, read through exported getters — `NodeCount`/`DataCount`
(node.c/data.c), `EnvelopeCount` (object.c — queued messages; 0 at rest),
`TaskStructCount` (sched.c — includes pooled entries), `BuffCount`/`QueueCount`
(dyn/). Counting is mechanism (core); publishing is behavior: the **Stats
object** (objects/stats) samples the getters on a timer and writes changed
values into ordinary properties, so a TextOut wired to `Nodes` is a live leak
readout and the fabric is its own leak detector. The discipline: a counter
that grows and never shrinks across a create/destroy cycle IS a leak, named
by its type — `testharness/leaktest.py` enforces it (structural cycles net
exactly zero; a 50-message burst costs exactly its one activate log record,
message-count-independent, proving SndMsg's ownership contract holds).

Leaks this found and fixed: every task-driven object leaked one task_entry
per RE-activation (`local->task = CreateTask(...)` unconditionally in
Activate — now created once per instance life); and `Bridge_CompactFlow`
(the flow log's own GC, dropping a deleted instance's recorded history) was
written but never called — now wired into Bridge_Delete, so the log no
longer grows without bound across create/delete churn.

Two measurement disciplines, learned the expensive way: the Stats tick
samples ALL counters before publishing ANY (publishing is itself allocation
— interleaving makes the observer watch its own wake oscillate forever), and
a client reading a continuously-published property must purge its event
backlog first or it reads stale history (value_of consumes the OLDEST match
— see leaktest's read_counters).

## Current focus (as of July 2026): the "cat" test flow

The near-term milestone is the first end-to-end dataflow. It was first
composed at boot in main.c; since July 2026 the dataflow test flows (cat,
filter/gate, queue/stack, tcp echo) live in **testharness/flowtest.py**,
built over the raw JSON protocol and asserting on subscribed events —
main.c's `CreateDefaultApp()` now builds only the app surfaces (web GUI
flow; the raw/auth bridge flows are present but disabled — the harness
composes its own raw TCP bridge through the web bridge at test time,
`ensure_raw_bridge` in rawtest.py). The flow's design, as originally
composed:

- A **Reader** instance reads `test.txt`; a **Writer** instance writes `test.txt.back`;
  `Connect(Reader "Out" → Writer "In")` — emulating `cat`.
- **Design decision: source and sink are two separate object classes**, not one
  "File" object with a Mode property (an older, abandoned idea). Reader is a
  pure data **source** (owns `Out`, pushes chunks);
  Writer is a pure data **sink** (owns `In`, reacts to arrivals). **The sink
  subscribes to the source**: `Connect()` adds the writer's `In` to a subscription
  list stored on the reader's `Out` property (properties are nodes, so the list
  lives as sub-nodes on `Out` — see the `AddSubscription()` stub in object.c).
  Data flows as **routed messages**, not stored property values: the reader's task
  reads one chunk, wraps it in a data node, and sends it as a message out its `Out`
  port; the routing layer (`SndMsg`/`DispatchMsg` in object.c) walks `Out`'s
  subscription list and delivers that message to every subscriber's handler as
  `(instance, msgid, data)`. The
  `rtrn_handled`/`rtrn_propagate`/`rtrn_dropped` codes in callback.h are the
  per-delivery verdicts. `Out` is a named output port — it exists to hold the
  subscription list and give `Connect()` an endpoint; chunks flow through it, they
  are not retained on it. EOF is just another message id through the same router,
  so sinks switch on msgid (data vs EOF) exactly like TCPObject's message dispatch.
  One source fans out to any number of sinks; the sink never polls.
- The reader drives itself via scheduler tasks, emitting chunks on its `Out`
  property. On end-of-file it sends an **EOF message on its Out** and deactivates
  (stops rescheduling, so its tasks drain).
- The writer receives data through its `In` intercept, buffers, and schedules write
  tasks. When it has received EOF *and* drained its write buffer, it closes the file
  and stops rescheduling.
- **Shutdown is emergent**: nobody calls exit. When both objects go quiet,
  `ExecTasks()` returns 0 and `MainLoop` flips Main's `State` to `Stopping`. This
  mechanism already works — the binary currently exits immediately because nothing
  schedules tasks.

This milestone is implemented and **verified working** (July 2026): with a test.txt
in place, `./framework` copies it to test.txt.back through the Reader → Writer
message flow and exits on its own when the flow drains. How the pieces landed:

- **Routing** (`object.c`): `SndMsg(instance, "Out", msgid, data)` queues a
  `MsgEnvelope` on the scheduler (one `AddTaskNow`, nothing more); when the task
  fires, `DispatchMsg` walks the `Subscriber` sub-nodes on the named port and
  delivers to each (`DeliverToSubscriber`, node.c — the one definition of
  delivery, shared with node.c's own synchronous property-write fan-out).
  `Connect()` builds the subscription: it records `{Instance, Port, Callback}`
  on the source port via `AddSubscription()` — Callback is the sink port's
  `OnMsg` handler if it has one, else 0, in which case delivery applies the
  universal default: store the payload onto the record's `{Instance, Port}`
  (whose own write fans out in turn, so chains hop). This is what lets
  `Connect()` reach ANY property — compiled port, plain data property, or
  `Activate` (an ordinary port since July 2026: `ActivateOnMsg` stamped by
  `RegisterInstance`) — with no adapter species (the old
  `PropertyBinding`/`ActivateBinding` adapters are deleted), so every graph
  walker (list-connections, `CloneConnections`, the delete scrub,
  `Disconnect()` — Connect's inverse, same resolution rules) reads the same
  records. The `WIRE` DebugPrint category traces every wire made/removed at
  `-v 3`.
  **Ownership contract (changed July 2026 with queued dispatch): SndMsg takes
  ownership of `data`** — DispatchMsg frees it after the last subscriber; the
  sender must NOT DelNode it after SndMsg (the old synchronous contract said
  the opposite). Handlers still run synchronously *within* a delivery and must
  copy anything they keep; anything forwarding a message it received sends a
  fresh copy, since the original belongs to the upstream sender's own queued
  delivery (see Filter_OnIn). DispatchMsg re-reads the port's live Subscriber
  list at delivery time, not a snapshot.
- **Deletion safety** (`object.c`): because messages can be in flight when
  either end dies, `DeleteInstance` runs `ScrubRegistrySubscriptions` (strips
  every Subscriber entry targeting the dead instance, registry-wide and
  recursively through sub-properties) and `CancelPendingSends` (blanks the
  outPort on any queued envelope the dead instance sent, including the
  mid-tick runnow bucket). Both fix ASan-confirmed use-after-frees. Supporting
  primitives added for this: `RemoveProp(owner, prop)` in node.c (unlink
  without freeing; props have no parent back-pointer, hence the owner arg),
  `DelData` in data.c, and `DelNode` now frees the name/value DataObjs every
  node owns (was a two-struct leak per deleted node).
- **Instantiation** (`object.c`): `CreateObject()` finds the class via
  `FindClass()` (walks RegObjList → libraries → classes), calls its
  `InstanceStart` pointer, and returns the new instance which `RegisterInstance()`
  left in the class's `LastInstance` property. Instances are activated with
  `ActivateInstance()`, which calls the `Activate` function pointer the instance
  carries on itself.
- **Scheduler access**: `ObjSetTaskList()` / `ObjGetTaskList()` in object.c share
  main's TaskList with loaded objects (same mechanism as RegObjList — the global
  lives in libframework.so, which main and all modules share). Task callbacks are
  invoked as `(data, data, task_callback)`, so objects arm tasks with their
  instance node as the data and keep the TaskPtr in their `local` struct.
- **Reader** (`objects/reader/reader.c`): full lifecycle; `Activate` opens
  Filename and arms a read task; the task freads one chunk, sends it out `Out`
  (`msg_send`, string payload), re-arms; at feof sends `msg_eof`, closes,
  doesn't re-arm. Text payloads only (null-terminated).
- **Writer** (`objects/writer/writer.c`): `In` port with `OnMsg = Writer_OnIn`;
  chunks are copied into a dyn `buff` and a drain task (armed on demand, guarded
  by a `scheduled` flag) writes oldest-first via `buffGetBlockFromTail` — note
  `buffGetBlockFromHead` is LIFO (head is the write end); tail is FIFO. After
  `msg_eof` and an empty buffer it closes and stops re-arming.
- **Out** (`objects/out/out.c`): a debug probe. Subscribe its `In` to any source
  port and it prints every passing message to stdout tagged with its `Label`
  property (msg id + payload size + payload; on EOF it prints message/byte
  totals). It prints synchronously in its handler and **never schedules a task**,
  so probes can be dropped onto any connection without holding the program open.
  `Echo=0` silences a probe without disconnecting it. It returns `rtrn_propagate`
  (a probe watches, it doesn't consume). The cat flow (flowtest.py) proves the
  same fan-out by subscribing its own tap to the Reader's `Out` alongside the
  Writer — two subscribers on one port.
- **Filter** (`objects/filter/filter.c`): a mid-flow object — `In` handler tests
  each message and forwards passers out its own `Out` with a fresh **copy** of
  the data node (the received original is owned by the upstream sender's queued
  delivery; chains are hops through the scheduler, one queued send each). `Mode`
  property: `all` / `change` (dedupe against last seen) / `ones` / `zeros`.
  `msg_eof` always passes, even through a disabled filter, so streams can always
  finish downstream. No tasks; never holds the program open.
- **The Enable port convention** (on Pulse, Reader, Writer, Out, Filter): every
  object carries an `Enable` input port (created with `SetPropStr(inst,
  "Enable", "1")`, handler registered as `OnMsg` on the port). Send `1` to
  enable, `0` to disable; EOF on an enable line is ignored. Because it's an
  ordinary port, **any source can drive any object's Enable through
  `Connect()`** — e.g. a timer/pulse can shut an object down (the planned
  30-second TCP timeout is just `Connect(Timer, "Out", TCP, "Enable")`).
  Semantics: task-driven objects **pause by not rescheduling** (zero cost while
  disabled; the rising edge re-arms via a `scheduled` flag), sinks just gate
  their handler; the Writer keeps buffering while paused and drains on resume.
  Note: a fully paused flow schedules nothing, so the program will quiesce and
  exit with data still buffered — pausing is not a way to keep a program alive.
- **Port-shadowing landmine**: never call `SetProp*` with a port's name after
  the port is created — SetProp prepends a *new* shadowing prop, and Connect /
  SndMsg would find the shadow (without `OnMsg`/subscribers) instead of the
  real port. Update port state with `SetValueStr(portNode, ...)` on the node
  itself, as the Enable handlers do. (Ports are created as STRING props where
  state is mirrored, since `SetValueInt(node, 0)` is a no-op — see below.)
- **TCP** (`objects/network/tcp.c`, built as `tcp.object`; `TCPObject.c` in the
  same directory stays as the uncompiled VNOS reference): server mode, any
  number of simultaneous connections (a linked ring serviced by one shared
  polling task, each message tagged with a `Conn` property identifying its
  connection — TCPObject's ring pattern, done right). `LocalPort` picks the
  port; `LocalAddr` (optional) picks the interface to bind — e.g. `127.0.0.1`
  for local-only; absent/empty means all interfaces (this is what the `-ip`
  option feeds). Received bytes go out `Out` (one message per recv), messages
  on `In` are buffered (dyn buff) and drained to the peer by the polling task;
  peer close returns it to listening.
  `Enable=0` is a **full shutdown**, not a pause: sockets close, `msg_eof` goes
  out `Out`, the poll task stops re-arming (re-enabling does not restart it —
  activation is one-shot). A timed server is therefore just a Pulse wired to
  `Enable`; note Pulse emits rising-then-falling, so a 30s lifetime uses
  `Interval=15000, Count=1` (falling edge at 30s). `SO_REUSEADDR` is set;
  `SIGPIPE` is ignored (set in ClassStart). Client mode is still to come — the
  connecting state machine lives in TCPObject.c.
- **msg_eof** added to callback.h (appended, existing values unshifted).
- **Bug fixed in sched.c**: `AddTaskDelay` never stored its `data` argument
  (`task->data` stayed uninitialized; `ExecTasks` passes it to the callback).
  Invisible before because SchedTest's callback ignores its args.
- **Bug fixed in dyn/queue.c**: `queuePush` discarded the first `buffAdd`'s
  return value, so the always-entered "retry" branch added every payload
  **twice** to the data buffer (against one size entry), scrambling pop order
  (first live run replayed 1,0,1,0,1,0 as 1,1,0,0,1,1). The size-entry add was
  also nested inside the retry branch and had to move out; a push was
  incrementing `numPopsLIFO` instead of `numPushes`, fixed in passing.
- `GetPropInt` / `GetPropStr` / `GetNextProp` (declared in node.h, never defined)
  are now implemented in node.c. `GetNextProp(node)` returns the *first* property;
  iterate with `GetNextSibling`.

Still parked (not needed for this milestone): the `Intercept` mechanism lives only
in the `SetPropLongOLD`/`old` variants in node.c; the active `SetProp*` functions
do not fire intercepts. Property writes also shadow (prepend a new prop node)
rather than update in place — reads see the newest because `GetPropNode` searches
from the head.

- **Queue and Stack** (`objects/queue/queue.c`, one module registering both
  classes — the class name picks the pop direction at InstanceStart): a thin
  layer over `dyn/queue.c`, which preserves message boundaries (unlike buff)
  and pops either end. Fully passive, no tasks, triggered by other things:
  `In` pushes (payload + msgid in the queue's `type` field, so `msg_eof`
  rides through in-band), `Clock` pops exactly one entry per arriving message
  and sends it out `Out` with its stored id; empty pops are silent no-ops.
  A disabled queue still accepts pushes but ignores its Clock. A Stack pops
  the EOF first if it was pushed last — that's what reversal means. Drive
  Clock with a Pulse for a rate limiter / clocked shift register.

Still cheap and unbuilt: Timer (In handler sends inter-message time out Out),
Random (pulse skeleton with High/Low props).

The long-range plan lives in **ROADMAP.md**: a web app where users log in, see
their canvas, drag objects from a palette into dataflows, and control them
through skinned instrument panels (LED/slider/VU meter/text widgets bound to
properties); finished flows become composite objects with their own ports.
Keystones in order: node-tree ⇄ JSON serialization, reviving the parked
Intercept path so property writes fire object callbacks, the SetSubProp widget
pattern (already sketched in reader.c) on every object, HTTP/WebSocket/bridge
objects over the TCP object, container ports for composition, then federated
palettes: web APIs and MCP servers imported as palette classes (their schemas
translated into the published-interface format; instances are generic proxies),
and the framework itself exposed as an MCP server so agents can invoke and
build flows. After that, languages as extensions: interpreter-hosting objects
whose scripts register real classes via trampoline handlers (function pointers
in node properties don't care what they point at), and script overrides on any
compiled object's properties through the revived Intercept path. Also on the
roadmap: object self-tests via the registry, dependency-ordered class loading,
TCP client mode/multi-connection.

## Current state / known inconsistencies (facts, not fixes)

This is an in-progress codebase; parts of the design exist only as comments and
stubs. Known rough edges to be aware of before touching anything:

- Registration naming convention (resolved July 2026): the exported symbols are
  `RegisterClass`/`UnRegisterClass` and `RegisterInstance`/`UnRegisterInstance`
  (capital R in Un**R**egister), but `RegisterLibrary`/`UnregisterLibrary`
  (lowercase r). object.h now matches object.c; call exactly these spellings from
  modules — lazy binding means a misspelled symbol only fails when first called.
- `SetValueInt(node, 0)` is a silent no-op: node.c guards with `if (!node || !value)`,
  so a zero can never be stored through it. Fresh INTEGER nodes read as 0 anyway,
  but don't rely on SetValueInt for zeros — this is why pulse edges travel as the
  strings "1"/"0" (sinks wanting numbers use the automatic data conversion).
- Several functions use implicit-int K&R style (`loadClasses()`, `PrintRegInfo()`);
  the root Makefile builds with `-w`, so no warnings surface.
- The Makefile's `depend` output was generated on a Linux box; the repo is currently
  sitting on a macOS host, but the build targets Linux conventions
  (`ld -shared`, `_init`/`_fini`).
